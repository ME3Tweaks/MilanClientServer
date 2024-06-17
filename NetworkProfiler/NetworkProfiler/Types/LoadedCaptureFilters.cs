using CommunityToolkit.Mvvm.ComponentModel;
using NetworkProfiler.Core;

namespace NetworkProfiler.Types;


public class ActorReplicationFilter(Action<ActorReplicationFilter, bool> callback) : ObservableObject
{
    string _classQuery = string.Empty;
    public string ClassQuery
    {
        get => _classQuery;
        set
        {
            _classQuery = value;
            OnPropertyChanged();
            callback(this, false);
        }
    }

    const StringComparison COMPARE_MODE = StringComparison.InvariantCultureIgnoreCase;
    public bool Apply(ref ActorReplicationCategory entry)
    {
        if (!string.IsNullOrWhiteSpace(ClassQuery))
            if (!entry.ActorName.Contains(ClassQuery, COMPARE_MODE))
                return false;

        return true;
    }
}

public class RemoteCallFilter(Action<RemoteCallFilter, bool> callback) : ObservableObject
{
    string _remoteCallQuery = string.Empty;
    public string RemoteCallQuery
    {
        get => _remoteCallQuery;
        set
        {
            _remoteCallQuery = value;
            OnPropertyChanged();
            callback(this, false);
        }
    }

    const StringComparison COMPARE_MODE = StringComparison.InvariantCultureIgnoreCase;
    public bool Apply(ref RemoteCallCategory entry)
    {
        if (!string.IsNullOrWhiteSpace(RemoteCallQuery))
        {
            var objectName = entry.ObjectName;
            var funcName = entry.FunctionName;

            if (!objectName.Contains(RemoteCallQuery, COMPARE_MODE)
                && !funcName.Contains(RemoteCallQuery, COMPARE_MODE))
                return false;
        }

        return true;
    }
}

public class ControlMessageFilter(Action<ControlMessageFilter, bool> callback) : ObservableObject
{
    string _paramQuery = string.Empty;
    public string ParamQuery
    {
        get => _paramQuery;
        set
        {
            _paramQuery = value;
            OnPropertyChanged();
            callback(this, false);
        }
    }

    string? _remote = null;
    public string? Remote
    {
        get => _remote;
        set
        {
            _remote = value;
            OnPropertyChanged();
            callback(this, false);
        }
    }

    ControlMessageType? _messageType = null;
    public ControlMessageType? MessageType
    {
        get => _messageType;
        set
        {
            _messageType = value;
            OnPropertyChanged();
            callback(this, false);
        }
    }

    const StringComparison COMPARE_MODE = StringComparison.InvariantCultureIgnoreCase;
    public bool Apply(ref ControlMessageEntry entry)
    {
        if (MessageType is not null)
            if (MessageType != entry.MessageType)
                return false;

        if (Remote is not null)
            if (Remote != entry.Remote)
                return false;

        if (!entry.Params.Contains(ParamQuery, COMPARE_MODE))
            return false;

        return true;
    }
}
