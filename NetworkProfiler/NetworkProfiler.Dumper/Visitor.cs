using NetworkProfiler.Core;
namespace NetworkProfiler.Dumper;

internal class LoggingVisitor(NetworkCapture owner) : ICaptureVisitor
{
    public NetworkCapture Owner { get; init; } = owner;


    public void OnActorReplication(ref ActorReplicationRecord record, int frame, float time)
    {
        
    }

    public void OnControlMessage(ref ControlMessageRecord record, int frame, float time)
    {
        Console.WriteLine($" - control message '{record.MsgType}' ({Owner.ResolveString(record.Params)}) " +
            $"received by {record.Recipient} from {record.Remote}");
    }

    public void OnFrameStart(ref FrameStartRecord record, int frame, float time)
    {
        //Console.WriteLine($"==== FRAME START == #{frame} == {time} ms ====");
    }

    public void OnLevelStart(ref LevelStartRecord record, int frame, float time)
    {

    }

    public void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time)
    {

    }

    public void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time)
    {
        // ...
    }

    public void OnSendBunch(ref SendBunchRecord record, int frame, float time)
    {

    }

    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time)
    {
        Console.WriteLine($" - session event: '{Owner.ResolveString(record.Event)}' " +
            $"({Owner.ResolveString(record.Description)}), remote = {record.Remote}");
    }

    public void OnSocketCall(ref SocketCallRecord record, int frame, float time)
    {

    }

    public void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time)
    {

    }
}
