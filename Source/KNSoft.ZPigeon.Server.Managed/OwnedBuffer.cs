using System.Buffers;
using System.Threading.Channels;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed class OwnedBuffer : IMemoryOwner<byte>
{
    private byte[]? buffer;
    private readonly int length;
    private readonly Action<int>? released;

    private OwnedBuffer(byte[] buffer, int length, Action<int>? released)
    {
        this.buffer = buffer;
        this.length = length;
        this.released = released;
    }

    public Memory<byte> Memory =>
        (buffer ?? throw new ObjectDisposedException(nameof(OwnedBuffer))).AsMemory(0, length);
    public ReadOnlySpan<byte> Span => Memory.Span;
    public int Length => length;
    public bool IsEmpty => length == 0;

    public byte[] ToArray() => Memory.ToArray();

    public static implicit operator ReadOnlyMemory<byte>(OwnedBuffer buffer) => buffer.Memory;

    internal static unsafe OwnedBuffer Copy(nint source, int length, Action<int>? released = null)
    {
        var buffer = ArrayPool<byte>.Shared.Rent(length);
        new ReadOnlySpan<byte>((void*)source, length).CopyTo(buffer);
        return new(buffer, length, released);
    }

    internal static void Drain(ChannelReader<OwnedBuffer> reader)
    {
        while (reader.TryRead(out var buffer)) buffer.Dispose();
    }

    public void Dispose()
    {
        var rented = Interlocked.Exchange(ref buffer, null);
        if (rented is not null)
        {
            ArrayPool<byte>.Shared.Return(rented);
            released?.Invoke(length);
        }
    }
}
