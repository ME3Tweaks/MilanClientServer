using Microsoft.Extensions.Options;

namespace MilanServer;

/// <summary>
/// The Galaxy at War stats settings (Milan:Stats:Galaxy), applied live from config/milan.json.
/// A bad value is ignored with a warning and the last good one stays in effect (the default at startup).
/// </summary>
public sealed class GalaxySettings : IDisposable
{
    public sealed record Values(bool Enabled, int CacheSeconds, string Url, int FullPopulation, double MaxBaseTint)
    {
        /// <summary>
        /// /v1/info advertises this as galaxyStatusUrl - set to null when this is not enabled
        /// </summary>
        public string? AdvertisedUrl => Enabled ? (Url.Length > 0 ? Url : Api.GalaxyStatsPath) : null;
    }

    public const int MinCacheSeconds = 30;
    public const int MaxCacheSeconds = 3600;
    public const int MaxUrlLength = 512;

    static readonly MilanGalaxyStatsOptions Defaults = new();
    public static readonly Values DefaultValues = new(Defaults.Enabled, Defaults.CacheSeconds, Defaults.Url, Defaults.FullPopulation, Defaults.MaxBaseTint);

    readonly ILogger<GalaxySettings> _logger;
    readonly IDisposable? _subscription;
    volatile Values _current;

    public GalaxySettings(IOptionsMonitor<MilanLiveOptions> options, ILogger<GalaxySettings> logger)
    {
        _logger = logger;
        _current = Build(options.CurrentValue.Stats.Galaxy, DefaultValues, logger);
        Describe(_current);
        _subscription = options.OnChange(next =>
        {
            Values built = Build(next.Stats.Galaxy, _current, logger);
            // File watchers often report one save twice
            if (built == _current)
            {
                return;
            }

            _current = built;
            Describe(built);
        });
    }

    public void Dispose() => _subscription?.Dispose();

    public Values Current => _current;

    static Values Build(MilanGalaxyStatsOptions options, Values previous, ILogger logger)
    {
        int cacheSeconds = options.CacheSeconds;
        if (cacheSeconds is < MinCacheSeconds or > MaxCacheSeconds)
        {
            logger.LogWarning("Ignored Stats:Galaxy:CacheSeconds: {Value} must be between {Min} and {Max}, keeping {Kept}.",  cacheSeconds, MinCacheSeconds, MaxCacheSeconds, previous.CacheSeconds);
            cacheSeconds = previous.CacheSeconds;
        }

        string url = (options.Url ?? "").Trim();
        if (!IsValidUrl(url))
        {
            logger.LogWarning("Ignored Stats:Galaxy:Url '{Value}': it must be empty, a path starting with '/', or an absolute http(s) URL, without a query or fragment, keeping '{Kept}'.",
                url, previous.Url);
            url = previous.Url;
        }

        int fullPopulation = options.FullPopulation;
        if (fullPopulation < 0)
        {
            logger.LogWarning("Ignored Stats:Galaxy:FullPopulation {Value}: must not be negative, keeping {Kept}.", fullPopulation, previous.FullPopulation);
            fullPopulation = previous.FullPopulation;
        }

        double maxBaseTint = options.MaxBaseTint;
        if (maxBaseTint is < 0 or > 1 || double.IsNaN(maxBaseTint))
        {
            logger.LogWarning("Ignored Stats:Galaxy:MaxBaseTint {Value}: it must be between 0 and 1, keeping {Kept}.", maxBaseTint, previous.MaxBaseTint);
            maxBaseTint = previous.MaxBaseTint;
        }

        return new Values(options.Enabled, cacheSeconds, url, fullPopulation, maxBaseTint);
    }

    /// <summary>
    /// Empty, a path on this server, or an absolute http(s) URL. Clients append "/{gameVersion}"
    /// Clientswill refuse an http URL when the matchmaker is https.
    /// </summary>
    public static bool IsValidUrl(string url)
    {
        if (url.Length == 0)
        {
            return true;
        }

        if (url.Length > MaxUrlLength || url.Any(c => c is <= ' ' or >= '\x7F' or '?' or '#'))
        {
            return false;
        }

        if (url.StartsWith('/'))
        {
            return !url.StartsWith("//", StringComparison.Ordinal);
        }

        return Uri.TryCreate(url, UriKind.Absolute, out Uri? uri) && (uri.Scheme == Uri.UriSchemeHttp || uri.Scheme == Uri.UriSchemeHttps) && uri.Host.Length > 0;
    }

    void Describe(Values values)
    {
        if (!values.Enabled)
        {
            _logger.LogInformation("Galaxy at War stats are off.");
            return;
        }

        _logger.LogInformation("Galaxy at War stats at {Url}, recounted every {CacheSeconds}s; tint {Tint}.",
            values.AdvertisedUrl, values.CacheSeconds,
            values.FullPopulation > 0 ? $"full population {values.FullPopulation}, max base {values.MaxBaseTint}" : "off");
    }
}
