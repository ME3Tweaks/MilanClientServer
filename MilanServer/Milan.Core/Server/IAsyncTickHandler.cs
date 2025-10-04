namespace Milan.Core.Server;

/// <summary>
/// Type for a delegate to run on a connection "tick",
/// provided by the connection's owner at construction time.
/// </summary>
/// <param name="connection">Connection calling the handler.</param>
/// <param name="token">Cancellation token owned by the connection loop.</param>
/// <see cref="IAsyncTickHandler{TConn}.TickAsync(TConn, CancellationToken)"/>
public delegate Task AsyncTickHandler<TConn>(TConn connection, CancellationToken token)
    where TConn : ISendingConnection;

/// <summary>
/// Provides an <see cref="AsyncTickHandler{TConn}"/> and ticking interval to <typeparamref name="TConn"/> instances.
/// </summary>
/// <typeparam name="TConn">Ticking connection type.</typeparam>
public interface IAsyncTickHandler<TConn> where TConn : ISendingConnection
{
    /// <summary>
    /// Tick interval, in seconds.
    /// </summary>
    public long Interval { get; }

    /// <summary>
    /// Async action to perform at <see cref="Interval"/> seconds.
    /// </summary>
    /// <param name="connection">Connection calling the handler.</param>
    /// <param name="token">Cancellation token owned by the connection loop.</param>
    public Task TickAsync(TConn connection, CancellationToken token);
}
