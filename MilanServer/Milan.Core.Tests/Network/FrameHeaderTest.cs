using Milan.Core.Network;

namespace Milan.Core.Tests.Network;

public class FrameHeaderTest
{
    [Fact]
    public void CanReadSync()
    {
        byte[] contents = [0x0D, 0x2A, 0xFF, 0x12, 0xD6, 0x87, 0x00, 0x00, 0x30, 0x39];
        Assert.Equal(FrameHeader.NET_SERIAL_SIZE, contents.Length);

        FrameHeader header = new();
        header.ReadFrom(contents);

        Assert.Equal(13, header.Component);
        Assert.Equal(42, header.Command);
        Assert.Equal(byte.MaxValue, (byte)header.Flags);
        Assert.Equal(1234567u, header.Length);
        Assert.Equal((ushort)12345u, header.Index);
    }

    [Fact]
    public void CanWriteSync()
    {
        FrameHeader header = new()
        {
            Component = 13,
            Command = 42,
            Flags = (EFrameFlags)byte.MaxValue,
            Length = 1234567u,
            Index = (ushort)12345u
        };

        byte[] contents = new byte[FrameHeader.NET_SERIAL_SIZE];
        header.WriteTo(contents);

        Assert.Equal([0x0D, 0x2A, 0xFF, 0x12, 0xD6, 0x87, 0x00, 0x00, 0x30, 0x39], contents);
    }

    [Fact]
    public void WontWriteTooLong()
    {
        foreach (uint length in new uint[] { uint.MaxValue - 1000, uint.MaxValue })
        {
            FrameHeader header = new() { Length = length };
            byte[] contents = new byte[FrameHeader.NET_SERIAL_SIZE];
            Assert.Throws<InvalidOperationException>(() => header.WriteTo(contents));
        }
    }
}
