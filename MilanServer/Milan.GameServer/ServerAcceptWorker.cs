using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Milan.Core.Options;
using Milan.Core.Server;
using System.Net;
using System.Net.Sockets;
using System.Threading.Channels;

namespace Milan.GameServer;

/// <summary>
/// Sets up and executes the main server loop (accepting and dispatching connections).
/// </summary>
public sealed class ServerAcceptWorker(
    ILogger<ServerAcceptWorker> logger,
    ILogger<ClientConnection> clientLogger,
    IHandlerRegistrant handlerRegistrant,
    IHandlerRegistry handlerRegistry,
    IOptions<ServerWorkerOptions> workerOptions,
    ChannelWriter<ClientStateMessage> clientStateWriter)
        : BackgroundService
{
    readonly ServerWorkerOptions _workerOptions = workerOptions.Value;

    long _maxActiveConnections = 0;
    long _activeConnections = 0;

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        logger.LogInformation("Starting ServerAcceptWorker...");

        handlerRegistrant.RegisterHandlers(handlerRegistry);
        logger.LogInformation("Handlers registered.");

        if (_workerOptions.EnableSecureConnection)
        {
            logger.LogError("Secure connections not yet implemented.");
            throw new NotImplementedException();
        }

        _maxActiveConnections = _workerOptions.MaxActiveConnections;

        var address = IPAddress.Parse(_workerOptions.Ip);
        using var listener = new TcpListener(address, _workerOptions.Port);

        logger.LogInformation("Listening at '{endpoint}'.", listener.LocalEndpoint);
        listener.Start();

        while (!stoppingToken.IsCancellationRequested)
        {
            // TODO: See if we need to handle OperationCanceledException here.

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

            ClientConnection connection = new(
                client,
                client.GetStream(),
                clientStateWriter,
                clientLogger,
                handlerRegistry,
                stoppingToken);

            _ = connection.ServeAsync().ContinueWith(t =>
                {
                    logger.LogDebug("Closed connection to '{endpoint}'.", connection.EndPoint);
                    Interlocked.Decrement(ref _activeConnections);
                }, stoppingToken);
        }

        listener.Stop();
        logger.LogInformation("Listening loop stopped.");
    }
}
