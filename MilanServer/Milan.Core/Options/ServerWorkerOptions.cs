namespace Milan.Core.Options;

public sealed class ServerWorkerOptions
{
    public string Ip { get; set; } = "0.0.0.0";
    public ushort Port { get; set; } = 23000;
    public long MaxActiveConnections { get; set; }
    public bool EnableSecureConnection { get; set; }
}
