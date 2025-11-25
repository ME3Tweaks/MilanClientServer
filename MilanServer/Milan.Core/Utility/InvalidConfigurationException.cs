namespace Milan.Core.Utility;

/// <summary>
/// Exception to throw when a worker, a service, or a component
/// fail to parse an invalid configuration parameter.
/// </summary>
/// <param name="message">Description of what is wrong with configuration.</param>
public sealed class InvalidConfigurationException(string message) : Exception(message)
{
    // ...
}
