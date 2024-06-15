using System.Diagnostics;
using System.Net;
using System.Text;

namespace NetworkProfiler.Core;


public class NetworkCapture : IDisposable, ICaptureReader
{
    readonly BinaryReader   reader;
    readonly List<string>   stringTable = [];
    readonly bool           skipping = false;

    public NetworkCapture(Stream stream)
    {
        Debug.Assert(stream.CanRead && stream.CanSeek);
        reader = new BinaryReader(stream, Encoding.Default, leaveOpen: false);
        skipping = true;

        StringConsolidatingVisitor visitor = new();
        AcceptVisitor(visitor);
        stringTable = visitor.OutputTable;

        skipping = false;
    }

    public string ResolveString(StringReference reference)
    {
        if (reference.Index < 0 || reference.Index >= stringTable.Count)
            throw new ArgumentException("string reference out of bounds", nameof(reference));

        return stringTable[reference.Index];
    }

    public string ResolveName(NameReference reference)
    {
        if (reference.Index < 0 || reference.Index >= stringTable.Count)
            throw new ArgumentException("string reference out of bounds", nameof(reference));

        if (reference.Number < 0)
            throw new ArgumentException("invalid name number", nameof(reference));

        return reference.Number switch
        {
            0 => stringTable[reference.Index],
            int num => $"{stringTable[reference.Index]}_{num}",
        };
    }

    public delegate bool VisitorPredicate(RecordType type, int frame, float time);

    public void AcceptVisitor(ICaptureVisitor visitor, VisitorPredicate predicate)
    {
        int counter = -1;
        int frameIndex = 0;
        float timerTotal = 0;

        reader.BaseStream.Seek(0, SeekOrigin.Begin);
        while (reader.BaseStream.Position < reader.BaseStream.Length)
        {
            counter += 1;
            RecordType type = ReadRecordType();

            //Console.WriteLine($"record #{counter}, type {type}, frame {frameIndex}, time {timerTotal}");

            // This is very verbose but avoids a lot of allocations.
            switch (type)
            {
                case RecordType.TieredStringLookup:
                    {
                        var record = ParseRecord<TieredStringLookupRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnTieredStringLookup(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.FrameStart:
                    {
                        var record = ParseRecord<FrameStartRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnFrameStart(ref record, frameIndex, timerTotal);
                        frameIndex += 1;
                        timerTotal += record.Delta;
                        break;
                    }
                case RecordType.LevelStart:
                    {
                        var record = ParseRecord<LevelStartRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnLevelStart(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.RemoteFunction:
                    {
                        var record = ParseRecord<RemoteFunctionRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnRemoteFunction(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.ActorReplication:
                    {
                        var record = ParseRecord<ActorReplicationRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnActorReplication(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.PropertyReplication:
                    {
                        var record = ParseRecord<PropertyReplicationRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnPropertyReplication(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.ControlMessage:
                    {
                        var record = ParseRecord<ControlMessageRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnControlMessage(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.SessionEvent:
                    {
                        var record = ParseRecord<SessionEventRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnSessionEvent(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.SocketCall:
                    {
                        var record = ParseRecord<SocketCallRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnSocketCall(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.SendBunch:
                    {
                        var record = ParseRecord<SendBunchRecord>();
                        if (predicate(type, frameIndex, timerTotal))
                            visitor.OnSendBunch(ref record, frameIndex, timerTotal);
                        break;
                    }
            }
        }
    }

    public void AcceptVisitor(ICaptureVisitor visitor)
    {
        AcceptVisitor(visitor, (type, frame, time) => true);
    }

    private T ParseRecord<T>() where T : IRecord, new()
    {
        T record = new();
        record.Read(this);
        return record;
    }

    #region IDisposable implementation.

    private bool disposedValue;

    protected virtual void Dispose(bool disposing)
    {
        if (!disposedValue)
        {
            if (disposing)
            {
                // TODO: dispose managed state (managed objects)
                reader.Dispose();
            }

            // TODO: set large fields to null
            disposedValue = true;
        }
    }

    public void Dispose()
    {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    #endregion

    #region ICaptureReader implementation.

    public RecordType ReadRecordType()
    {
        byte value = reader.ReadByte();
        if ((value & 0x80) == 0)
            throw new InvalidDataException();

        unchecked { value &= (byte)~0x80; }
        return Enum.IsDefined(typeof(RecordType), value) switch
        {
            true => (RecordType)value,
            false => throw new InvalidDataException(),
        };
    }

    public bool ReadBool()
    {
        byte value = reader.ReadByte();
        return value switch
        {
            0 => false, 1 => true,
            _ => throw new InvalidDataException(),
        };
    }

    public short ReadInt16()
    {
        long value = ReadInt64();
        if (value >= short.MinValue && value <= short.MaxValue)
            return (short)value;
        throw new InvalidDataException();
    }

    public int ReadInt32()
    {
        long value = ReadInt64();
        if (value >= int.MinValue && value <= int.MaxValue)
            return (int)value;
        throw new InvalidDataException();
    }

    public long ReadInt64()
    {
        long value = reader.Read7BitEncodedInt64();
        return value;
    }

    public float ReadFloat()
    {
        long value = ReadInt64();
        return (float)value / 1000;
    }

    public StringReference ReadString()
    {
        var index = ReadInt32();
        return new StringReference(index);
    }

    public NameReference ReadName()
    {
        var str = ReadString();
        var number = ReadInt32();
        return new (str.Index, number);
    }

    public NameReference ReadObject()
    {
        return ReadName();
    }

    public IPEndPoint ReadRemote()
    {
        var reference = ReadString();

        if (skipping)
            return new IPEndPoint(0, 0);

        var address = ResolveString(reference);
        return IPEndPoint.Parse(address);
    }

    public void ReadBlob(Span<byte> buffer)
    {
        int count = reader.Read(buffer);
        if (count != buffer.Length)
            throw new EndOfStreamException();
    }

    #endregion
}


public interface ICaptureVisitor
{
    void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time);
    void OnFrameStart(ref FrameStartRecord record, int frame, float time);
    void OnLevelStart(ref LevelStartRecord record, int frame, float time);
    void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time);
    void OnActorReplication(ref ActorReplicationRecord record, int frame, float time);
    void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time);
    void OnControlMessage(ref ControlMessageRecord record, int frame, float time);
    void OnSessionEvent(ref SessionEventRecord record, int frame, float time);
    void OnSocketCall(ref SocketCallRecord record, int frame, float time);
    void OnSendBunch(ref SendBunchRecord record, int frame, float time);
}

class StringConsolidatingVisitor : ICaptureVisitor
{
    public List<string> OutputTable = new();

    public void OnActorReplication(ref ActorReplicationRecord record, int frame, float time) { }
    public void OnControlMessage(ref ControlMessageRecord record, int frame, float time) { }
    public void OnFrameStart(ref FrameStartRecord record, int frame, float time) { }
    public void OnLevelStart(ref LevelStartRecord record, int frame, float time) { }
    public void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time) { }
    public void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time) { }
    public void OnSendBunch(ref SendBunchRecord record, int frame, float time) { }
    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time) { }
    public void OnSocketCall(ref SocketCallRecord record, int frame, float time) { }

    public void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time)
    {
        Debug.WriteLine($"adding tiered string lookup record with {record.Table.Length} item(s) at frame {frame}");
        OutputTable.AddRange(record.Table);
    }
}
