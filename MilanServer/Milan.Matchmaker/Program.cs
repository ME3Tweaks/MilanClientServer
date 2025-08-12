using Milan.Core.Components;
using Milan.Core.Network;

TableHandlerRegistry registry = new();
new RedirectorComponent().Register(registry);
new HeartbeatComponent().Register(registry);
