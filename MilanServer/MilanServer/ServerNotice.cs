using System.Security.Cryptography;
using System.Text;
using Microsoft.Extensions.Options;

namespace MilanServer;

/// <summary>
/// MOTD handler
/// </summary>
public sealed class ServerNotice : IDisposable
{
    public const string FallbackLanguage = "INT";
    public const int MaxTextLength = 1000;

    sealed record Snapshot(string ExpectedGameVersion, IReadOnlySet<string> Blocked, string Id,
        IReadOnlyDictionary<string, string> Text, string Signature);

    readonly ILogger<ServerNotice> _logger;
    readonly LiveConfigFile? _file;
    readonly IDisposable? _subscription;
    volatile Snapshot _current;
    string? _reportedFileError;

    public ServerNotice(IOptionsMonitor<MilanLiveOptions> options, ILogger<ServerNotice> logger, LiveConfigFile? file = null)
    {
        _logger = logger;
        _file = file;
        ReportFileError();
        _current = Build(options.CurrentValue, logger);
        Describe(_current);
        _subscription = options.OnChange(next =>
        {
            ReportFileError();
            Snapshot built = Build(next, logger);
            // File watchers often report one save twice.
            if (built.Signature == _current.Signature)
            {
                return;
            }

            _current = built;
            Describe(built);
        });
    }

    public void Dispose() => _subscription?.Dispose();

    public string ExpectedGameVersion => _current.ExpectedGameVersion;

    /// <summary>
    /// Whether the version may not host, search, or redeem invites (BlockedGameVersions)
    /// </summary>
    public bool IsBlocked(string gameVersion) => _current.Blocked.Contains(gameVersion);

    /// <summary>
    /// Get inofrmation for the client
    /// </summary>
    /// <param name="gameVersion"></param>
    /// <param name="language"></param>
    /// <returns></returns>
    public (string? LatestGameVersion, bool? Outdated, NoticeText? Notice) For(string? gameVersion, string? language)
    {
        Snapshot current = _current;
        string? latest = current.ExpectedGameVersion.Length > 0 ? current.ExpectedGameVersion : null;
        bool? outdated = null;
        if (latest != null && gameVersion != null && MatchmakingStore.ValidateGameVersion(gameVersion) == null)
        {
            // A blocked version must update, whatever its number says.
            if (current.Blocked.Contains(gameVersion))
            {
                outdated = true;
            }
            else if (CompareVersions(gameVersion, latest) is int order)
            {
                outdated = order < 0;
            }
        }

        NoticeText? notice = null;
        string key = NormalizeLanguage(language) ?? FallbackLanguage;
        if (current.Text.TryGetValue(key, out string? text) || current.Text.TryGetValue(FallbackLanguage, out text))
        {
            notice = new NoticeText(current.Id, text);
        }
        return (latest, outdated, notice);
    }

    /// <summary>
    /// Compares version strings
    /// </summary>
    /// <param name="a"></param>
    /// <param name="b"></param>
    /// <returns></returns>
    public static int? CompareVersions(string a, string b)
    {
        List<int>? left = NumericParts(a), right = NumericParts(b);
        if (left is null || right is null)
        {
            return null;
        }

        for (int i = 0; i < Math.Max(left.Count, right.Count); i++)
        {
            int x = i < left.Count ? left[i] : 0, y = i < right.Count ? right[i] : 0;
            if (x != y)
            {
                return x < y ? -1 : 1;
            }
        }
        return 0;
    }

    static List<int>? NumericParts(string version)
    {
        List<int> parts = [];
        foreach (string part in version.Split('.'))
        {
            if (part.Length is 0 or > 9 || !part.All(char.IsAsciiDigit))
            {
                break;
            }

            parts.Add(int.Parse(part, System.Globalization.CultureInfo.InvariantCulture));
        }
        return parts.Count > 0 ? parts : null;
    }

    static string? NormalizeLanguage(string? language)
    {
        if (string.IsNullOrEmpty(language) || language.Length > 8 || !language.All(char.IsAsciiLetter))
        {
            return null;
        }

        return language.ToUpperInvariant();
    }

    static Snapshot Build(MilanLiveOptions options, ILogger logger)
    {
        string expected = options.ExpectedGameVersion.Trim();
        if (expected.Length > 0 && MatchmakingStore.ValidateGameVersion(expected) is not null)
        {
            logger.LogWarning("Ignored ExpectedGameVersion '{Version}': it must be printable ASCII without spaces, at most 64 characters.", expected);
            expected = "";
        }

        SortedSet<string> blocked = new(StringComparer.Ordinal);
        foreach (string? raw in options.BlockedGameVersions)
        {
            string version = (raw ?? "").Trim();
            if (MatchmakingStore.ValidateGameVersion(version) is not null)
            {
                logger.LogWarning("Invalid game version presented '{Version}', discarding", version);
                continue;
            }
            blocked.Add(version);
        }
        if (expected.Length > 0 && blocked.Contains(expected))
        {
            // Sanity Check
            logger.LogWarning("ExpectedGameVersion '{Version}' is also in BlockedGameVersions, so the current version cannot use matchmaking.", expected);
        }

        SortedDictionary<string, string> texts = new(StringComparer.Ordinal);
        foreach ((string rawLanguage, string? rawText) in options.Notice.Text)
        {
            string? language = NormalizeLanguage(rawLanguage);
            if (language is null)
            {
                logger.LogWarning("Ignored the Notice text for '{Language}': the key must be a game language such as INT or DEU.", rawLanguage);
                continue;
            }
            string text = CleanText(rawText ?? "");
            if (text.Length > MaxTextLength)
            {
                logger.LogWarning("Ignored the {Language} Notice text: it has {Length} characters, more than {Max}.", language, text.Length, MaxTextLength);
                continue;
            }
            if (text.Length > 0)
            {
                texts[language] = text;
            }
        }

        StringBuilder all = new();
        foreach ((string language, string text) in texts)
        {
            all.Append(language).Append('=').Append(text).Append('\0');
        }

        string textHash = Convert.ToHexStringLower(SHA256.HashData(Encoding.UTF8.GetBytes(all.ToString())))[..16];

        string id = options.Notice.Id.Trim();
        if (id.Length > 0 && MatchmakingStore.ValidateGameVersion(id) is not null)
        {
            logger.LogWarning("Ignored the Notice Id '{Id}': it must be printable ASCII without spaces, at most 64 characters.", id);
            id = "";
        }
        if (id.Length == 0)
        {
            id = textHash;
        }

        return new Snapshot(expected, blocked, id, texts, $"{expected}|{string.Join(',', blocked)}|{id}|{textHash}");
    }

    // Plain text for a message box: "\n" (as typed in an environment variable) and real line
    // breaks become line breaks, and other control characters are dropped.
    static string CleanText(string text)
    {
        StringBuilder clean = new(text.Length);
        string normalized = text.Replace("\r\n", "\n").Replace("\\n", "\n");
        foreach (char c in normalized)
        {
            if (c == '\n' || !char.IsControl(c))
            {
                clean.Append(c);
            }
        }
        return clean.ToString().Trim();
    }

    void ReportFileError()
    {
        string? error = _file?.LastError;
        if (error is not null && error != _reportedFileError)
        {
            _logger.LogWarning("Ignored the latest {Path}, which does not parse; the last version that did stays in effect. {Error}", LiveConfigFile.RelativePath, error);
        }
        _reportedFileError = error;
    }

    void Describe(Snapshot snapshot)
    {
        _logger.LogInformation("Current game version {Version}; blocked versions {Blocked}; message of the day {Notice}.",
            snapshot.ExpectedGameVersion.Length > 0 ? snapshot.ExpectedGameVersion : "not set",
            snapshot.Blocked.Count > 0 ? string.Join(", ", snapshot.Blocked) : "none",
            snapshot.Text.Count > 0 ? $"{snapshot.Id} in {string.Join(", ", snapshot.Text.Keys)}" : "none");
    }
}
