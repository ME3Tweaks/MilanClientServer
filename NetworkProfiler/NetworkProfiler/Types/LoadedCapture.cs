using CommunityToolkit.Mvvm.ComponentModel;
using NetworkProfiler.Core;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Net;
using System.Text.RegularExpressions;

namespace NetworkProfiler.Types;


#region Small data classes shared between model and viewmodel.

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

/// <summary>
/// A "struct-of-arrays" wrapper for per-frame statistics.
/// </summary>
public class FrameStatistics(long count)
{
    public short[] ActorCount { get; private set; } = new short[count];
    public short[] PropertyCount { get; private set; } = new short[count];
    public float[] PropertySize { get; private set; } = new float[count];

    public short[] RemoteCallCount { get; private set; } = new short[count];
    public float[] RemoteCallSize { get; private set; } = new float[count];

    public short[] SendBunchCount {  get; private set; } = new short[count];
    public float[] SendBunchSize { get; private set; } = new float[count];

    public short[] SocketSendCount { get; private set; } = new short[count];
    public float[] SocketSendSize { get; private set; } = new float[count];

    public short[] SocketRecvCount { get; private set; } = new short[count];
    public float[] SocketRecvSize { get; private set; } = new float[count];

    public short[] ControlMessageCount { get; private set; } = new short[count];

}

#endregion


/// <summary>
/// Parsed network capture ready for providing general overview.
/// </summary>
public class LoadedCapture : IDisposable, ICaptureVisitor
{
    readonly NetworkCapture capture;

    public long FrameCount => capture.FrameCount;
    public long RecordCount => capture.RecordCount;

    public readonly List<string> RemoteAddresses = [];
    readonly HashSet<IPEndPoint> remoteAddressHashset = [];

    public FrameStatistics? FrameStats { get; private set; } = null;

    public readonly List<ActorReplicationCategory> ActorReplicationCategories = [];
    readonly Dictionary<string, int> actorReplicationLookup = [];
    int lastActorReplicationIndex = -1;

    public readonly List<RemoteCallCategory> RemoteCallCategories = [];
    readonly Dictionary<(string, string), int> remoteCallLookup = [];

    public readonly List<ControlMessageEntry> ControlMessageEntries = [];


    public LoadedCapture(NetworkCapture source)
    {
        FrameStats = new(source.FrameCount);

        capture = source;
        capture.AcceptVisitor(this);

        FinalizeCollection();
    }

    private void FinalizeCollection()
    {
        // Sort properties and actors by total size (descending),
        // calculating the total actors' send size from their properties.
        foreach (var actorReplCategory in ActorReplicationCategories)
        {
            actorReplCategory.Properties.Sort((a, b) => b.AccumBits.CompareTo(a.AccumBits));
            actorReplCategory.AccumBits = actorReplCategory.Properties.Select(prop => prop.AccumBits).Sum();
        }
        ActorReplicationCategories.Sort((a, b) => b.AccumBits.CompareTo(a.AccumBits));

        // Sort remote proc calls by total size.
        RemoteCallCategories.Sort((a, b) => b.AccumBits.CompareTo(a.AccumBits));

        // Sort remote addresses.
        var sortedAddresses = remoteAddressHashset.ToList().Select(ip => ip.ToString()).Order().ToList();
        RemoteAddresses.AddRange(sortedAddresses);

        // Transform statistics' size fields from bytes to bits.
        if (FrameStats is not null)
        {
            for (int i = 0; i < FrameStats.PropertySize.Length; i++)
                FrameStats.PropertySize[i] /= 8;

            for (int i = 0; i < FrameStats.RemoteCallSize.Length; i++)
                FrameStats.RemoteCallSize[i] /= 8;

            for (int i = 0; i < FrameStats.SendBunchSize.Length; i++)
                FrameStats.SendBunchSize[i] /= 8;

            for (int i = 0; i < FrameStats.SocketSendSize.Length; i++)
                FrameStats.SocketSendSize[i] /= 8;

            for (int i = 0; i < FrameStats.SocketRecvSize.Length; i++)
                FrameStats.SocketRecvSize[i] /= 8;
        }
    }


    #region ICaptureVisitor implementation.

    public int VisitedCount { get; set; } = 0;

    public void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time) { }
    public void OnFrameStart(ref FrameStartRecord record, int frame, float time, long recordPosition) { }
    public void OnLevelStart(ref LevelStartRecord record, int frame, float time) { }
    public void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time)
    {
        var objectName = capture.ResolveString(record.Actor);
        var functionName = capture.ResolveString(record.Function);

        if (!remoteCallLookup.TryGetValue((objectName, functionName), out int remoteCallIndex))
        {
            remoteCallIndex = RemoteCallCategories.Count;
            RemoteCallCategories.Add(new () { ObjectName = objectName, FunctionName = functionName });
            remoteCallLookup[(objectName, functionName)] = remoteCallIndex;
        }

        RemoteCallCategory remoteCallCategory = RemoteCallCategories[remoteCallIndex];

        remoteCallCategory.Count += 1;
        remoteCallCategory.AccumBits += record.Bits;

        remoteAddressHashset.Add(record.Remote);

        if (FrameStats != null)
        {
            FrameStats.RemoteCallCount[frame] += 1;
            FrameStats.RemoteCallSize[frame] += record.Bits;
        }
    }
    public void OnActorReplication(ref ActorReplicationRecord record, int frame, float time)
    {
        if (lastActorReplicationIndex != -1)
        {
            ActorReplicationCategory prevActorReplicationCategory = ActorReplicationCategories[lastActorReplicationIndex];
            if (prevActorReplicationCategory.bNonEmpty)
            {
                prevActorReplicationCategory.NonEmptyCount++;
                prevActorReplicationCategory.bNonEmpty = false;
            }
        }

        var actorName = capture.ResolveString(record.Actor);

        if (!actorReplicationLookup.TryGetValue(actorName, out int actorReplIndex))
        {
            actorReplIndex = ActorReplicationCategories.Count;
            ActorReplicationCategories.Add(new() { ActorName = actorName });
            actorReplicationLookup[actorName] = actorReplIndex;
        }

        ActorReplicationCategory actorReplCategory = ActorReplicationCategories[actorReplIndex];

        actorReplCategory.bNonEmpty = false;
        actorReplCategory.Count += 1;
        lastActorReplicationIndex = actorReplIndex;

        remoteAddressHashset.Add(record.Remote);

        if (FrameStats != null)
        {
            FrameStats.ActorCount[frame] += 1;
        }
    }
    public void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time)
    {
        if (lastActorReplicationIndex == -1)
        {
            Debug.WriteLine($"skipping property replication at frame {frame}");
            return;
        }

        var propertyName = capture.ResolveString(record.Property);

        ActorReplicationCategory actorCategory = ActorReplicationCategories[lastActorReplicationIndex];
        actorCategory.bNonEmpty = true;

        int propertyCategoryIndex = actorCategory.Properties
            .FindIndex((propRecord) => propRecord.PropertyName == propertyName);
        if (propertyCategoryIndex == -1)
        {
            propertyCategoryIndex = actorCategory.Properties.Count;
            actorCategory.Properties.Add(new() { PropertyName = propertyName });
        }

        PropertyReplicationCategory propertyCategory = actorCategory.Properties[propertyCategoryIndex];

        propertyCategory.AccumBits += record.Bits;
        propertyCategory.Count += 1;

        remoteAddressHashset.Add(record.Remote);

        if (FrameStats != null)
        {
            FrameStats.PropertyCount[frame] += 1;
            FrameStats.PropertySize[frame] += record.Bits;
        }
    }
    public void OnControlMessage(ref ControlMessageRecord record, int frame, float time)
    {
        var paramString = capture.ResolveString(record.Params);
        //Debug.WriteLine($">> CONTROL MESSAGE {record.MsgType}: {paramString}");

        ControlMessageEntries.Add(new()
        {
            MessageType = record.MsgType,
            Recipient = record.Recipient,
            Params = paramString,
            Remote = record.Remote!.ToString(),
            FrameIndex = frame,
            FrameTime = time,
        });

        remoteAddressHashset.Add(record.Remote);

        if (FrameStats != null)
        {
            FrameStats.ControlMessageCount[frame] += 1;
        }
    }
    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time)
    {
        remoteAddressHashset.Add(record.Remote);
    }
    public void OnSocketCall(ref SocketCallRecord record, int frame, float time)
    {
        remoteAddressHashset.Add(record.Remote);

        if (FrameStats != null)
        {
            if (record.Direction == SocketDirection.Sending)
            {
                FrameStats.SocketSendCount[frame] += 1;
                FrameStats.SocketSendSize[frame] += record.Bits;
            }
            else if (record.Direction == SocketDirection.Receiving)
            {
                FrameStats.SocketRecvCount[frame] += 1;
                FrameStats.SocketRecvSize[frame] += record.Bits;
            }
        }
    }
    public void OnSendBunch(ref SendBunchRecord record, int frame, float time)
    {
        remoteAddressHashset.Add(record.Remote);

        if (frame >= 390 && frame <= 392)
        {
            Debug.WriteLine($"SendBunch at frame {frame} has {record.Bits} bit(s)");
            //Debugger.Break();
        }

        if (FrameStats != null)
        {
            FrameStats.SendBunchCount[frame] += 1;
            FrameStats.SendBunchSize[frame] += record.Bits;
        }
    }

    #endregion

    #region IDisposable implementation.

    private bool disposedValue;
    protected virtual void Dispose(bool disposing)
    {
        if (!disposedValue)
        {
            if (disposing)
            {
                capture.Dispose();
            }

            disposedValue = true;
        }
    }
    public void Dispose()
    {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    #endregion
}


/// <summary>
/// View model for <see cref="LoadedCapture"/>.
/// </summary>
public class LoadedCaptureViewModel : ObservableObject, IDisposable
{
    private readonly LoadedCapture capture;

    public long FrameCount => capture.FrameCount;
    public long RecordCount => capture.RecordCount;

    public FrameStatistics FrameStats => capture.FrameStats!;

    public IEnumerable<string> RemoteAddresses => capture.RemoteAddresses;
    public ObservableCollection<ActorReplicationCategory> ReplicatedActors { get; private set; }
    public ObservableCollection<RemoteCallCategory> RemoteCalls { get; private set; }
    public ObservableCollection<ControlMessageEntry> ControlMessages { get; private set; }

    public ActorReplicationFilter ActorReplicationFilter { get; private set; }
    public RemoteCallFilter RemoteCallFilter { get; private set; }
    public ControlMessageFilter ControlMessageFilter { get; private set; }

    public LoadedCaptureViewModel(LoadedCapture model)
    {
        capture = model;

        ReplicatedActors = new ObservableCollection<ActorReplicationCategory>(capture.ActorReplicationCategories);
        RemoteCalls = new ObservableCollection<RemoteCallCategory>(capture.RemoteCallCategories);
        ControlMessages = new ObservableCollection<ControlMessageEntry>(capture.ControlMessageEntries);

        ActorReplicationFilter = new ActorReplicationFilter((filter, restore) =>
        {
            var filtered = capture.ActorReplicationCategories.Where(e => filter.Apply(ref e));
            ReplicatedActors = new ObservableCollection<ActorReplicationCategory>(filtered);
            OnPropertyChanged(nameof(ReplicatedActors));
        });
        RemoteCallFilter = new RemoteCallFilter((filter, restore) =>
        {
            var filtered = capture.RemoteCallCategories.Where(e => filter.Apply(ref e));
            RemoteCalls = new ObservableCollection<RemoteCallCategory>(filtered);
            OnPropertyChanged(nameof(RemoteCalls));
        });
        ControlMessageFilter = new ControlMessageFilter((filter, restore) =>
        {
            var filtered = capture.ControlMessageEntries.Where(e => filter.Apply(ref e));
            ControlMessages = new ObservableCollection<ControlMessageEntry>(filtered);
            OnPropertyChanged(nameof(ControlMessages));
        });
    }

    #region IDisposable implementation.

    private bool disposedValue;

    protected virtual void Dispose(bool disposing)
    {
        if (!disposedValue)
        {
            if (disposing)
            {
                capture.Dispose();
            }

            disposedValue = true;
        }
    }
    public void Dispose()
    {
        Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    #endregion
}
