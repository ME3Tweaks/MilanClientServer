using NetworkProfiler.Core;

namespace NetworkProfiler.Types;

public class ActorReplicationCategory
{
    public string ActorName { get; init; } = string.Empty;
    public List<PropertyReplicationCategory> Properties { get; init; } = [];
    public int Count { get; set; } = 0;

    internal bool bNonEmpty = false;
    public int NonEmptyCount { get; set; } = 0;

    public int AccumBits { get; set; } = 0;
    public float AccumBytes => (float)AccumBits / 8;

    public float AvgBits => NonEmptyCount > 0 ? (float)AccumBits / NonEmptyCount : 0;
    public float AvgBytes => AvgBits / 8;
}

public class PropertyReplicationCategory
{
    public string PropertyName { get; init; } = string.Empty;
    public int Count { get; set; } = 0;

    public int AccumBits { get; set; } = 0;
    public float AccumBytes => (float)AccumBits / 8;

    public float AvgBits => (float)AccumBits / Count;
    public float AvgBytes => AvgBits / 8;
}

public class RemoteCallCategory
{
    public string ObjectName { get; init; } = string.Empty;
    public string FunctionName { get; init; } = string.Empty;
    public int Count { get; set; } = 0;

    public string JointName => $"{ObjectName}.{FunctionName}";

    public int AccumBits { get; set; } = 0;
    public float AccumBytes => (float)AccumBits / 8;

    public float AvgBits => (float)AccumBits / Count;
    public float AvgBytes => AvgBits / 8;
}

public class ControlMessageEntry
{
    public ControlMessageType MessageType { get; set; }
    public ControlMessageHandler Recipient { get; set; }
    public string Params { get; set; } = string.Empty;
    public string Remote { get; set; } = string.Empty;
    public int FrameIndex { get; set; }
    public float FrameTime { get; set; }
}

public class LevelStartEntry
{
    public int FrameIndex { get; set; }
    public float FrameTime { get; set; }
}

public class SessionEventEntry
{
    public string Event { get; set; } = string.Empty;
    public string Description { get; set; } = string.Empty;
    public string Remote { get; set; } = string.Empty;
    public int FrameIndex { get; set; }
    public float FrameTime { get; set; }
}


/// <summary>
/// A "struct-of-arrays" wrapper for per-frame statistics.
/// </summary>
public class FrameStatistics(long count)
{
    public int[] ActorCount { get; private set; } = new int[count];
    public int[] PropertyCount { get; private set; } = new int[count];
    public float[] PropertySize { get; private set; } = new float[count];

    public int[] RemoteCallCount { get; private set; } = new int[count];
    public float[] RemoteCallSize { get; private set; } = new float[count];

    public int[] SendBunchCount { get; private set; } = new int[count];
    public float[] SendBunchSize { get; private set; } = new float[count];

    public int[] SocketSendCount { get; private set; } = new int[count];
    public float[] SocketSendSize { get; private set; } = new float[count];

    public int[] SocketRecvCount { get; private set; } = new int[count];
    public float[] SocketRecvSize { get; private set; } = new float[count];

    public int[] ControlMessageCount { get; private set; } = new int[count];


    public FrameStatistics(FrameStatistics source, long index) : this(1)
    {
        ActorCount[0] = source.ActorCount[index];
        PropertyCount[0] = source.PropertyCount[index];
        PropertySize[0] = source.PropertySize[index];
        RemoteCallCount[0] = source.RemoteCallCount[index];
        RemoteCallSize[0] = source.RemoteCallSize[index];
        SendBunchCount[0] = source.SendBunchCount[index];
        SendBunchSize[0] = source.SendBunchSize[index];
        SocketSendCount[0] = source.SocketSendCount[index];
        SocketSendSize[0] = source.SocketSendSize[index];
        SocketRecvCount[0] = source.SocketRecvCount[index];
        SocketRecvSize[0] = source.SocketRecvSize[index];
        ControlMessageCount[0] = source.ControlMessageCount[index];

    }
}
