using System.Diagnostics;
using System.IO;
using System.Text;
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
    readonly string? _query = null;


    public LoadedFrame(LoadedCapture source, int frameIndex, string searchQuery)
    {
        //Debug.WriteLine($"searchQuery = {searchQuery}, isnows = {string.IsNullOrWhiteSpace(searchQuery)}");

        capture = source.capture;
        _query = string.IsNullOrWhiteSpace(searchQuery) ? null : searchQuery;

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

        if (!FilterRecord(ref record))
            return;

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

        if (!FilterRecord(ref record))
            return;

        BeginEntry($"Frame start");
        WriteParam("Index", frame.ToString());
        WriteParam("Time", time.ToString());
        EndEntry();
    }

    public void OnLevelStart(ref LevelStartRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        if (!FilterRecord(ref record))
            return;

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

        if (!FilterRecord(ref record))
            return;

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

        if (!FilterRecord(ref record))
            return;

        BeginEntry("Replicate actor");
        WriteParam("Actor", capture.ResolveName(record.Actor));
        WriteParam("Remote", record.Remote?.ToString() ?? "n/a");
        EndEntry();
    }

    public void OnPropertyReplication(ref PropertyReplicationRecord record, int frame, float time)
    {
        Debug.Assert(frame == Index);
        RecordCount += 1;

        if (!FilterRecord(ref record))
            return;

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

        if (!FilterRecord(ref record))
            return;

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

        if (!FilterRecord(ref record))
            return;

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

        if (!FilterRecord(ref record))
            return;

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

        if (!FilterRecord(ref record))
            return;

        BeginEntry("Send Bunch");
        WriteParam("Channel", capture.ResolveName(record.Channel));
        WriteParam("Bits", record.Bits.ToString());
        WriteParam("Remote", record.Remote?.ToString() ?? "n/a");
        EndEntry();
    }

    #endregion


    #region Record filtering implementation.

    const StringComparison COMPARISON = StringComparison.InvariantCultureIgnoreCase;

    bool FilterRecord(ref TieredStringLookupRecord record)
    {
        if (_query is null)
            return true;

        if ("Tiered string lookup".Contains(_query, COMPARISON))
            return true;

        return false;
    }

    bool FilterRecord(ref FrameStartRecord record)
    {
        if (_query is null)
            return true;

        if ("Frame start".Contains(_query, COMPARISON))
            return true;

        if (record.Delta.ToString().Contains(_query, COMPARISON))
            return true;

        return false;
    }

    bool FilterRecord(ref LevelStartRecord record)
    {
        if (_query is null)
            return true;

        if ("Level start".Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveString(record.Map).Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveString(record.Portal).Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveString(record.Host).Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveString(record.Protocol).Contains(_query, COMPARISON))
            return true;

        if (record.Port.ToString().Contains(_query, COMPARISON))
            return true;

        for (int i = 0; i < record.Ops.Length; i++)
            if (capture.ResolveString(record.Ops[i]).Contains(_query, COMPARISON))
                return true;

        return false;
    }

    bool FilterRecord(ref RemoteFunctionRecord record)
    {
        if (_query is null)
            return true;

        if ("Remote call".Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveName(record.Actor).Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveName(record.Function).Contains(_query, COMPARISON))
            return true;

        if (record.Bits.ToString().Contains(_query, COMPARISON))
            return true;

        if ((record.Remote?.ToString() ?? "n/a").Contains(_query, COMPARISON))
            return true;

        return false;
    }

    bool FilterRecord(ref ActorReplicationRecord record)
    {
        if (_query is null)
            return true;

        if ("Replicate actor".Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveName(record.Actor).Contains(_query, COMPARISON))
            return true;

        if ((record.Remote?.ToString() ?? "n/a").Contains(_query, COMPARISON))
            return true;

        return false;
    }

    bool FilterRecord(ref PropertyReplicationRecord record)
    {
        if (_query is null)
            return true;

        if ("Replicate property".Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveName(record.Property).Contains(_query, COMPARISON))
            return true;

        if (record.Bits.ToString().Contains(_query, COMPARISON))
            return true;

        if ((record.Remote?.ToString() ?? "n/a").Contains(_query, COMPARISON))
            return true;

        return false;
    }

    bool FilterRecord(ref ControlMessageRecord record)
    {
        if (_query is null)
            return true;

        if ("Control message".Contains(_query, COMPARISON))
            return true;

        if (record.MsgType.ToString().Contains(_query, COMPARISON))
            return true;

        if (record.Recipient.ToString().Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveString(record.Params).Contains(_query, COMPARISON))
            return true;

        if ((record.Remote?.ToString() ?? "n/a").Contains(_query, COMPARISON))
            return true;

        return false;
    }

    bool FilterRecord(ref SessionEventRecord record)
    {
        if (_query is null)
            return true;

        if ("Session event".Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveString(record.Event).Contains(_query, COMPARISON))
            return true;

        if (capture.ResolveString(record.Description).Contains(_query, COMPARISON))
            return true;

        if ((record.Remote?.ToString() ?? "n/a").Contains(_query, COMPARISON))
            return true;

        return false;
    }

    bool FilterRecord(ref SocketCallRecord record)
    {
        // If we are filtering the log, we are probably not interested in low-level calls.
        return _query is null;
    }

    bool FilterRecord(ref SendBunchRecord record)
    {
        // If we are filtering the log, we are probably not interested in low-level calls.
        return _query is null;
    }

    #endregion
}
