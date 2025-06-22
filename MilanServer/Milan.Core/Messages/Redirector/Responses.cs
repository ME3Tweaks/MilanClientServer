using Milan.Core.Payload;

namespace Milan.Core.Messages.Redirector;


/// <summary>
/// Response to a <see cref="QueryRequest"/>.
/// </summary>
public ref struct QueryResponse
{
    public record struct Address(uint Ip, ushort Port);

    public Address Match;
    public Address Stun;
    public Address Turn;

    public void Read(ref JsonObjectReader reader)
    {
        Address match = new();
        Address stun = new();
        Address turn = new();

        reader.GetObjectWith("match", (ref reader) =>
        {
            match.Ip = (uint)reader.GetInt("ip");
            match.Port = (ushort)reader.GetInt("port");
        });

        reader.GetObjectWith("stun", (ref reader) =>
        {
            stun.Ip = (uint)reader.GetInt("ip");
            stun.Port = (ushort)reader.GetInt("port");
        });

        reader.GetObjectWith("turn", (ref reader) =>
        {
            turn.Ip = (uint)reader.GetInt("ip");
            turn.Port = (ushort)reader.GetInt("port");
        });

        Match = match;
        Stun = stun;
        Turn = turn;
    }

    public readonly void Write(ref JsonObjectWriter writer)
    {
        Address match = Match;
        Address stun = Stun;
        Address turn = Turn;

        writer.SetObjectWith("match", (ref writer) =>
        {
            writer.SetInt("ip", (int)match.Ip);
            writer.SetInt("port", (int)match.Port);
        });

        writer.SetObjectWith("stun", (ref writer) =>
        {
            writer.SetInt("ip", (int)stun.Ip);
            writer.SetInt("port", (int)stun.Port);
        });

        writer.SetObjectWith("turn", (ref writer) =>
        {
            writer.SetInt("ip", (int)turn.Ip);
            writer.SetInt("port", (int)turn.Port);
        });
    }
}
