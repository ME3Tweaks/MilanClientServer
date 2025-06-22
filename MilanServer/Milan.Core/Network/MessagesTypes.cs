namespace Milan.Core.Network;

public readonly record struct NotificationContext(IConnection Connection, ReadOnlyMemory<byte> Body);
public readonly record struct RequestContext(ISendingConnection Connection, uint FrameIndex, ReadOnlyMemory<byte> Body);
public readonly record struct ResponseContext(IConnection Connection, uint FrameIndex, ReadOnlyMemory<byte> Body);

public delegate Task NotificationHandlerDelegate(in NotificationContext context, CancellationToken token);
public delegate Task RequestHandlerDelegate(in RequestContext context, CancellationToken token);
public delegate Task ResponseHandlerDelegate(in ResponseContext context, CancellationToken token);
