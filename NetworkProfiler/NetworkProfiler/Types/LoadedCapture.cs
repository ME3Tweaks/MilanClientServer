using CommunityToolkit.Mvvm.ComponentModel;
using NetworkProfiler.Core;
using System.Diagnostics;

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

    public float AvgBits => (float)AccumBits / NonEmptyCount;
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


/// <summary>
/// Parsed network capture ready for providing general overview.
/// </summary>
public class LoadedCapture : IDisposable, ICaptureVisitor
{
    readonly NetworkCapture capture;

    public readonly List<ActorReplicationCategory> ActorReplicationCategories = [];
    readonly Dictionary<string, int> actorReplicationLookup = [];
    int lastActorReplicationIndex = -1;


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
    }


    #region ICaptureVisitor implementation.

    public void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time) { }
    public void OnFrameStart(ref FrameStartRecord record, int frame, float time, long recordPosition) { }
    public void OnLevelStart(ref LevelStartRecord record, int frame, float time) { }
    public void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time) { }
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
    }
    public void OnControlMessage(ref ControlMessageRecord record, int frame, float time) { }
    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time) { }
    public void OnSocketCall(ref SocketCallRecord record, int frame, float time) { }
    public void OnSendBunch(ref SendBunchRecord record, int frame, float time) { }

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

public class LoadedCaptureViewModel(LoadedCapture model) : ObservableObject, IDisposable
{
    private readonly LoadedCapture capture = model;

    public IEnumerable<ActorReplicationCategory> ReplicatedActors => capture.ActorReplicationCategories;

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
