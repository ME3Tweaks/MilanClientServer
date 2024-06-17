using CommunityToolkit.Mvvm.ComponentModel;
using NetworkProfiler.Core;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Net;

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

#endregion


/// <summary>
/// Parsed network capture ready for providing general overview.
/// </summary>
public class LoadedCapture : IDisposable, ICaptureVisitor
{
    readonly NetworkCapture capture;

    public readonly List<string> RemoteAddresses = [];
    readonly HashSet<IPEndPoint> remoteAddressHashset = [];

    public readonly List<ActorReplicationCategory> ActorReplicationCategories = [];
    readonly Dictionary<string, int> actorReplicationLookup = [];
    int lastActorReplicationIndex = -1;

    public readonly List<RemoteCallCategory> RemoteCallCategories = [];
    readonly Dictionary<(string, string), int> remoteCallLookup = [];

    public readonly List<ControlMessageEntry> ControlMessageEntries = [];


    public LoadedCapture(NetworkCapture source)
    {
        capture = source;
        capture.AcceptVisitor(this);
        FinalizeCategories();
    }

    private void FinalizeCategories()
    {
        foreach (var actorReplCategory in ActorReplicationCategories)
        {
            actorReplCategory.Properties.Sort((a, b) => b.AccumBits.CompareTo(a.AccumBits));
            actorReplCategory.AccumBits = actorReplCategory.Properties.Select(prop => prop.AccumBits).Sum();
        }
        ActorReplicationCategories.Sort((a, b) => b.AccumBits.CompareTo(a.AccumBits));

        RemoteCallCategories.Sort((a, b) => b.AccumBits.CompareTo(a.AccumBits));

        var sortedAddresses = remoteAddressHashset.ToList().Select(ip => ip.ToString()).Order().ToList();
        RemoteAddresses.AddRange(sortedAddresses);
    }


    #region ICaptureVisitor implementation.

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
    }
    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time)
    {
        remoteAddressHashset.Add(record.Remote);
    }
    public void OnSocketCall(ref SocketCallRecord record, int frame, float time)
    {
        remoteAddressHashset.Add(record.Remote);
    }
    public void OnSendBunch(ref SendBunchRecord record, int frame, float time)
    {
        remoteAddressHashset.Add(record.Remote);
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
