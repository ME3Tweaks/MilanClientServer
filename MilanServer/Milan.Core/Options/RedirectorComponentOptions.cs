namespace Milan.Core.Options;

public sealed class RedirectorComponentOptions
{
    public string Matchmaking { get; set; } = string.Empty;
    public string STUN { get; set; } = string.Empty;
    public string TURN { get; set; } = string.Empty;
}
