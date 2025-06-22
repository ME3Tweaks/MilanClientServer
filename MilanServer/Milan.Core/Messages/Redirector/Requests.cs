using Milan.Core.Payload;

namespace Milan.Core.Messages.Redirector;

/// <summary>
/// Sent to retrieve main servers' connection data for a game session.
/// </summary>
/// <see cref="QueryResponse"/>
public ref struct QueryRequest
{
    public record struct Version(int Major, int Minor, int Patch);

    public Version HostVersion;
    public Version ContentVersion;
    public int GameIndex;

    public void Read(ref JsonObjectReader reader)
    {
        Version hostVersion = new();
        Version contentVersion = new();

        reader.GetObjectWith("host_version", (ref reader) =>
        {
            hostVersion.Major = reader.GetInt("major");
            hostVersion.Minor = reader.GetInt("minor");
            hostVersion.Patch = reader.GetInt("patch");
        });

        reader.GetObjectWith("content_version", (ref reader) =>
        {
            contentVersion.Major = reader.GetInt("major");
            contentVersion.Minor = reader.GetInt("minor");
            contentVersion.Patch = reader.GetInt("patch");
        });

        HostVersion = hostVersion;
        ContentVersion = contentVersion;

        GameIndex = reader.GetInt("game");
    }

    public readonly void Write(ref JsonObjectWriter writer)
    {
        Version hostVersion = HostVersion;
        Version contentVersion = ContentVersion;

        writer.SetObjectWith("host_version", (ref writer) =>
        {
            writer.SetInt("major", hostVersion.Major);
            writer.SetInt("minor", hostVersion.Minor);
            writer.SetInt("patch", hostVersion.Patch);
        });

        writer.SetObjectWith("content_version", (ref writer) =>
        {
            writer.SetInt("major", contentVersion.Major);
            writer.SetInt("minor", contentVersion.Minor);
            writer.SetInt("patch", contentVersion.Patch);
        });

        writer.SetInt("game", GameIndex);
    }
}

