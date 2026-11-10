using Microsoft.Extensions.Configuration.Json;

namespace MilanServer;

/// <summary>
/// Class for having a live-config file that can be changed without a server restart
/// </summary>
public sealed class LiveConfigFile : JsonConfigurationSource
{
    public const string RelativePath = "config/milan.json";

    // Last error for parsing, null if last read succeeded
    public string? LastError { get; private set; }

    public override IConfigurationProvider Build(IConfigurationBuilder builder)
    {
        EnsureDefaults(builder);
        return new Provider(this);
    }

    sealed class Provider(LiveConfigFile source) : JsonConfigurationProvider(source)
    {
        // Clear error when parse succeeds
        public override void Load(Stream stream)
        {
            try
            {
                base.Load(stream);
                source.LastError = null;
            }
            catch (FormatException ex)
            {
                source.LastError = ex.InnerException is { } inner ? $"{ex.Message} {inner.Message}" : ex.Message;
            }
        }
    }
}
