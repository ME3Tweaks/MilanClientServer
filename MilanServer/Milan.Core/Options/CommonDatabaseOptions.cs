namespace Milan.Core.Options;

public sealed class CommonDatabaseOptions
{
    public string Host { get; set; } = "localhost";
    public ushort Port { get; set; } = 3306;
    public string Database { get; set; } = "milan";
    public string User { get; set; }  = "milan";
    public string Password { get; set; } = "milan";
}
