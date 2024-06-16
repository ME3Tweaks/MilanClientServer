using System.Net;

namespace NetworkProfiler.Core;


/// <summary>
/// Valid types of serialized records.
/// </summary>
public enum RecordType : byte
{
    TieredStringLookup = 10 + 1,
    FrameStart,
    LevelStart,
    RemoteFunction,
    ActorReplication,
    PropertyReplication,
    ControlMessage,
    SessionEvent,
    SocketCall,
    SendBunch,
}


/// <summary>
/// Valid types of control messages.
/// </summary>
public enum ControlMessageType : byte
{
    Hello = 0,
    Welcome = 1,
    Upgrade = 2,
    Challenge = 3,
    Netspeed = 4,
    Login = 5,
    Failure = 6,
    Uses = 7,
    Have = 8,
    Join = 9,
    DlMngr = 11,
    Abort = 13,
    Unload = 14,
    Unk19_DLM = 19,
    Unk21_VIS = 21,
}

public enum ControlMessageHandler : byte
{
    World,
    NetPendingLevel,
}

public enum SocketDirection : byte
{
    Sending,
    Receiving,
}


public readonly struct StringReference(int index)
{
    public readonly int Index = index;
}

public readonly struct NameReference(int index, int number)
{
    public readonly int Index = index;
    public readonly int Number = number;
}


public interface ICaptureReader
{
    bool Skipping { get; }

    RecordType ReadRecordType();
    bool ReadBool();
    short ReadInt16();
    int ReadInt32();
    long ReadInt64();
    float ReadFloat();
    IPEndPoint ReadRemote();
    StringReference ReadString();
    NameReference ReadName();
    NameReference ReadObject();
    void ReadBlob(Span<byte> buffer);
}
