using System.Net;
using System.Net.Sockets;

namespace MilanServer;

internal static class NetworkAddresses
{
    public static IPAddress? Normalize(IPAddress? address) =>
        address is { IsIPv4MappedToIPv6: true } ? address.MapToIPv4() : address;

    // The UE3 UTcpNetDriver socket is IPv4-only, so an IPv6 control address is not
    // a usable game candidate.
    public static IPAddress? PublicIPv4(IPAddress? address)
    {
        IPAddress? normalized = Normalize(address);
        return normalized?.AddressFamily == AddressFamily.InterNetwork ? normalized : null;
    }

    // Key for per-address limits and rate limiting. IPv6 is grouped by /64 because
    // a single subscriber usually controls a whole /64.
    public static string AccountingKey(IPAddress? address)
    {
        IPAddress? normalized = Normalize(address);
        if (normalized is null)
            return "unknown";

        if (normalized.AddressFamily == AddressFamily.InterNetworkV6)
        {
            byte[] bytes = normalized.GetAddressBytes();
            Array.Clear(bytes, 8, 8);
            return $"{new IPAddress(bytes)}/64";
        }

        return normalized.ToString();
    }

    // Accepts only canonical dotted-quad IPv4 in ranges that are not routable on the
    // public Internet, so a host cannot point joiners' probes at third parties.
    public static bool TryParseLanAddress(string? input, out IPAddress address)
    {
        address = IPAddress.None;
        if (input is null
            || !IPAddress.TryParse(input, out IPAddress? parsed)
            || parsed.AddressFamily != AddressFamily.InterNetwork
            || parsed.ToString() != input)
        {
            return false;
        }

        byte[] b = parsed.GetAddressBytes();
        bool isLan = b[0] == 10
            || (b[0] == 172 && (b[1] & 0xF0) == 16)
            || (b[0] == 192 && b[1] == 168)
            || (b[0] == 100 && (b[1] & 0xC0) == 64); // 100.64/10: CGNAT shared space and overlay VPNs.
        if (!isLan)
            return false;

        address = parsed;
        return true;
    }

    public static bool IsLanAddress(IPAddress? address) =>
        address is not null && TryParseLanAddress(address.ToString(), out _);
}
