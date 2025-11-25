using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Milan.Core.Data;
using Milan.Core.Options;
using Milan.Core.Server;
using Milan.Core.Utility;
using System.Net;
using System.Net.Sockets;

namespace Milan.GameServer;

/// <summary>
/// Sets up and executes the main server loop (accepting and dispatching connections).
/// </summary>
public sealed class ServerAcceptWorker(
    IServiceProvider serviceProvider,
    ILogger<ServerAcceptWorker> logger,
    IHandlerRegistrant handlerRegistrant,
    IHandlerRegistry handlerRegistry,
    IDbContextFactory<MilanDatabaseContext> databaseContextFactory,
    IOptions<ServerAcceptWorkerOptions> workerOptions)
        : BackgroundService
{
    readonly ServerAcceptWorkerOptions _workerOptions = workerOptions.Value;

    long _maxActiveConnections = 0;
    long _activeConnections = 0;

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        logger.LogInformation("Starting ServerAcceptWorker...");

        handlerRegistrant.RegisterHandlers(handlerRegistry);
        logger.LogInformation("All handlers registered.");

        if (_workerOptions.EnableSecureConnection)
        {
            logger.LogCritical("Secure connections not yet implemented.");
            throw new NotImplementedException();
        }

        _maxActiveConnections = _workerOptions.MaxActiveConnections;

        if (!TryParseEndPoint(_workerOptions.Address, out IPEndPoint endPoint))
        {
            logger.LogCritical($"invalid ServerAcceptWorker address: {_workerOptions.Address}");
            throw new InvalidConfigurationException("invalid ServerAcceptWorker address");
        }

        using (var context = await databaseContextFactory.CreateDbContextAsync(stoppingToken))
        {
            logger.LogInformation("Initializing database context...");
            await context.OnServerStart(stoppingToken);
        }

        using var listener = new TcpListener(endPoint);

        logger.LogInformation("Listening at {endpoint}.", listener.LocalEndpoint);
        listener.Start();

        try
        {
            while (!stoppingToken.IsCancellationRequested)
            {
                TcpClient client = await listener.AcceptTcpClientAsync(stoppingToken);
                logger.LogDebug("Accepted connection to '{endpoint}'.", client.Client.RemoteEndPoint);

                // Connection counter needs to be incremented and decremented atomically
                // because normal connection closure decrements it in the continuation below.

                if (_maxActiveConnections > 0 && Interlocked.Increment(ref _activeConnections) >= _maxActiveConnections)
                {
                    logger.LogWarning("Terminating connection to '{endpoint}': too many clients ({connections})...",
                        client.Client.RemoteEndPoint, _activeConnections);
                    Interlocked.Decrement(ref _activeConnections);

                    client.Close();
                    continue;
                }

                IServiceScope scope = serviceProvider.CreateScope();
                ClientConnection connection = new(serviceProvider, scope,
                    client, client.GetStream(), handlerRegistry, stoppingToken);

                _ = connection.ServeAsync().ContinueWith(serveTask =>
                {
                    if (_maxActiveConnections > 0)
                        Interlocked.Decrement(ref _activeConnections);

                    scope.Dispose();

                    if (serveTask.Exception is AggregateException aggregateException)
                    {
                        aggregateException.Handle(exception =>
                        {
                            logger.LogError("Exception on a connection to '{endpoint}': {message}", connection.EndPoint, exception?.Message);
#if DEBUG
                            logger.LogError("Exception stack trace: {stacktrace}", exception?.StackTrace);
#endif

                            return true;
                        });
                    }

                    logger.LogDebug("Closed connection to '{endpoint}'.", connection.EndPoint);
                }, TaskContinuationOptions.ExecuteSynchronously);
            }
        }
        catch (OperationCanceledException)
        {
            logger.LogWarning("ServerAcceptWorker caught an OperationCanceledException, terminating.");
        }

        listener.Stop();
        logger.LogInformation("Listening loop stopped.");
    }

    static bool TryParseEndPoint(string address, out IPEndPoint endPoint)
    {
        if (string.IsNullOrWhiteSpace(address))
        {
            endPoint = new IPEndPoint(0, 0);
            return false;
        }

        endPoint = IPEndPoint.Parse(address);
        return endPoint.AddressFamily is AddressFamily.InterNetwork;
    }
}
