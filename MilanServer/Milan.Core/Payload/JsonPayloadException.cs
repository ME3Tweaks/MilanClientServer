namespace Milan.Core.Payload;


/// <summary>
/// Exception thrown to handle unexpected payload state.
/// </summary>
/// <param name="position">Number of bytes since start of payload.</param>
/// <param name="message">What exactly is wrong with payload.</param>
public class JsonPayloadException(long position, string message) : Exception(message)
{
    /// <summary>
    /// Number of bytes since start of payload.
    /// </summary>
    public long BytePosition { get; init; } = position;

    public override string ToString() => $"payload error at position {BytePosition}: {Message}";
}
