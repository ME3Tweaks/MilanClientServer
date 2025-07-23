using Milan.Core.Payload;

namespace Milan.Core.Messages.Redirector;

/// <summary>
/// Sent to retrieve main servers' connection data for a game session.
/// </summary>
/// <see cref="QueryResponse"/>
public struct QueryRequest
{
    public Version HostVersion;
    public Version ContentVersion;
    public int GameIndex;

    public void Read(ref JsonObjectReader reader)
    {
        reader.GetObjectWith("host_version", HostVersion.Read);
        reader.GetObjectWith("content_version", ContentVersion.Read);
        GameIndex = reader.GetInt("game");
    }

    public readonly void Write(ref JsonObjectWriter writer)
    {
        writer.SetObjectWith("host_version", HostVersion.Write);
        writer.SetObjectWith("content_version", ContentVersion.Write);
        writer.SetInt("game", GameIndex);
    }
}

