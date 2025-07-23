using Milan.Core.Payload;

namespace Milan.Core.Messages.Redirector;


/// <summary>
/// Response to a <see cref="QueryRequest"/>.
/// </summary>
public struct QueryResponse
{
    public Address Match;
    public Address Stun;
    public Address Turn;

    public void Read(ref JsonObjectReader reader)
    {
        reader.GetObjectWith("match", Match.Read);
        reader.GetObjectWith("stun", Stun.Read);
        reader.GetObjectWith("turn", Turn.Read);
    }

    public readonly void Write(ref JsonObjectWriter writer)
    {
        writer.SetObjectWith("match", Match.Write);
        writer.SetObjectWith("stun", Stun.Write);
        writer.SetObjectWith("turn", Turn.Write);
    }
}
