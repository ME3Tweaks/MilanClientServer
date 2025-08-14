using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;
using Milan.Core.Network;
using Milan.Core.Options;
using System.Net;
using System.Net.Sockets;

namespace Milan.Core.Server;

/// <summary>
/// Sets up and executes the server loop.
/// </summary>
public sealed class ServerWorker(ILogger<ServerWorker> logger,
    IHandlerRegistrant handlerRegistrant, IOptions<ServerWorkerOptions> workerOptions)
        : BackgroundService
{
    readonly TableHandlerRegistry _handlerRegistry = new();
    readonly ServerWorkerOptions _workerOptions = workerOptions.Value;

    long _maxActiveConnections = 0;
    long _activeConnections = 0;

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        logger.LogInformation("Starting (Milan) ServerWorker...");

        handlerRegistrant.RegisterHandlers(_handlerRegistry);
        logger.LogInformation("Handlers registered.");

        if (_workerOptions.EnableSecureConnection)
        {
            logger.LogError("Secure connections not yet implemented.");
            throw new NotImplementedException();
        }

        _maxActiveConnections = _workerOptions.MaxActiveConnections;

        var address = IPAddress.Parse(_workerOptions.Ip);
        using TcpListener listener = new(address, _workerOptions.Port);

        listener.Start();
        logger.LogInformation("Listening at '{endpoint}'.", listener.LocalEndpoint);

        while (!stoppingToken.IsCancellationRequested)
        {
            TcpClient client = await listener.AcceptTcpClientAsync(stoppingToken);
            logger.LogDebug("Accepted connection to '{endpoint}'.", client.Client.RemoteEndPoint);

            if (_maxActiveConnections > 0 && Interlocked.Increment(ref _activeConnections) >= _maxActiveConnections)
            {
                logger.LogWarning("Terminating connection to '{endpoint}': too many clients ({connections})...",
                    client.Client.RemoteEndPoint, _activeConnections);

                Interlocked.Decrement(ref _activeConnections);
                client.Close();
                continue;
            }

            ClientConnection connection = new(
                _handlerRegistry,
                ClientTickHandler, CLIENT_TICK_SEC,
                client, client.GetStream(), stoppingToken);
            _ = connection.ServeAsync().ContinueWith(onCloseConnection, stoppingToken);

            void onCloseConnection(Task _)
            {
                logger.LogDebug("Closed connection to '{endpoint}'.", connection.EndPoint);
                Interlocked.Decrement(ref _activeConnections);
            }
        }

        listener.Stop();
        logger.LogInformation("Listening loop stopped.");
    }

    const long CLIENT_TICK_SEC = 5;
    async Task ClientTickHandler(ClientConnection connection, CancellationToken token)
    {
        await Task.CompletedTask;
    }
}
