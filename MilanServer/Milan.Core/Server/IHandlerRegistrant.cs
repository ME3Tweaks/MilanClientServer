namespace Milan.Core.Server;

/// <summary>
/// Allows server executables to register only a subset of components.
/// </summary>
public interface IHandlerRegistrant
{
    /// <summary>
    /// Invokes <see cref="BaseComponent{T}.Register(IHandlerRegistry)"/>
    /// for whichever components the server wants.
    /// </summary>
    /// <param name="registry">Receiving handler registry.</param>
    void RegisterHandlers(IHandlerRegistry registry);
}
