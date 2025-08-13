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

    readonly Lock _connectionLock = new();
    readonly List<ClientConnection> _connections = [];
    readonly CancellationTokenSource _connectionCancelTokenSource = new();

    protected override async Task ExecuteAsync(CancellationToken stoppingToken)
    {
        logger.LogInformation("starting (Milan) ServerWorker");

        handlerRegistrant.RegisterHandlers(_handlerRegistry);
        logger.LogDebug("registered handlers");

        if (_workerOptions.EnableSecureConnection)
        {
            logger.LogError("secure connections not yet implemented");
            throw new NotImplementedException();
        }

        TcpListener listener = new(IPAddress.Any, 23000);

        _connections.EnsureCapacity(16);
        while (!_connectionCancelTokenSource.IsCancellationRequested)
        {
            TcpClient client = await listener.AcceptTcpClientAsync(_connectionCancelTokenSource.Token);

            lock (_connectionLock)
            {
                uint maxConnections = _workerOptions.MaxConnections;
                if (maxConnections == 0 || maxConnections <= _connections.Count)
                {
                    ClientConnection connection = new(
                        _handlerRegistry,
                        OnClientTick, CLIENT_TICK_INTERVAL,
                        client, client.GetStream(), _connectionCancelTokenSource.Token);
                    _connections.Add(connection);
                    _ = connection.ServeAsync().ContinueWith(_ => _connections.Remove(connection));
                }
                else
                {
                    logger.LogWarning("connection to '{client}' terminated: too many connections ({count})",
                        client.Client.RemoteEndPoint, _connections.Count);
                    client.Close();
                }
            }
        }
    }

    const long CLIENT_TICK_INTERVAL = 5;
    async Task OnClientTick(ClientConnection connection, CancellationToken token)
    {
        await Task.CompletedTask;
    }
}
