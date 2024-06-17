using System.Diagnostics;
using System.Net;
using System.Text;

namespace NetworkProfiler.Core;


public interface IRecord
{
    RecordType Type { get; }
    void Read(ICaptureReader reader);
}


public struct TieredStringLookupRecord : IRecord
{
    public string[] Table;

    public readonly RecordType Type => RecordType.TieredStringLookup;
    public void Read(ICaptureReader reader)
    {
        Span<byte> buffer = stackalloc byte[2048];

        int count = reader.ReadInt32();
        Table = new string[count + 1];

        for (int i = 0; i <= count; ++i)
        {
            short length = reader.ReadInt16();
            Debug.Assert(length >= 0);

            length *= 2;
            if (length == 0)
                continue;

            Debug.Assert(length < 2048);
            var bytes = buffer[..length];

            reader.ReadBlob(bytes);
            var str = Encoding.Unicode.GetString(bytes);

            Table[i] = str;
        }
    }
}

public struct FrameStartRecord : IRecord
{
    public float Delta;

    public readonly RecordType Type => RecordType.FrameStart;
    public void Read(ICaptureReader reader)
    {
        Delta = reader.ReadFloat();
    }
}

public struct LevelStartRecord : IRecord
{
    public StringReference Protocol;
    public StringReference Host;
    public StringReference Map;
    public StringReference Portal;
    public StringReference[] Ops;
    public int Port;

    public readonly RecordType Type => RecordType.LevelStart;
    public void Read(ICaptureReader reader)
    {
        Protocol = reader.ReadString();
        Host = reader.ReadString();
        Map = reader.ReadString();
        Portal = reader.ReadString();
        Port = reader.ReadInt32();

        int count = reader.ReadInt32();
        Ops = new StringReference[count];

        for (int i = 0; i < count; ++i)
        {
            Ops[i] = reader.ReadString();
        }
    }
}

public struct RemoteFunctionRecord : IRecord
{
    public IPEndPoint Remote;
    public NameReference Actor;
    public NameReference Function;
    public int Bits;

    public readonly RecordType Type => RecordType.RemoteFunction;
    public void Read(ICaptureReader reader)
    {
        Remote = reader.ReadRemote();
        Actor = reader.ReadObject();
        Function = reader.ReadObject();
        Bits = reader.ReadInt32();
    }
}

public struct ActorReplicationRecord : IRecord
{
    public IPEndPoint Remote;
    public NameReference Actor;

    public readonly RecordType Type => RecordType.ActorReplication;
    public void Read(ICaptureReader reader)
    {
        Remote = reader.ReadRemote();
        Actor = reader.ReadObject();
    }
}

public struct PropertyReplicationRecord : IRecord
{
    public IPEndPoint Remote;
    public NameReference Property;
    public int Bits;

    public readonly RecordType Type => RecordType.PropertyReplication;
    public void Read(ICaptureReader reader)
    {
        Remote = reader.ReadRemote();
        Property = reader.ReadObject();
        Bits = reader.ReadInt32();
    }
}

public struct ControlMessageRecord : IRecord
{
    public IPEndPoint? Remote;
    public ControlMessageHandler Recipient;
    public ControlMessageType MsgType;
    public StringReference Params;

    public readonly RecordType Type => RecordType.ControlMessage;
    public void Read(ICaptureReader reader)
    {
        bool hasRemote = reader.ReadBool();
        if (hasRemote)
            Remote = reader.ReadRemote();

        Recipient = reader.ReadBool()
            ? ControlMessageHandler.NetPendingLevel
            : ControlMessageHandler.World;

        int MsgTypeInt = reader.ReadInt32();
        MsgType = (ControlMessageType)MsgTypeInt;

        Params = reader.ReadString();

        //Debug.WriteLine($"control message - {MsgType} / {Recipient} - read");
    }
}

public struct SessionEventRecord : IRecord
{
    public IPEndPoint Remote;
    public StringReference Event;
    public StringReference Description;

    public readonly RecordType Type => RecordType.SessionEvent;
    public void Read(ICaptureReader reader)
    {
        Remote = reader.ReadRemote();
        Event = reader.ReadString();
        Description = reader.ReadString();
    }
}

public struct SocketCallRecord : IRecord
{
    public IPEndPoint Remote;
    public SocketDirection Direction;
    public int Bits;

    public readonly RecordType Type => RecordType.SocketCall;
    public void Read(ICaptureReader reader)
    {
        Remote = reader.ReadRemote();
        Direction = reader.ReadBool() ? SocketDirection.Sending : SocketDirection.Receiving;
        Bits = reader.ReadInt32();
    }
}

public struct SendBunchRecord : IRecord
{
    public IPEndPoint Remote;
    public NameReference Channel;
    public int Bits;

    public readonly RecordType Type => RecordType.SendBunch;
    public void Read(ICaptureReader reader)
    {
        Remote = reader.ReadRemote();
        Channel = reader.ReadObject();
        Bits = reader.ReadInt32();
    }
}
