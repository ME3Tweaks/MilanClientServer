using System.Net;

namespace NetworkProfiler.Core;


public readonly struct StringReference
{
    public readonly int Index;
}

public readonly struct NameReference
{
    public readonly int Index;
    public readonly int Number;
}


public interface ICaptureReader
{
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
    void ReadBlob(short length, Span<byte> buffer);
}
