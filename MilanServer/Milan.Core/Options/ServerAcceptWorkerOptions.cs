namespace Milan.Core.Options;

public sealed class ServerAcceptWorkerOptions
{
    public string Address { get; set; } = "127.0.0.1:23000";
    public long MaxActiveConnections { get; set; } = 0;
    public bool EnableSecureConnection { get; set; } = false;
}
