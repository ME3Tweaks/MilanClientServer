using System.Buffers.Binary;
using System.Buffers.Text;
using System.Net;

namespace MilanServer.Tests;

public sealed class DiscoveryTests : StoreTestBase
{
    static readonly IPEndPoint HostGameSocket = new(IPAddress.Parse("203.0.113.10"), 40001);
    static readonly IPEndPoint JoinerGameSocket = new(IPAddress.Parse("198.51.100.20"), 50002);

    static (UInt128 Id, byte[] Secret) Decode(UdpTicket ticket) =>
        (BinaryPrimitives.ReadUInt128BigEndian(Base64Url.DecodeFromChars(ticket.TicketId)), Base64Url.DecodeFromChars(ticket.Secret));

    static byte[] Request(UdpTicket ticket, ulong sequence)
    {
        (UInt128 id, byte[] secret) = Decode(ticket);
        byte[] packet = new byte[DiscoveryPacket.RequestLength];
        DiscoveryPacket.WriteRequest(packet, id, sequence, secret);
        return packet;
    }

    int Bind(UdpTicket ticket, ulong sequence, IPEndPoint source, out IPEndPoint observed)
    {
        byte[] response = new byte[DiscoveryPacket.ResponseLength];
        int length = Store.HandleDiscovery(Request(ticket, sequence), source, response);
        observed = new IPEndPoint(IPAddress.None, 0);
        if (length > 0)
        {
            (UInt128 id, byte[] secret) = Decode(ticket);
            Assert.True(DiscoveryPacket.TryReadResponse(response, secret, out UInt128 echoedId, out ulong echoedSequence, out observed));
            Assert.Equal((id, sequence), (echoedId, echoedSequence));
        }
        return length;
    }

    [Fact]
    public void Packets_RoundTripAndResponsesAreSmaller()
    {
        byte[] secret = [.. Enumerable.Range(1, 32).Select(i => (byte)i)];
        byte[] request = new byte[DiscoveryPacket.RequestLength];
        DiscoveryPacket.WriteRequest(request, 42, 7, secret);

        Assert.True(DiscoveryPacket.TryReadRequest(request, out DiscoveryPacket.BindingRequest parsed));
        Assert.Equal(new DiscoveryPacket.BindingRequest(42, 7), parsed);
        Assert.True(DiscoveryPacket.VerifyRequestTag(request, secret));
        Assert.False(DiscoveryPacket.VerifyRequestTag(request, new byte[32]));
        Assert.True(DiscoveryPacket.ResponseLength < DiscoveryPacket.RequestLength);

        byte[] response = new byte[DiscoveryPacket.ResponseLength];
        DiscoveryPacket.WriteResponse(response, 42, 7, HostGameSocket, secret);
        Assert.True(DiscoveryPacket.TryReadResponse(response, secret, out _, out _, out IPEndPoint observed));
        Assert.Equal(HostGameSocket, observed);

        response[33] ^= 1;
        Assert.False(DiscoveryPacket.TryReadResponse(response, secret, out _, out _, out _));
    }

    [Theory]
    [InlineData(0)]  // magic
    [InlineData(4)]  // version
    [InlineData(5)]  // type
    [InlineData(7)]  // reserved
    [InlineData(40)] // padding
    public void Request_RejectsBadStructure(int index)
    {
        byte[] request = new byte[DiscoveryPacket.RequestLength];
        DiscoveryPacket.WriteRequest(request, 42, 7, new byte[32]);
        request[index] ^= 0x5A;

        Assert.False(DiscoveryPacket.TryReadRequest(request, out _));
    }

    [Fact]
    public void Request_RejectsWrongLength()
    {
        byte[] request = new byte[DiscoveryPacket.RequestLength + 1];
        DiscoveryPacket.WriteRequest(request, 42, 7, new byte[32]);

        Assert.False(DiscoveryPacket.TryReadRequest(request, out _));
        Assert.False(DiscoveryPacket.TryReadRequest(request.AsSpan(0, DiscoveryPacket.RequestLength - 1), out _));
    }

    [Fact]
    public void HostBinding_BecomesPublicObservedCandidate()
    {
        CreateLobbyResponse lobby = Host(State(port: 7777, lan: ["192.168.1.20"]));

        Assert.Equal(DiscoveryPacket.ResponseLength, Bind(lobby.UdpTicket, 1, HostGameSocket, out IPEndPoint observed));
        Assert.Equal(HostGameSocket, observed);

        EndpointCandidate[] expected =
        [
            new("203.0.113.10", 40001, CandidateKind.PublicObserved),
            new("203.0.113.10", 7777, CandidateKind.PublicReported),
            new("192.168.1.20", 7777, CandidateKind.Lan),
        ];
        Assert.Equal(expected, Redeem(lobby).Value.HostCandidates);
    }

    [Fact]
    public void HostBinding_ReplacesMatchingReportedCandidate()
    {
        CreateLobbyResponse lobby = Host(State(port: 7777));

        Bind(lobby.UdpTicket, 1, new IPEndPoint(HostIp, 7777), out _);

        EndpointCandidate[] expected = [new("203.0.113.10", 7777, CandidateKind.PublicObserved)];
        Assert.Equal(expected, Redeem(lobby).Value.HostCandidates);
    }

    [Fact]
    public void Binding_RejectsReplayAndEnforcesInterval()
    {
        CreateLobbyResponse lobby = Host();

        Assert.NotEqual(0, Bind(lobby.UdpTicket, 5, HostGameSocket, out _));
        Advance(1);
        Assert.Equal(0, Bind(lobby.UdpTicket, 5, HostGameSocket, out _));
        Assert.Equal(0, Bind(lobby.UdpTicket, 4, HostGameSocket, out _));

        Assert.NotEqual(0, Bind(lobby.UdpTicket, 6, HostGameSocket, out _));
        Assert.Equal(0, Bind(lobby.UdpTicket, 7, HostGameSocket, out _));
        Time.Advance(TimeSpan.FromMilliseconds(Options.DiscoveryMinIntervalMilliseconds));
        Assert.NotEqual(0, Bind(lobby.UdpTicket, 7, HostGameSocket, out _));
    }

    [Fact]
    public void Binding_DropsForgedUnknownAndIPv6Requests()
    {
        CreateLobbyResponse lobby = Host();
        byte[] response = new byte[DiscoveryPacket.ResponseLength];

        byte[] forged = Request(lobby.UdpTicket, 1);
        forged[60] ^= 1;
        Assert.Equal(0, Store.HandleDiscovery(forged, HostGameSocket, response));

        byte[] unknown = new byte[DiscoveryPacket.RequestLength];
        DiscoveryPacket.WriteRequest(unknown, 12345, 1, new byte[32]);
        Assert.Equal(0, Store.HandleDiscovery(unknown, HostGameSocket, response));

        Assert.Equal(0, Store.HandleDiscovery(Request(lobby.UdpTicket, 1), new IPEndPoint(IPAddress.Parse("2001:db8::1"), 4000), response));
        Assert.NotEqual(0, Store.HandleDiscovery(Request(lobby.UdpTicket, 1),
            new IPEndPoint(IPAddress.Parse("::ffff:203.0.113.10"), 4000), response));
    }

    [Fact]
    public void Binding_StopsWhenLobbyOrReservationEnds()
    {
        CreateLobbyResponse lobby = Host(State(players: 2));
        JoinReservation cancelled = Redeem(lobby).Value;
        JoinReservation expiring = Redeem(lobby, OtherJoinerIp).Value;

        Assert.Null(Store.CancelReservation(cancelled.ReservationId, cancelled.JoinKey));
        Assert.Equal(0, Bind(cancelled.UdpTicket, 1, JoinerGameSocket, out _));

        Advance(Options.ReservationSeconds);
        Assert.Equal(0, Bind(expiring.UdpTicket, 1, JoinerGameSocket, out _));

        Assert.Null(Store.DeleteLobby(lobby.LobbyId, lobby.HostKey));
        Assert.Equal(0, Bind(lobby.UdpTicket, 1, HostGameSocket, out _));
    }

    [Fact]
    public async Task Introductions_CarryJoinerCandidates()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation reservation = Redeem(lobby).Value;

        Bind(reservation.UdpTicket, 1, JoinerGameSocket, out _);
        Assert.Null(Store.ReportJoinerCandidates(reservation.ReservationId, reservation.JoinKey,
            new JoinerCandidatesRequest(62000, ["10.0.0.7"])));

        IntroductionList list = (await Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, -1, TestContext.Current.CancellationToken)).Value;

        Introduction introduction = Assert.Single(list.Introductions);
        Assert.Equal(reservation.ReservationId, introduction.ReservationId);
        Assert.Equal(reservation.TraversalKey, introduction.TraversalKey);
        Assert.False(introduction.SamePublicAddress);
        EndpointCandidate[] expected =
        [
            new("198.51.100.20", 50002, CandidateKind.PublicObserved),
            new("10.0.0.7", 62000, CandidateKind.Lan),
        ];
        Assert.Equal(expected, introduction.JoinerCandidates);
    }

    [Fact]
    public async Task Introductions_UseObservedAddressForLocalityHint()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation housemate = Redeem(lobby).Value;
        Bind(lobby.UdpTicket, 1, HostGameSocket, out _);

        Bind(housemate.UdpTicket, 1, new IPEndPoint(HostIp, 40999), out _);

        IntroductionList list = (await Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, -1, TestContext.Current.CancellationToken)).Value;
        Assert.True(Assert.Single(list.Introductions).SamePublicAddress);
    }

    [Fact]
    public void JoinerCandidates_AreValidated()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation reservation = Redeem(lobby).Value;

        Assert.Equal("invalid_request", Store.ReportJoinerCandidates(reservation.ReservationId, reservation.JoinKey,
            new JoinerCandidatesRequest(62000, ["8.8.8.8"]))?.Code);
        Assert.Equal("invalid_request", Store.ReportJoinerCandidates(reservation.ReservationId, reservation.JoinKey,
            new JoinerCandidatesRequest(0))?.Code);
        Assert.Equal(Failures.ReservationNotFound, Store.ReportJoinerCandidates(reservation.ReservationId, lobby.HostKey,
            new JoinerCandidatesRequest(62000)));
    }

    [Fact]
    public async Task IntroductionPoll_WakesOnNewReservation()
    {
        CreateLobbyResponse lobby = Host();
        CancellationToken ct = TestContext.Current.CancellationToken;
        long version = (await Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, -1, ct)).Value.Version;

        Task<Outcome<IntroductionList>> poll = Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, version, ct);
        Assert.False(poll.IsCompleted);

        JoinReservation reservation = Redeem(lobby).Value;

        IntroductionList list = (await poll.WaitAsync(TimeSpan.FromSeconds(5), ct)).Value;
        Assert.True(list.Version > version);
        Assert.Equal(reservation.ReservationId, Assert.Single(list.Introductions).ReservationId);
    }

    [Fact]
    public async Task IntroductionPoll_TimesOutUnchanged_AndEndsWithLobby()
    {
        CreateLobbyResponse lobby = Host();
        CancellationToken ct = TestContext.Current.CancellationToken;
        long version = (await Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, -1, ct)).Value.Version;

        Task<Outcome<IntroductionList>> idle = Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, version, ct);
        Advance(Options.LongPollSeconds);
        Assert.Equal(version, (await idle.WaitAsync(TimeSpan.FromSeconds(5), ct)).Value.Version);

        Task<Outcome<IntroductionList>> closing = Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, version, ct);
        Assert.Null(Store.DeleteLobby(lobby.LobbyId, lobby.HostKey));
        Assert.Equal(Failures.LobbyNotFound, (await closing.WaitAsync(TimeSpan.FromSeconds(5), ct)).Failure);
    }

    [Fact]
    public async Task ReservationPoll_WakesOnHostBinding()
    {
        CreateLobbyResponse lobby = Host();
        JoinReservation reservation = Redeem(lobby).Value;
        CancellationToken ct = TestContext.Current.CancellationToken;
        ReservationStatus initial = (await Store.PollReservationAsync(reservation.ReservationId, reservation.JoinKey, -1, ct)).Value;

        Task<Outcome<ReservationStatus>> poll = Store.PollReservationAsync(reservation.ReservationId, reservation.JoinKey, initial.Version, ct);
        Assert.False(poll.IsCompleted);
        Bind(lobby.UdpTicket, 1, HostGameSocket, out _);

        ReservationStatus updated = (await poll.WaitAsync(TimeSpan.FromSeconds(5), ct)).Value;
        Assert.Equal(new EndpointCandidate("203.0.113.10", 40001, CandidateKind.PublicObserved), updated.HostCandidates[0]);
    }

    [Fact]
    public async Task HostHeartbeat_DoesNotWakePolls()
    {
        CreateLobbyResponse lobby = Host();
        CancellationToken ct = TestContext.Current.CancellationToken;
        long version = (await Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, -1, ct)).Value.Version;
        Task<Outcome<IntroductionList>> poll = Store.PollIntroductionsAsync(lobby.LobbyId, lobby.HostKey, version, ct);

        Report(lobby, State(1));

        Assert.False(poll.IsCompleted);
    }
}
