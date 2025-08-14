namespace Milan.Core.Options;

public sealed class RedirectorOptions
{
    public RedirectorOptionsAddress Matchmaking { get; set; } = new();
    public RedirectorOptionsAddress STUN { get; set; } = new();
    public RedirectorOptionsAddress TURN { get; set; } = new();
}

public sealed class RedirectorOptionsAddress
{
    public string Ip { get; set; } = string.Empty;
    public ushort Port { get; set; } = ushort.MinValue;
}
