using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using NetworkProfiler.Core;

namespace NetworkProfiler.Types;


public class LoadedFrame : ICaptureVisitor
{
    readonly NetworkCapture capture;

    public int Index { get; }
    public FrameStatistics Stats { get; }
    public int RecordCount { get; private set; } = 0;

    public string Log { get; }
    readonly StringBuilder _log = new();


    public LoadedFrame(LoadedCapture source, int frameIndex)
    {
        capture = source.capture;

        Index = frameIndex;
        Stats = new (source.FrameStats!, frameIndex);

        source.capture.AcceptVisitorSingleFrame(this, frameIndex);
        Log = _log.ToString();
    }


    #region Log string generation.

    void BeginEntry(string header)
    {
        _log.AppendLine(header.ToUpper());
    }

    const int PARAM_COLUMN_LEN = 20;

    void WriteParam(string key, string value)
    {
        _log.Append("   ");
        _log.Append(key);
        _log.Append(":  ");
        if (key.Length < PARAM_COLUMN_LEN)
            _log.Append(' ', PARAM_COLUMN_LEN - key.Length);
        _log.Append(value);
        _log.AppendLine();
    }

    void EndEntry()
    {
        _log.AppendLine();
    }

    #endregion


    #region ICaptureVisitor implementation.

    public int VisitedCount { get; set; } = 0;

    public void OnTieredStringLookup(ref TieredStringLookupRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        BeginEntry($"Tiered string lookup");
        WriteParam("Count", record.Table.Length.ToString());
        for (int i = 0; i < record.Table.Length; i++)
            WriteParam($"Entry #{i}", record.Table[i]);
        EndEntry();
    }
    public void OnFrameStart(ref FrameStartRecord record, int frame, float time, long recordPosition)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        BeginEntry($"Frame start");
        WriteParam("Index", frame.ToString());
        WriteParam("Time", time.ToString());
        EndEntry();
    }

    public void OnLevelStart(ref LevelStartRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        BeginEntry($"Level start");
        WriteParam("Map", capture.ResolveString(record.Map));
        WriteParam("Portal", capture.ResolveString(record.Portal));
        WriteParam("Host", capture.ResolveString(record.Host));
        WriteParam("Port", record.Port.ToString());
        WriteParam("Protocol", capture.ResolveString(record.Protocol));

        for (int i = 0; i < record.Ops.Length; i++)
            WriteParam($"Op {i}", capture.ResolveString(record.Ops[i]));

        EndEntry();
    }

    public void OnRemoteFunction(ref RemoteFunctionRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        BeginEntry("Remote call");
        WriteParam("Object", capture.ResolveName(record.Actor));
        WriteParam("Function", capture.ResolveName(record.Function));
        WriteParam("Bits", record.Bits.ToString());
        WriteParam("Remote", record.Remote?.ToString() ?? "n/a");
        EndEntry();
    }

    public void OnActorReplication(ref ActorReplicationRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        BeginEntry("Replicate actor");
        WriteParam("Actor", capture.ResolveName(record.Actor));
        WriteParam("Remote", record.Remote?.ToString() ?? "n/a");
        EndEntry();
    }

    public void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        BeginEntry("Replicate property");
        WriteParam("Property", capture.ResolveName(record.Property));
        WriteParam("Bits", record.Bits.ToString());
        WriteParam("Remote", record.Remote?.ToString() ?? "n/a");
        EndEntry();
    }

    public void OnControlMessage(ref ControlMessageRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        BeginEntry("Control message");
        WriteParam("Type", record.MsgType.ToString());
        WriteParam("Handler", record.Recipient.ToString());
        WriteParam("Parameters", capture.ResolveString(record.Params));
        WriteParam("Remote", record.Remote?.ToString() ?? "n/a");
        EndEntry();
    }

    public void OnSessionEvent(ref SessionEventRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        BeginEntry("Session event");
        WriteParam("Event", capture.ResolveString(record.Event));
        WriteParam("Description", capture.ResolveString(record.Description));
        WriteParam("Remote", record.Remote?.ToString() ?? "n/a");
        EndEntry();
    }

    public void OnSocketCall(ref SocketCallRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        var method = record.Direction switch
        {
            SocketDirection.Sending => "SendTo",
            SocketDirection.Receiving => "RecvFrom",
            _ => throw new InvalidDataException(),
        };

        BeginEntry($"FSocketWin::{method}");
        WriteParam("Bits", record.Bits.ToString());
        WriteParam("Remote", record.Remote?.ToString() ?? "n/a");
        EndEntry();
    }

    public void OnSendBunch(ref SendBunchRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        BeginEntry("Send Bunch");
        WriteParam("Channel", capture.ResolveName(record.Channel));
        WriteParam("Bits", record.Bits.ToString());
        WriteParam("Remote", record.Remote?.ToString() ?? "n/a");
        EndEntry();
    }

    #endregion
}
