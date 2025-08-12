using Microsoft.Extensions.Hosting;
using Microsoft.Extensions.Logging;
using Milan.Core.Network;

namespace Milan.Core.Server;

/// <summary>
/// Sets up and executes the server loop.
/// </summary>
public sealed class ServerWorker(ILogger<ServerWorker> logger, IHandlerRegistrant handlerRegistrant)
    : BackgroundService
{
    readonly TableHandlerRegistry _handlerRegistry = new();

    protected override Task ExecuteAsync(CancellationToken stoppingToken)
    {
        logger.LogInformation("Starting (Milan) ServerWorker.");

        handlerRegistrant.RegisterHandlers(_handlerRegistry);
        logger.LogDebug("Registered handlers.");

        throw new NotImplementedException();
    }
}
