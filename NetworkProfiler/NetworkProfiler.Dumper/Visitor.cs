using NetworkProfiler.Core;
namespace NetworkProfiler.Dumper;

internal class LoggingVisitor(NetworkCapture owner) : ICaptureVisitor
{
    public NetworkCapture Owner { get; init; } = owner;

    public void OnControlMessage(ref ControlMessageRecord record, int frame, float time)
    {
        Console.WriteLine($"control message '{record.MsgType}' ({Owner.ResolveString(record.Params)}) " +
            $"received by {record.Recipient} from {record.Remote}");
    }

    public void OnFrameStart(ref FrameStartRecord record, int frame, float time, long recordPosition)
    {
        Console.WriteLine($"==== FRAME START == #{frame} == {time} ms == {recordPosition} byte(s) ====");
    }

    public void OnLevelStart(ref LevelStartRecord record, int frame, float time)
    {
        Console.WriteLine($"level start, host {Owner.ResolveString(record.Host)}, map {Owner.ResolveString(record.Map)}");
    }

    public void OnActorReplication(ref ActorReplicationRecord record, int frame, float time)
    {
        Console.WriteLine($"actor replicated {Owner.ResolveName(record.Actor)} to {record.Remote}");
    }

    public void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time)
    {
        Console.WriteLine($"  - property {Owner.ResolveName(record.Property)} in {record.Bits} bit(s)");
    }

    public void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time)
    {
        Console.WriteLine($"rpc call to {Owner.ResolveName(record.Actor)}.{Owner.ResolveName(record.Function)} in {record.Bits} bit(s)");
    }

    public void OnSendBunch(ref SendBunchRecord record, int frame, float time)
    {
        Console.WriteLine($"send bunch of {record.Bits} bit(s) on {Owner.ResolveName(record.Channel)} to {record.Remote}");
    }

    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time)
    {
        Console.WriteLine($"session event: '{Owner.ResolveString(record.Event)}' " +
            $"({Owner.ResolveString(record.Description)}), remote = {record.Remote}");
    }

    public void OnSocketCall(ref SocketCallRecord record, int frame, float time)
    {
        var method = record.Direction == SocketDirection.Receiving ? "RecvFrom" : "SendTo";
        Console.WriteLine($"FSocketWin::{method} {record.Bits} bit(s) to {record.Remote}");
    }

    public void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time)
    {

    }
}
