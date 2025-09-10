namespace Milan.Core.Network;

public sealed class ClientTickHandler : IAsyncTickHandler<ClientConnection>
{
    public long Interval => 5;
    public async Task TickAsync(ClientConnection connection, CancellationToken token)
    {
        await Task.CompletedTask;
    }
}
