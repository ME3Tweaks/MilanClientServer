using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using NetworkProfiler.Core;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Net;
using System.Text;
using System.Windows.Input;

namespace NetworkProfiler.Types;


/// <summary>
/// Parsed network capture ready for providing general overview.
/// <para/>
/// Note that this type implements <see cref="ICaptureVisitor"/> and uses that interface to do
/// a one-time load of everything it wants to extract from the provided <see cref="NetworkCapture"/>.
/// This extraction is strictly one-time only. All secondary analysis actions must run in dedicated
/// fields to avoid contaminating this type's state with arbitrary results.
/// </summary>
public class LoadedCapture : IDisposable, ICaptureVisitor
{
    internal readonly NetworkCapture capture;

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
    public readonly List<LevelStartEntry> LevelStartEntries = [];
    public readonly List<SessionEventEntry> SessionEventEntries = [];


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

    public void OnLevelStart(ref LevelStartRecord record, int frame, float time)
    {
        LevelStartEntries.Add(new LevelStartEntry()
        {
            FrameIndex = frame,
            FrameTime = time,
        });
    }

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

        SessionEventEntries.Add(new SessionEventEntry()
        {
            Event = capture.ResolveString(record.Event),
            Description = capture.ResolveString(record.Description),
            Remote = record.Remote?.ToString() ?? "n/a",
            FrameIndex = frame,
            FrameTime = time,
        });
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

    public string TotalSummary
    {
        get
        {
            StringBuilder summary = new();

            summary.AppendLine($"Frame count: {capture.FrameCount}");
            summary.AppendLine($"Record count: {capture.RecordCount}");
            summary.AppendLine();

            summary.AppendLine($"Remote addresses: {capture.RemoteAddresses.Count}");
            foreach (var address in capture.RemoteAddresses)
            {
                summary.AppendLine($"    - {address}");
            }
            summary.AppendLine();

            if (capture.FrameStats is not null)
                AppendFrameStatsSummary(summary, capture.FrameStats);

            return summary.ToString();
        }
    }

    public string FrameSummary
    {
        get
        {
            StringBuilder summary = new();

            if (CurrentFrame is not null)
            {
                summary.AppendLine($"Frame index: {CurrentFrame.Index}");
                summary.AppendLine();
                AppendFrameStatsSummary(summary, CurrentFrame.Stats);
            }

            return summary.ToString();
        }
    }

    void AppendFrameStatsSummary(StringBuilder builder, FrameStatistics stats)
    {
        builder.AppendLine($"Actor count: {stats.ActorCount.Sum()}");
        builder.AppendLine($"Property count: {stats.PropertyCount.Sum()}");
        builder.AppendLine($"Property size: {stats.PropertySize.Sum()}");
        builder.AppendLine($"RPC count: {stats.RemoteCallCount.Sum()}");
        builder.AppendLine($"RPC size: {stats.RemoteCallSize.Sum()}");
        builder.AppendLine($"SendBunch count: {stats.SendBunchCount.Sum()}");
        builder.AppendLine($"SendBunch size: {stats.SendBunchSize.Sum()}");
        builder.AppendLine($"Control message count: {stats.ControlMessageCount.Sum()}");
        builder.AppendLine();
    }

    public LoadedFrame? CurrentFrame { get; private set; } = null;

    public string LogQuery
    {
        get => _logQuery;
        set
        {
            _logQuery = value;
            OnPropertyChanged();
            SelectFrame(CurrentFrame!.Index);
        }
    }
    string _logQuery = string.Empty;

    public IEnumerable<string> RemoteAddresses => capture.RemoteAddresses;
    public ObservableCollection<ActorReplicationCategory> ReplicatedActors { get; private set; }
    public ObservableCollection<RemoteCallCategory> RemoteCalls { get; private set; }
    public ObservableCollection<ControlMessageEntry> ControlMessages { get; private set; }

    public IEnumerable<SessionEventEntry> SessionEvents => capture.SessionEventEntries;
    public IEnumerable<LevelStartEntry> LevelStarts => capture.LevelStartEntries;

    public List<int> SessionEventMarkersX { get; } = [];
    public List<int> SessionEventMarkersY { get; private set; } = [];

    public List<int> LevelStartMarkersX { get; } = [];
    public List<int> LevelStartMarkersY { get; private set; } = [];

    public ActorReplicationFilter ActorReplicationFilter { get; private set; }
    public RemoteCallFilter RemoteCallFilter { get; private set; }
    public ControlMessageFilter ControlMessageFilter { get; private set; }

    public LoadedCaptureViewModel(LoadedCapture model)
    {
        capture = model;
        BuildMarkerCoordinates();

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

        SelectFrame(0);
    }

    void BuildMarkerCoordinates()
    {
        HashSet<int> sessionEventFrameHash = [];
        foreach (var sessionEvent in SessionEvents)
        {
            int frameIndex = sessionEvent.FrameIndex;
            if (sessionEventFrameHash.Contains(frameIndex))
                continue;

            sessionEventFrameHash.Add(frameIndex);
            SessionEventMarkersX.Add(frameIndex);
        }
        SessionEventMarkersY = Enumerable.Repeat(0, SessionEventMarkersX.Count).ToList();

        HashSet<int> levelStartFrameHash = [];
        foreach (var levelStart in LevelStarts)
        {
            int frameIndex = levelStart.FrameIndex;
            if (levelStartFrameHash.Contains(frameIndex))
                continue;

            levelStartFrameHash.Add(frameIndex);
            LevelStartMarkersX.Add(frameIndex);
        }
        LevelStartMarkersY = Enumerable.Repeat(0, LevelStartMarkersX.Count).ToList();
    }

    #region Frame ops.

    public void FirstFrame()
    {
        SelectFrame(0);
    }

    public bool IsFirstFrameAvailable()
        => true;

    public void PreviousFrame()
    {
        var currentIndex = CurrentFrame?.Index ?? -1;
        var prevIndex = Math.Clamp(currentIndex - 1, 0, (int)FrameCount - 1);
        if (prevIndex != currentIndex)
            SelectFrame(prevIndex);
    }

    public bool IsPreviousFrameAvailable()
        => CurrentFrame?.Index > 0;

    public void NextFrame()
    {
        var currentIndex = CurrentFrame?.Index ?? -1;
        var nextIndex = Math.Clamp(currentIndex + 1, 0, (int)FrameCount - 1);
        if (nextIndex != currentIndex)
            SelectFrame(nextIndex);
    }

    public bool IsNextFrameAvailable()
        => CurrentFrame?.Index < FrameCount - 1;

    public void SelectFrame(int index)
    {
        if (index < 0 || index >= FrameCount)
            return;

        CurrentFrame = new(capture, index, LogQuery);
        OnPropertyChanged(nameof(CurrentFrame));
        OnPropertyChanged(nameof(FrameSummary));
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
