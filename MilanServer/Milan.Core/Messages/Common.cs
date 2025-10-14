using Milan.Core.Payload;
using System.Net;

namespace Milan.Core.Messages;

/// <summary>
/// An IPv4 address.
/// </summary>
public record struct Address(uint Ip, ushort Port)
{
    public Address() : this(0, 0) {}

    public void Read(ref JsonObjectReader reader)
    {
        Ip = (uint)reader.GetInt("ip");
        Port = (ushort)reader.GetInt("port");
    }

    public readonly void Write(ref JsonObjectWriter writer)
    {
        writer.SetInt("ip", (int)Ip);
        writer.SetInt("ip", (int)Port);
    }

    public readonly override string ToString() => $"{new IPAddress(Ip)}:{Port}";
}

/// <summary>
/// A game/server version.
/// </summary>
public record struct Version(int Major, int Minor, int Patch)
{
    public void Read(ref JsonObjectReader reader)
    {
        Major = reader.GetInt("major");
        Minor = reader.GetInt("minor");
        Patch = reader.GetInt("patch");
    }

    public readonly void Write(ref JsonObjectWriter writer)
    {
        writer.SetInt("major", Major);
        writer.SetInt("minor", Minor);
        writer.SetInt("patch", Patch);
    }
    public readonly override string ToString() => $"{Major}.{Minor}.{Patch}";
}
