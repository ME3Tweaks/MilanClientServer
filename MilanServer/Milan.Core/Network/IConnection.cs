using Milan.Core.Payload;
using Milan.Core.Utility;
using System.Net;

namespace Milan.Core.Network;

/// <summary>
/// Common interface for a network connection.
/// </summary>
/// <see cref="ClientConnection"/>
public interface IConnection
{
    /// <summary>
    /// Tries to retrieve the client's address.
    /// </summary>
    public IPEndPoint? EndPoint { get; }
    /// <summary>
    /// Checks if this connection is using TLS encryption.
    /// </summary>
    public bool IsSecure { get; }

    /// <summary>
    /// Signals to close the connection asynchronously.
    /// </summary>
    public Task CloseAsync();
}

/// <summary>
/// Specialization of <see cref="IConnection"/> capable of sending frames.
/// </summary>
public interface ISendingConnection : IConnection
{
    /// <summary>
    /// Sends a message with a plain binary payload.
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    /// <param name="msg">Enumeration for whether the message should be sent as a notification.</param>
    /// <param name="body">Payload contents.</param>
    public Task SendBytesAsync(byte component, byte command, EServerMessage msg, ReadOnlyMemory<byte> body);
    /// <summary>
    /// Sends a message with a serialized JSON payload.
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    /// <param name="msg">Enumeration for whether the message should be sent as a notification.</param>
    /// <param name="callback">Callback which writes payload JSON contents.</param>
    public Task SendJsonAsync(byte component, byte command, EServerMessage msg, RefSingleAction<JsonObjectWriter> callback);

    /// <summary>
    /// Overload for <see cref="SendBytesAsync(byte, byte, EServerMessage, ReadOnlyMemory{byte})"/>
    /// which uses <see cref="EServerMessage.Default"/> message type.
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    /// <param name="body">Payload contents.</param>
    public async Task SendBytesAsync(byte component, byte command, ReadOnlyMemory<byte> body)
        => await SendBytesAsync(component, command, EServerMessage.Default, body);
    /// <summary>
    /// Overload for <see cref="SendJsonAsync(byte, byte, EServerMessage, RefSingleAction{JsonObjectWriter})"/>
    /// which uses <see cref="EServerMessage.Default"/> message type.
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    /// <param name="callback">Callback which writes payload JSON contents.</param>
    public async Task SendJsonAsync(byte component, byte command, RefSingleAction<JsonObjectWriter> callback)
        => await SendJsonAsync(component, command, EServerMessage.Default, callback);
}
