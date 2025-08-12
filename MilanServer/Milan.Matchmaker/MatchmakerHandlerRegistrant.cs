using Milan.Core.Components;
using Milan.Core.Network;
using Milan.Core.Server;

namespace Milan.Matchmaker;

public sealed class MatchmakerHandlerRegistrant(IServiceProvider serviceProvider) : IHandlerRegistrant
{
    public void RegisterHandlers(IHandlerRegistry registry)
    {
        new RedirectorComponent(serviceProvider).Register(registry);
        new HeartbeatComponent(serviceProvider).Register(registry);
    }
}
