namespace Milan.Core.Utility;

/// <summary>
/// Dynamically-sized buffer backed by an array.
/// </summary>
/// <param name="len">Initial buffer size, in bytes.</param>
public struct ResizableBuffer(int len = 0)
{
    byte[] _array = new byte[len];
    int _length = len;

    public readonly int Capacity => _array.Length;
    public readonly int Length => _length;

    public void ResizeNoCopy(int len)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(len, 0, nameof(len));
        if (len > _array.Length) _array = new byte[len];
        _length = len;
    }

    public readonly Memory<byte> AsMemory() => _array.AsMemory(0, _length);
    public readonly Span<byte> AsSpan() => _array.AsSpan(0, _length);

    public static implicit operator Memory<byte>(ResizableBuffer buffer) => buffer.AsMemory();
    public static implicit operator Span<byte>(ResizableBuffer buffer) => buffer.AsSpan();

    public static implicit operator ReadOnlyMemory<byte>(ResizableBuffer buffer) => buffer.AsMemory();
    public static implicit operator ReadOnlySpan<byte>(ResizableBuffer buffer) => buffer.AsSpan();
}
