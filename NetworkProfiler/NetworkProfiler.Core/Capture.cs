using System.Diagnostics;
using System.Net;
using System.Text;

namespace NetworkProfiler.Core;


internal record struct FrameEntry(long Offset, float Time);

public class BadCaptureFormatException(string reason)
    : Exception($"invalid capture format: {reason}")
{
    public string Reason { get; set; } = reason;
}

public class NetworkCapture : IDisposable, ICaptureReader
{
    readonly BinaryReader       reader;
    readonly List<string>       stringTable = [];
    readonly List<FrameEntry>   frameTable = [];
    readonly bool               skipping = false;

    public long FrameCount => frameTable.Count;
    public long RecordCount { get; private set; } = 0;

    public const int HEADER_SIZE = 64;
    public const int SUPPORTED_VERSION = 1; 

    public NetworkCapture(Stream stream)
    {
        Debug.Assert(stream.CanRead && stream.CanSeek);
        reader = new BinaryReader(stream, Encoding.Default, leaveOpen: false);
        skipping = true;

        var magicBytes = reader.ReadBytes(8);
        var magicString = Encoding.ASCII.GetString(magicBytes);
        if (magicString != "MLNNTPRF")
            throw new BadCaptureFormatException("wrong magic");

        var magicVersion = reader.ReadInt32();
        if (magicVersion != SUPPORTED_VERSION)
            throw new BadCaptureFormatException("unsupported version");

        reader.BaseStream.Seek(HEADER_SIZE, SeekOrigin.Begin);

        InitializingVisitor visitor = new();
        AcceptVisitor(visitor);
        RecordCount = visitor.VisitedCount;

        stringTable = visitor.OutputStrings;
        frameTable = visitor.OutputFrames;

        skipping = false;
    }

    public string ResolveString(StringReference reference)
    {
        if (reference.Index < 0 || reference.Index >= stringTable.Count)
            throw new ArgumentException("string reference out of bounds", nameof(reference));

        return stringTable[reference.Index] ?? string.Empty;
    }

    public string ResolveString(NameReference reference)
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
            int num => $"{stringTable[reference.Index]}_{num-1}",
        };
    }

    public delegate bool VisitorPredicate(RecordType type, int frame, float time);

    public void AcceptVisitor(ICaptureVisitor visitor)
    {
        AcceptVisitorInternal(
            visitor, HEADER_SIZE, 0, 0,
            (type, frame, time) => true,
            (type, frame, time) => false
        );
    }

    public void AcceptVisitorSingleFrame(ICaptureVisitor visitor, int frameIndex)
    {
        if (frameIndex < 0 || frameIndex >= frameTable.Count)
            throw new ArgumentException("index out of bounds", nameof(frameIndex));

        var (offset, time) = frameTable[frameIndex];
        AcceptVisitorInternal(
            visitor, offset, frameIndex, time,
            (type, frame, time) => frame == frameIndex,
            (type, frame, time) => false
        );
    }

    private void AcceptVisitorInternal(ICaptureVisitor visitor,
        long startOffset, int startFrame, float startTime,
        VisitorPredicate callPredicate, VisitorPredicate breakPredicate)
    {
        int counter = 0;
        int frameIndex = startFrame - 1;
        float timerTotal = startTime;

        reader.BaseStream.Seek(startOffset, SeekOrigin.Begin);
        while (reader.BaseStream.Position < reader.BaseStream.Length)
        {
            counter += 1;
            var recordPosition = reader.BaseStream.Position;

            RecordType type = ReadRecordType();
            switch (type)
            {
                case RecordType.TieredStringLookup:
                    {
                        var record = ParseRecord<TieredStringLookupRecord>();
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnTieredStringLookup(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.FrameStart:
                    {
                        var record = ParseRecord<FrameStartRecord>();
                        frameIndex += 1;
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnFrameStart(ref record, frameIndex, timerTotal, recordPosition);
                        timerTotal += record.Delta;
                        break;
                    }
                case RecordType.LevelStart:
                    {
                        var record = ParseRecord<LevelStartRecord>();
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnLevelStart(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.RemoteFunction:
                    {
                        var record = ParseRecord<RemoteFunctionRecord>();
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnRemoteFunction(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.ActorReplication:
                    {
                        var record = ParseRecord<ActorReplicationRecord>();
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnActorReplication(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.PropertyReplication:
                    {
                        var record = ParseRecord<PropertyReplicationRecord>();
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnPropertyReplication(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.ControlMessage:
                    {
                        var record = ParseRecord<ControlMessageRecord>();
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnControlMessage(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.SessionEvent:
                    {
                        var record = ParseRecord<SessionEventRecord>();
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnSessionEvent(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.SocketCall:
                    {
                        var record = ParseRecord<SocketCallRecord>();
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnSocketCall(ref record, frameIndex, timerTotal);
                        break;
                    }
                case RecordType.SendBunch:
                    {
                        var record = ParseRecord<SendBunchRecord>();
                        if (callPredicate(type, frameIndex, timerTotal))
                            visitor.OnSendBunch(ref record, frameIndex, timerTotal);
                        break;
                    }
            }

            if (breakPredicate(type, frameIndex, timerTotal))
                break;
        }

        visitor.VisitedCount = counter;
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

    public bool Skipping => skipping;

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
    int VisitedCount { get; set; }

    void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time);
    void OnFrameStart(ref FrameStartRecord record, int frame, float time, long recordPosition);
    void OnLevelStart(ref LevelStartRecord record, int frame, float time);
    void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time);
    void OnActorReplication(ref ActorReplicationRecord record, int frame, float time);
    void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time);
    void OnControlMessage(ref ControlMessageRecord record, int frame, float time);
    void OnSessionEvent(ref SessionEventRecord record, int frame, float time);
    void OnSocketCall(ref SocketCallRecord record, int frame, float time);
    void OnSendBunch(ref SendBunchRecord record, int frame, float time);
}

internal class InitializingVisitor : ICaptureVisitor
{
    public List<string> OutputStrings = [];
    public List<FrameEntry> OutputFrames = [];

    public int VisitedCount { get; set; } = 0;

    public void OnActorReplication(ref ActorReplicationRecord record, int frame, float time) { }
    public void OnControlMessage(ref ControlMessageRecord record, int frame, float time) { }
    public void OnLevelStart(ref LevelStartRecord record, int frame, float time) { }
    public void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time) { }
    public void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time) { }
    public void OnSendBunch(ref SendBunchRecord record, int frame, float time) { }
    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time) { }
    public void OnSocketCall(ref SocketCallRecord record, int frame, float time) { }

    public void OnFrameStart(ref FrameStartRecord record, int frame, float time, long offset)
    {
        Debug.Assert(frame == OutputFrames.Count);
        OutputFrames.Add(new FrameEntry(offset, time));
    }

    public void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time)
    {
        Debug.WriteLine($"adding tiered string lookup record with {record.Table.Length} item(s) at frame {frame}");
        OutputStrings.AddRange(record.Table);
    }
}
