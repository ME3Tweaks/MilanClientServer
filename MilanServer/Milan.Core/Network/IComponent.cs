namespace Milan.Core.Network;

public interface IComponent
{
    public byte Id { get; }
    public string Name { get; }

    void Register(IHandlerRegistry registry);

    IEnumerable<NotificationHandlerDelegate> NotificationHandlers { get; }
    IEnumerable<RequestHandlerDelegate> RequestHandlers { get; }
    IEnumerable<ResponseHandlerDelegate> ResponseHandlers { get; }
}
