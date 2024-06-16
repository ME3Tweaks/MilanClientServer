using NetworkProfiler.Core;
using NetworkProfiler.Types;

namespace NetworkProfiler.Analysis;

public class InitialAnalysisVisitor : ICaptureVisitor
{
    public LoadedCapture CaptureRef { get; set; }

    public void OnActorReplication(ref ActorReplicationRecord record, int frame, float time)
    {
        throw new NotImplementedException();
    }

    public void OnControlMessage(ref ControlMessageRecord record, int frame, float time)
    {
        throw new NotImplementedException();
    }

    public void OnFrameStart(ref FrameStartRecord record, int frame, float time, long recordPosition)
    {
        throw new NotImplementedException();
    }

    public void OnLevelStart(ref LevelStartRecord record, int frame, float time)
    {
        throw new NotImplementedException();
    }

    public void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time)
    {
        throw new NotImplementedException();
    }

    public void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time)
    {
        throw new NotImplementedException();
    }

    public void OnSendBunch(ref SendBunchRecord record, int frame, float time)
    {
        throw new NotImplementedException();
    }

    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time)
    {
        throw new NotImplementedException();
    }

    public void OnSocketCall(ref SocketCallRecord record, int frame, float time)
    {
        throw new NotImplementedException();
    }

    public void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time)
    {
        throw new NotImplementedException();
    }
}
