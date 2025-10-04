using Milan.Core.Payload;
using Milan.Core.Utility;
using System.Net;

namespace Milan.Core.Server;

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
    /// Sends a message without a payload.
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    /// <param name="msg">Enumeration for whether the message should be sent as a notification.</param>
    public Task SendEmptyAsync(byte component, byte command, EServerMessage msg);
    /// <summary>
    /// Sends a message with a serialized JSON payload.
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    /// <param name="msg">Enumeration for whether the message should be sent as a notification.</param>
    /// <param name="callback">Callback which writes payload's JSON contents.</param>
    public Task SendJsonAsync(byte component, byte command, EServerMessage msg, RefSingleAction<JsonObjectWriter> callback);
    /// <summary>
    /// Sends a message with an arbitrary payload through a stream.
    /// <para>
    /// This may be more efficient than <see cref="SendBytesAsync(byte, byte, EServerMessage, ReadOnlyMemory{byte})"/>,
    /// depending on your circumstances.
    /// </para>
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    /// <param name="msg">Enumeration for whether the message should be sent as a notification.</param>
    /// <param name="callback">Callback which writes payload's stream contents.</param>
    public Task SendStreamAsync(byte component, byte command, EServerMessage msg, Action<Stream> callback);

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
    /// Overload for <see cref="SendEmptyAsync(byte, byte, EServerMessage)"/>
    /// which uses <see cref="EServerMessage.Default"/> message type.
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    public async Task SendEmptyAsync(byte component, byte command)
        => await SendEmptyAsync(component, command, EServerMessage.Default);
    /// <summary>
    /// Overload for <see cref="SendJsonAsync(byte, byte, EServerMessage, RefSingleAction{JsonObjectWriter})"/>
    /// which uses <see cref="EServerMessage.Default"/> message type.
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    /// <param name="callback">Callback which writes payload's JSON contents.</param>
    public async Task SendJsonAsync(byte component, byte command, RefSingleAction<JsonObjectWriter> callback)
        => await SendJsonAsync(component, command, EServerMessage.Default, callback);
    /// <summary>
    /// Overload for <see cref="SendStreamAsync(byte, byte, EServerMessage, Action{Stream})"/>
    /// which uses <see cref="EServerMessage.Default"/> message type.
    /// </summary>
    /// <param name="component">Component index in the connection's associated registry.</param>
    /// <param name="command">Command index in the registered <c>component</c>.</param>
    /// <param name="callback">Callback which writes payload's stream contents.</param>
    public async Task SendStreamAsync(byte component, byte command, Action<Stream> callback)
        => await SendStreamAsync(component, command, EServerMessage.Default, callback);
}
