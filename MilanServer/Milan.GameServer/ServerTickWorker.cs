using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Milan.Core.Server;
using System.Threading.Channels;

namespace Milan.GameServer;

/// <summary>
/// Sets up and executes the server tick loop (on connections received from <see cref="ServerAcceptWorker"/>).
/// </summary>
/// <param name="logger"></param>
public sealed class ServerTickWorker(
    ILogger<ServerTickWorker> logger,
    ChannelReader<ClientStateMessage> clientStateReader)
        : BackgroundService
{
    /// <summary>
    /// Default interval, in seconds, for periodic tasks.
    /// </summary>
    public const long TICK_INTERVAL_SEC = 5;

    /// <summary>
    /// Storage for all registered connection instances.
    /// </summary>
    /// <remarks>
    /// Not the most efficient for iteration, but can be left as-is
    /// until I've got some hard data on its performance impact.  -- d00t
    /// </remarks>
    private readonly HashSet<ClientConnection> _connections = [];

    /// <summary>
    /// Synchronization object for <see cref="_connections"/> storage.
    /// </summary>
    private readonly SemaphoreSlim _semaphore = new(initialCount: 1, maxCount: 1);

    protected override async Task ExecuteAsync(CancellationToken token)
    {
        logger.LogInformation("Starting ServerTickWorker...");

        try
        {
            await TickEveryoneAsync(token);

            using PeriodicTimer timer = new(TimeSpan.FromSeconds(TICK_INTERVAL_SEC));
            while (await timer.WaitForNextTickAsync(token) && !token.IsCancellationRequested)
                await TickEveryoneAsync(token);
        }
        catch (OperationCanceledException)
        {
            logger.LogWarning("ServerTickWorker suffered an OperationCanceledException, terminating.");
        }

        logger.LogInformation("Ticking loop stopped.");
    }

    private async Task TickEveryoneAsync(CancellationToken token)
    {
        // TODO: With new design, I'm not sure this semaphore is actually necessary here...
        await _semaphore.WaitAsync(token);

        while (clientStateReader.TryRead(out ClientStateMessage message))
        {
            if (!message.Closing)
            {
                if (!_connections.Add(message.Connection))
                {
                    logger.LogWarning("Added connection to '{address}' already stored.",
                        message.Connection.EndPoint);
                }
            }
            else
            {
                if (!_connections.Remove(message.Connection))
                {
                    logger.LogWarning("Removed connection to '{address}' not stored (yet?)",
                        message.Connection.EndPoint);
                }
            }
        }

        if (!token.IsCancellationRequested)
        {
            // TODO: Provide a linked cancellation token here.
            await Task.WhenAll(_connections.Select(conn => conn.TickAsync()));
        }

        _semaphore.Release();
    }
}
