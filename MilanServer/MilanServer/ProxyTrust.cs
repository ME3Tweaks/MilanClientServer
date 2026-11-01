using System.Collections.Concurrent;
using System.Net;
using Microsoft.Extensions.Options;

namespace MilanServer;

// Milan:TrustedProxies: reverse proxies whose X-Forwarded-For and X-Forwarded-Proto headers are
// applied, as single addresses or CIDR ranges such as 172.16.0.0/12. Loopback is always trusted.
internal static class ProxyTrust
{
    // Any client can send forwarding headers, so the warning remembers only this many sources.
    const int MaxWarnedSources = 16;

    public static bool IsValidEntry(string entry) =>
        IPAddress.TryParse(entry, out _) || IPNetwork.TryParse(entry, out _);

    public static void Add(ForwardedHeadersOptions forwarded, string entry)
    {
        if (IPAddress.TryParse(entry, out IPAddress? address))
            forwarded.KnownProxies.Add(address);
        else
            forwarded.KnownIPNetworks.Add(IPNetwork.Parse(entry));
    }

    // A reverse proxy missing from TrustedProxies makes its HTTPS requests look like plain HTTP
    // from the proxy. Logs each untrusted source of forwarding headers once, with the fix.
    public static IApplicationBuilder UseUntrustedProxyWarning(this IApplicationBuilder app)
    {
        ForwardedHeadersOptions forwarded = app.ApplicationServices.GetRequiredService<IOptions<ForwardedHeadersOptions>>().Value;
        ILogger logger = app.ApplicationServices.GetRequiredService<ILoggerFactory>().CreateLogger(typeof(ProxyTrust));
        ConcurrentDictionary<IPAddress, byte> warned = new();
        return app.Use((http, next) =>
        {
            IHeaderDictionary headers = http.Request.Headers;
            IPAddress? source = http.Connection.RemoteIpAddress;
            if (source is not null && warned.Count < MaxWarnedSources
                && (headers.ContainsKey(forwarded.ForwardedForHeaderName) || headers.ContainsKey(forwarded.ForwardedProtoHeaderName))
                && !IsTrusted(forwarded, source) && warned.TryAdd(NetworkAddresses.Normalize(source)!, 0))
            {
                string address = NetworkAddresses.Normalize(source)!.ToString();
                logger.LogWarning("Ignored X-Forwarded-For and X-Forwarded-Proto from {Source}, which is not a trusted proxy, so its "
                    + "requests count as plain HTTP from that address. If it is your reverse proxy, set Milan__TrustedProxies__0={Proxy} "
                    + "(a CIDR range also works).", address, address);
            }
            return next(http);
        });
    }

    // Matches ForwardedHeadersMiddleware, which also checks the IPv4 form of an IPv4-mapped address.
    static bool IsTrusted(ForwardedHeadersOptions forwarded, IPAddress source) =>
        (source.IsIPv4MappedToIPv6 && IsTrusted(forwarded, source.MapToIPv4()))
        || forwarded.KnownProxies.Contains(source)
        || forwarded.KnownIPNetworks.Any(network => network.Contains(source));
}
