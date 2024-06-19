using System.Diagnostics;
using System.Text;
using NetworkProfiler.Core;

namespace NetworkProfiler.Types;


public class LoadedFrame : ICaptureVisitor
{
    public int Index { get; }
    public FrameStatistics Stats { get; }
    public int RecordCount { get; private set; } = 0;

    public LoadedFrame(LoadedCapture source, int frameIndex)
    {
        Index = frameIndex;
        Stats = new (source.FrameStats!, frameIndex);

        source.capture.AcceptVisitorSingleFrame(this, frameIndex);
    }


    #region ICaptureVisitor implementation.

    public int VisitedCount { get; set; } = 0;

    public void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }
    public void OnFrameStart(ref FrameStartRecord record, int frame, float time, long recordPosition)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }

    public void OnLevelStart(ref LevelStartRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }

    public void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }

    public void OnActorReplication(ref ActorReplicationRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }

    public void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }

    public void OnControlMessage(ref ControlMessageRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }

    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }

    public void OnSocketCall(ref SocketCallRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }

    public void OnSendBunch(ref SendBunchRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;
    }

    #endregion
}
