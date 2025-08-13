namespace Milan.Core.Options;

public sealed class ServerWorkerOptions
{
    public uint MaxConnections { get; set; }
    public bool EnableSecureConnection { get; set; }
    public string SecureCertificate { get; set; } = string.Empty;
}
