using Milan.Core.Payload;

namespace Milan.Core.Messages.Heartbeat;

/// <summary>
/// Sent from client to server to indicate that the server is online,
/// whenever the client receives and verifies a <see cref="ServerAliveNotification"/>.
/// </summary>
public struct ClientAliveNotification
{
    /// <summary>
    /// Timestamp of this notification being sent.
    /// </summary>
    public DateTimeOffset DispatchTimestamp;
    /// <summary>
    /// Timestamp of the previous <see cref="ServerAliveNotification"/> being sent.
    /// </summary>
    public DateTimeOffset OriginDispatchTimestamp;

    public void Read(ref JsonObjectReader reader)
    {
        long low = reader.GetInt("dispatch_low");
        long high = reader.GetInt("dispatch_high");
        DispatchTimestamp = DateTimeOffset.FromUnixTimeMilliseconds((high << 32) | low);

        long origin_low = reader.GetInt("origin_dispatch_low");
        long origin_high = reader.GetInt("origin_dispatch_low");
        OriginDispatchTimestamp = DateTimeOffset.FromUnixTimeMilliseconds((origin_high << 32) | origin_low);
    }

    public readonly void Write(ref JsonObjectWriter writer)
    {
        long timestamp = DispatchTimestamp.ToUnixTimeMilliseconds();
        writer.SetInt("dispatch_low", (int)(timestamp & uint.MaxValue));
        writer.SetInt("dispatch_high", (int)(timestamp >> 32));

        long origin_timestamp = OriginDispatchTimestamp.ToUnixTimeMilliseconds();
        writer.SetInt("origin_dispatch_low", (int)(origin_timestamp & uint.MaxValue));
        writer.SetInt("origin_dispatch_high", (int)(origin_timestamp >> 32));
    }
}

/// <summary>
/// Sent from server to client to indicate that the server is online.
/// </summary>
public struct ServerAliveNotification
{
    /// <summary>
    /// Timestamp of this notification being sent.
    /// </summary>
    public DateTimeOffset DispatchTimestamp;

    public void Read(ref JsonObjectReader reader)
    {
        long low = reader.GetInt("dispatch_low");
        long high = reader.GetInt("dispatch_high");
        DispatchTimestamp = DateTimeOffset.FromUnixTimeMilliseconds((high << 32) | low);
    }

    public readonly void Write(ref JsonObjectWriter writer)
    {
        long timestamp = DispatchTimestamp.ToUnixTimeMilliseconds();
        writer.SetInt("dispatch_low", (int)(timestamp & uint.MaxValue));
        writer.SetInt("dispatch_high", (int)(timestamp >> 32));
    }
}
