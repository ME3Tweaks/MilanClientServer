namespace Milan.Core.Network;

/// <summary>
/// Auxiliary frame indicator.
/// </summary>
[Flags]
public enum EFrameFlags : byte
{
    Notification = 1 << 0,
}

/// <summary>
/// In-memory representation of a frame header.
/// </summary>
public struct FrameHeader
{
    /// <summary>
    /// Number of bytes this frame header occupies over the wire.
    /// </summary>
    public const int NET_SERIAL_SIZE = 10;
    /// <summary>
    /// Maximum value of <c>Length</c> field, equal to maximum value which fits into a 24-bit unsigned integer.
    /// </summary>
    public const uint NET_LENGTH_MAX = 16777215;

    // TODO: Move `MAX_COMPONENT_COUNT` and `MAX_COMMAND_COUNT` out to `HandlerTable` or whatever.
    //   These restrictions make sense but not at protocol level.

    /// <summary>
    /// Maximum allowed number of components registered.
    /// </summary>
    public const byte MAX_COMPONENT_COUNT = 1 << 6;
    /// <summary>
    /// Maximum allowed number of commands registered per component.
    /// </summary>
    public const byte MAX_COMMAND_COUNT = 1 << 6;

    /// <summary>
    /// Index of the component (i.e. group of commands).
    /// </summary>
    public byte Component;
    /// <summary>
    /// Index of the command within the <see cref="Component"/>.
    /// </summary>
    public byte Command;
    /// <summary>
    /// Frame flags.
    /// </summary>
    public EFrameFlags Flags;
    /// <summary>
    /// Number of bytes in this frame's body.
    /// Serialized as a big-endian 24-bit unsigned integer.
    /// </summary>
    public uint Length;
    /// <summary>
    /// Number of this message within a session.
    /// Serialized as a big-endian 32-bit unsigned integer.
    /// </summary>
    public uint Index;

    /// <summary>
    /// Assigns frame length, checking for overflow.
    /// </summary>
    /// <param name="length">New frame length.</param>
    /// <exception cref="ArgumentOutOfRangeException">When <c>length</c> is negative or too high to be serialized.</exception>
    public void SetLength(long length)
    {
        if (length < 0 || length > NET_LENGTH_MAX)
        {
            throw new ArgumentOutOfRangeException(nameof(length),
                $"cannot serialize frame header for {Length} body byte(s)");
        }

        Length = (uint)length;
    }

    /// <summary>
    /// Serializes this frame header from a <c>stream</c>.
    /// </summary>
    /// <param name="stream">Input stream.</param>
    /// <param name="buffer">Buffer for <see cref="NET_SERIAL_SIZE"/> byte(s).</param>
    /// <param name="token">Cancel notification token.</param>
    /// <exception cref="ArgumentException">
    /// If <c>stream</c> does not support reading,
    /// or <c>buffer</c> is not <see cref="NET_SERIAL_SIZE"/> byte(s) long.
    /// </exception>
    public async Task ReadAsync(Stream stream, Memory<byte> buffer, CancellationToken token)
    {
#if DEBUG
        if (!stream.CanRead)
            throw new ArgumentException("stream was not readable", nameof(stream));
        if (buffer.Length != NET_SERIAL_SIZE)
            throw new ArgumentException($"buffer was {buffer.Length} byte(s) long, expected {NET_SERIAL_SIZE}", nameof(buffer));
#endif

        await stream.ReadExactlyAsync(buffer, token);
        ReadFrom(buffer.Span);
    }

    /// <summary>
    /// Serializes this frame header from a memory block.
    /// </summary>
    /// <param name="header">Block of <see cref="NET_SERIAL_SIZE"/> byte(s).</param>
    public void ReadFrom(Span<byte> header)
    {
        Component = header[0];
        Command = header[1];
        Flags = (EFrameFlags)header[2];
        Length = (uint)header[3] << 16 | (uint)header[4] << 8 | header[5];
        Index = (uint)header[6] << 24 | (uint)header[7] << 16 | (uint)header[8] << 8 | header[9];
    }

    /// <summary>
    /// Serializes this frame header to a <c>stream</c>.
    /// </summary>
    /// <param name="stream">Output stream.</param>
    /// <param name="buffer">Buffer for <see cref="NET_SERIAL_SIZE"/> byte(s).</param>
    /// <param name="token">Cancel notification token.</param>
    /// <exception cref="ArgumentException">
    /// If <c>stream</c> does not support reading,
    /// or <c>buffer</c> is not <see cref="NET_SERIAL_SIZE"/> byte(s) long.
    /// </exception>
    /// <exception cref="InvalidOperationException">
    /// If <c>Length</c> is too large to fit into a 24-bit unsigned integer,
    /// which is mandated by the serialization format.
    /// </exception>
    public async readonly Task WriteAsync(Stream stream, Memory<byte> buffer, CancellationToken token)
    {
#if DEBUG
        if (!stream.CanWrite)
            throw new ArgumentException("stream was not writable", nameof(stream));
        if (buffer.Length != NET_SERIAL_SIZE)
            throw new ArgumentException($"buffer was {buffer.Length} byte(s) long, expected {NET_SERIAL_SIZE}", nameof(buffer));
#endif

        WriteTo(buffer.Span);
        await stream.WriteAsync(buffer, token);
    }

    /// <summary>
    /// Serializes this frame header to a memory block.
    /// </summary>
    /// <param name="header">Block of <see cref="NET_SERIAL_SIZE"/> byte(s).</param>
    /// <exception cref="InvalidOperationException">See <see cref="WriteAsync(Stream, Memory{byte}, CancellationToken)"/>.</exception>
    public readonly void WriteTo(Span<byte> header)
    {
        if (Length > NET_LENGTH_MAX)
        {
            // Checking that 'Length' fits into a 24-bit unsigned integer.
            throw new InvalidOperationException($"cannot serialize frame header for {Length} body byte(s)");
        }

        header[0] = Component;
        header[1] = Command;
        header[2] = (byte)Flags;
        header[3] = (byte)((Length >> 16) & 0xFF);
        header[4] = (byte)((Length >> 8) & 0xFF);
        header[5] = (byte)(Length & 0xFF);
        header[6] = (byte)((Index >> 24) & 0xFF);
        header[7] = (byte)((Index >> 16) & 0xFF);
        header[8] = (byte)((Index >> 8) & 0xFF);
        header[9] = (byte)(Index & 0xFF);
    }
}
