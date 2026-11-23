using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using Microsoft.Extensions.Options;

namespace MilanServer;

// UDP endpoint discovery v1. A peer sends a binding request from its UE3 game socket;
// the server answers with the address and port it observed, which becomes that
// peer's PublicObserved candidate. All integers are big-endian.
//
// Request, 64 bytes:
//   0  4  magic "MLNR"          4  1  version (1)       5  1  type (1)
//   6  2  zero                  8 16  ticket ID        24  8  sequence
//  32 16  zero padding (keeps every response smaller than its request)
//  48 16  HMAC-SHA256(ticket secret, bytes 0..47), first 16 bytes
//
// Response, 56 bytes:
//   0  4  magic "MLNR"          4  1  version (1)       5  1  type (2)
//   6  2  zero                  8 16  ticket ID        24  8  echoed sequence
//  32  4  observed IPv4        36  2  observed port    38  2  zero
//  40 16  HMAC-SHA256(ticket secret, bytes 0..39), first 16 bytes
//
// The server answers only authenticated requests whose sequence is higher than any
// accepted before for that ticket, and at most once per DiscoveryMinIntervalMilliseconds.
// Anything else is dropped without a reply.
public static class DiscoveryPacket
{
    public const int RequestLength = 64;
    public const int ResponseLength = 56;
    public const byte ProtocolVersion = 1;
    public const byte TypeBindingRequest = 1;
    public const byte TypeBindingResponse = 2;
    const int TagLength = 16;

    static ReadOnlySpan<byte> Magic => "MLNR"u8;

    public readonly record struct BindingRequest(UInt128 TicketId, ulong Sequence);

    // Checks structure only. Verify the tag once the ticket secret is known.
    public static bool TryReadRequest(ReadOnlySpan<byte> packet, out BindingRequest request)
    {
        request = default;
        if (!HasHeader(packet, RequestLength, TypeBindingRequest) || packet[32..48].ContainsAnyExcept((byte)0))
        {
            return false;
        }

        request = new BindingRequest(
            BinaryPrimitives.ReadUInt128BigEndian(packet[8..24]),
            BinaryPrimitives.ReadUInt64BigEndian(packet[24..32]));
        return true;
    }

    public static bool VerifyRequestTag(ReadOnlySpan<byte> packet, ReadOnlySpan<byte> secret) =>
        VerifyTag(packet[..48], packet[48..RequestLength], secret);

    public static void WriteRequest(Span<byte> destination, UInt128 ticketId, ulong sequence, ReadOnlySpan<byte> secret)
    {
        destination = destination[..RequestLength];
        destination.Clear();
        WriteHeader(destination, TypeBindingRequest, ticketId, sequence);
        ComputeTag(destination[..48], secret, destination[48..]);
    }

    public static void WriteResponse(Span<byte> destination, UInt128 ticketId, ulong sequence, IPEndPoint observed, ReadOnlySpan<byte> secret)
    {
        destination = destination[..ResponseLength];
        destination.Clear();
        WriteHeader(destination, TypeBindingResponse, ticketId, sequence);
        observed.Address.TryWriteBytes(destination[32..36], out _);
        BinaryPrimitives.WriteUInt16BigEndian(destination[36..38], (ushort)observed.Port);
        ComputeTag(destination[..40], secret, destination[40..]);
    }

    public static bool TryReadResponse(ReadOnlySpan<byte> packet, ReadOnlySpan<byte> secret,
        out UInt128 ticketId, out ulong sequence, out IPEndPoint observed)
    {
        ticketId = default;
        sequence = default;
        observed = new IPEndPoint(IPAddress.None, 0);
        if (!HasHeader(packet, ResponseLength, TypeBindingResponse) || packet[38] != 0 || packet[39] != 0
            || !VerifyTag(packet[..40], packet[40..ResponseLength], secret))
        {
            return false;
        }

        ticketId = BinaryPrimitives.ReadUInt128BigEndian(packet[8..24]);
        sequence = BinaryPrimitives.ReadUInt64BigEndian(packet[24..32]);
        observed = new IPEndPoint(new IPAddress(packet[32..36]), BinaryPrimitives.ReadUInt16BigEndian(packet[36..38]));
        return true;
    }

    static bool HasHeader(ReadOnlySpan<byte> packet, int length, byte type) =>
        packet.Length == length && packet[..4].SequenceEqual(Magic)
        && packet[4] == ProtocolVersion && packet[5] == type && packet[6] == 0 && packet[7] == 0;

    static void WriteHeader(Span<byte> destination, byte type, UInt128 ticketId, ulong sequence)
    {
        Magic.CopyTo(destination);
        destination[4] = ProtocolVersion;
        destination[5] = type;
        BinaryPrimitives.WriteUInt128BigEndian(destination[8..24], ticketId);
        BinaryPrimitives.WriteUInt64BigEndian(destination[24..32], sequence);
    }

    static void ComputeTag(ReadOnlySpan<byte> data, ReadOnlySpan<byte> secret, Span<byte> tag)
    {
        Span<byte> full = stackalloc byte[HMACSHA256.HashSizeInBytes];
        HMACSHA256.HashData(secret, data, full);
        full[..TagLength].CopyTo(tag);
    }

    static bool VerifyTag(ReadOnlySpan<byte> data, ReadOnlySpan<byte> tag, ReadOnlySpan<byte> secret)
    {
        Span<byte> expected = stackalloc byte[TagLength];
        ComputeTag(data, secret, expected);
        return CryptographicOperations.FixedTimeEquals(expected, tag);
    }
}

// The UDP port actually bound, for /v1/info. Zero when discovery is disabled.
public sealed class DiscoveryEndpoint
{
    public int Port { get; set; }
}

// IPv4 only, matching the UE3 UTcpNetDriver socket.
public sealed class UdpDiscoveryService(
    MatchmakingStore store,
    IOptions<MilanServerOptions> options,
    DiscoveryEndpoint endpoint,
    ILogger<UdpDiscoveryService> logger) : BackgroundService
{
    // Stops Windows from failing later receives after an ICMP port-unreachable.
    const int SIO_UDP_CONNRESET = -1744830452;

    Socket? _socket;

    // Binds before the host finishes starting, so a port conflict fails startup.
    public override Task StartAsync(CancellationToken cancellationToken)
    {
        if (options.Value.UdpEnabled)
        {
            _socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            if (OperatingSystem.IsWindows())
            {
                _socket.IOControl(SIO_UDP_CONNRESET, [0, 0, 0, 0], null);
            }

            _socket.Bind(new IPEndPoint(IPAddress.Any, options.Value.UdpPort));
            endpoint.Port = ((IPEndPoint)_socket.LocalEndPoint!).Port;
            logger.LogInformation("UDP endpoint discovery listening on port {Port}.", endpoint.Port);
        }

        return base.StartAsync(cancellationToken);
    }

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        if (_socket is null)
        {
            return;
        }

        byte[] receiveBuffer = new byte[2048];
        byte[] sendBuffer = new byte[DiscoveryPacket.ResponseLength];
        EndPoint anyRemote = new IPEndPoint(IPAddress.Any, 0);

        while (!stoppingToken.IsCancellationRequested)
        {
            SocketReceiveFromResult received;
            try
            {
                received = await _socket.ReceiveFromAsync(receiveBuffer, SocketFlags.None, anyRemote, stoppingToken);
            }
            catch (OperationCanceledException)
            {
                break;
            }
            catch (SocketException ex)
            {
                logger.LogDebug(ex, "UDP receive failed.");
                continue;
            }

            int length = store.HandleDiscovery(receiveBuffer.AsSpan(0, received.ReceivedBytes),
                (IPEndPoint)received.RemoteEndPoint, sendBuffer);
            if (length == 0)
            {
                continue;
            }

            try
            {
                await _socket.SendToAsync(sendBuffer.AsMemory(0, length), SocketFlags.None, received.RemoteEndPoint, stoppingToken);
            }
            catch (SocketException ex)
            {
                logger.LogDebug(ex, "UDP send failed.");
            }
        }
    }

    public override void Dispose()
    {
        _socket?.Dispose();
        base.Dispose();
    }
}
