using KNSoft.ZPigeon.Server.Managed;
using System.Buffers.Binary;
using System.Collections;
using System.Diagnostics.CodeAnalysis;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

internal static class RemoteMediaTests
{
    private const BindingFlags InstanceMembers = BindingFlags.Instance | BindingFlags.NonPublic;
    private static readonly Assembly Web = Assembly.Load("KNSoft.ZPigeon.Web");
    private static readonly Type Stun = Web.GetType("KNSoft.ZPigeon.Web.StunServer", true)!;

    internal static void Run()
    {
        TestLeases();
        TestExpiry();
        TestRateLimit();
        TestBinding(IPAddress.Loopback);
        TestBinding(IPAddress.IPv6Loopback);
        TestMalformedAttributes();
        TestCapturePolicy();
        TestWindowRelayBuffers();
        TestWindowInputFlowAsync().GetAwaiter().GetResult();
        TestTransportPorts();
        TestClientTemplate();
        Console.WriteLine("Remote media: STUN UDP, leases, IPv4/IPv6, capture policy " +
                          "and bounded relay buffers/input credit; transport ports and Client template passed.");
    }

    private static async Task TestWindowInputFlowAsync()
    {
        using (var fixture = new WindowInputFixture())
        {
            var update = fixture.Stream.UpdateAsync(new WindowCaptureOptions(MaxBitRate: 1_500_000)).AsTask();
            var acknowledgement = fixture.Stream.SendAsync(new byte[] { 4, 0, 1, 0, 0, 0, 1, 0 }).AsTask();
            Assert(fixture.Attempts == 1 && !update.IsCompleted && !acknowledgement.IsCompleted,
                   "Initial input credit must wait for writable without spinning or passing a later ACK.");
            fixture.Writable = true;
            fixture.SignalWritable();
            await Task.WhenAll(update, acknowledgement).WaitAsync(TimeSpan.FromSeconds(2));
            Assert(fixture.Sent.Count == 2 && fixture.Sent[0][0] == 5 && fixture.Sent[1][0] == 4 &&
                   BinaryPrimitives.ReadUInt32LittleEndian(fixture.Sent[0].AsSpan(6)) == 1_500_000,
                   "Writable must release the initial options update and preserve the following display ACK.");
        }
        using (var fixture = new WindowInputFixture())
        {
            using var cancellation = new CancellationTokenSource();
            var canceled = fixture.Stream.UpdateAsync(new WindowCaptureOptions(MaxDimension: 1280),
                                                       cancellation.Token).AsTask();
            var next = fixture.Stream.SendAsync(new byte[] { 4, 0, 1, 0, 0, 0, 1, 0 }).AsTask();
            cancellation.Cancel();
            try
            {
                await canceled.WaitAsync(TimeSpan.FromSeconds(2));
                throw new InvalidOperationException("Request cancellation must interrupt an input credit wait.");
            }
            catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
            {
            }
            fixture.Writable = true;
            fixture.SignalWritable();
            await next.WaitAsync(TimeSpan.FromSeconds(2));
            Assert(fixture.Sent.Count == 1 && fixture.Sent[0][0] == 4,
                   "Canceling one input wait must leave the channel usable and send no canceled options.");
        }
        foreach (var dispose in new[] { false, true })
        {
            using var fixture = new WindowInputFixture();
            var active = fixture.Stream.UpdateAsync(new WindowCaptureOptions(MaxDimension: 1280)).AsTask();
            var queued = fixture.Stream.SendAsync(new byte[] { 4, 0, 1, 0, 0, 0, 1, 0 }).AsTask();
            var completion = dispose ? fixture.Stream.DisposeAsync().AsTask() : Task.CompletedTask;
            if (!dispose) fixture.Complete();
            foreach (var pending in new[] { active, queued })
            {
                try
                {
                    await pending.WaitAsync(TimeSpan.FromSeconds(2));
                    throw new InvalidOperationException("Closing must interrupt every pending input writer.");
                }
                catch (NativeException exception) when (
                    exception.Status is { Type: ZpStatusType.NtStatus, Code: 0xC0000184 })
                {
                }
            }
            await completion.WaitAsync(TimeSpan.FromSeconds(2));
            Assert(fixture.Sent.Count == 0 && fixture.Attempts == 1,
                   "Completion and disposal must wake both credit and send-lock waits without sending after close.");
        }
    }

    private static void TestTransportPorts()
    {
        Assert(new NativeServer(AppContext.BaseDirectory).Port == 21943,
               "The default base port must be 21943.");
        foreach (ushort port in new ushort[] { 1, 52000, ushort.MaxValue - 1 })
        {
            Assert(new NativeServer(AppContext.BaseDirectory, port).Port == port,
                   "A custom base port must preserve both range boundaries without reserving the UDP offset.");
        }
        foreach (ushort port in new ushort[] { 0, ushort.MaxValue })
        {
            try
            {
                _ = new NativeServer(AppContext.BaseDirectory, port);
            }
            catch (ArgumentOutOfRangeException exception) when (exception.ParamName == "port")
            {
                continue;
            }
            throw new InvalidOperationException("Zero and an overflowing UDP base port must be rejected.");
        }
    }

    private static void TestClientTemplate()
    {
        var api = Web.GetType("KNSoft.ZPigeon.Web.ClientBuildWebApi", true)!;
        var read = api.GetMethod("ReadTemplate", BindingFlags.Static | BindingFlags.NonPublic)!
                      .CreateDelegate<Func<string, (byte[] Image, int Offset)>>();
        var template = read(Path.Combine(AppContext.BaseDirectory, "KNSoft.ZPigeon.Client.exe"));
        var configuration = template.Image.AsSpan(template.Offset, 4580);
        Assert(BinaryPrimitives.ReadUInt32LittleEndian(configuration[16..]) == 1 &&
               BinaryPrimitives.ReadUInt32LittleEndian(configuration[20..]) == 0 &&
               BinaryPrimitives.ReadUInt32LittleEndian(configuration[28..]) == 21943 &&
               Encoding.Unicode.GetString(configuration.Slice(420, 32)).TrimEnd('\0') == "127.0.0.1" &&
               Encoding.Unicode.GetString(configuration.Slice(452, 32)).TrimEnd('\0') == "localhost",
               "The native template must keep version 1, no embedded trust root, default port and local TLS identity.");
        var path = Path.Combine(Path.GetTempPath(), $"KNSoft.ZPigeon.TemplateTest-{Guid.NewGuid():N}.exe");
        try
        {
            foreach (uint port in new uint[] { 0, ushort.MaxValue, 0x10000U | 21943, 52000 })
            {
                BinaryPrimitives.WriteUInt32LittleEndian(configuration[28..], port);
                File.WriteAllBytes(path, template.Image);
                try
                {
                    _ = read(path);
                }
                catch (InvalidOperationException)
                {
                    continue;
                }
                throw new InvalidOperationException(
                    "A reusable template must reject a patched base port or TCP TLS policy.");
            }
        }
        finally
        {
            File.Delete(path);
        }
    }

    private static void TestLeases()
    {
        using var fixture = new StunFixture();
        using var client = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        Assert(fixture.Exchange(client, IPAddress.Loopback, Request()) == null,
               "STUN must reject a source without a live lease.");
        using var first = fixture.Authorize(IPAddress.Loopback, IPAddress.Loopback.MapToIPv6());
        using var second = fixture.Authorize(IPAddress.Loopback.MapToIPv6());
        Assert(fixture.Exchange(client, IPAddress.Loopback, Request()) != null,
               "Mapped IPv4 addresses must use the same authorization.");
        first.Dispose();
        first.Dispose();
        Assert(fixture.Exchange(client, IPAddress.Loopback, Request()) != null,
               "Disposing one lease twice must preserve another live lease.");
        second.Dispose();
        Assert(fixture.Exchange(client, IPAddress.Loopback, Request()) == null,
               "Releasing every lease must immediately revoke STUN access.");
    }

    private static void TestExpiry()
    {
        using var fixture = new StunFixture();
        using var lease = fixture.Authorize(IPAddress.Loopback);
        lease.GetType().GetFields(InstanceMembers).Single(field => field.FieldType == typeof(long))
             .SetValue(lease, Environment.TickCount64 - 1);
        using var client = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        Assert(fixture.Exchange(client, IPAddress.Loopback, Request()) == null,
               "Expired authorization must be rejected before the periodic sweep.");
        var allowed = (IDictionary)Stun.GetField("allowed", InstanceMembers)!.GetValue(fixture.Server)!;
        Assert(allowed.Count == 0, "Expired address entries must be reclaimed.");
    }

    private static void TestRateLimit()
    {
        using var fixture = new StunFixture();
        using var lease = fixture.Authorize(IPAddress.Loopback);
        var allow = Stun.GetMethod("Allow", InstanceMembers)!;
        for (var index = 0; index < 64; index++)
        {
            Assert((bool)allow.Invoke(fixture.Server, [IPAddress.Loopback])!,
                   "An authorized source must receive its bounded per-second quota.");
        }
        Assert(!(bool)allow.Invoke(fixture.Server, [IPAddress.Loopback])!,
               "An authorized source must not exceed 64 STUN requests per second.");
    }

    private static void TestBinding(IPAddress address)
    {
        using var fixture = new StunFixture();
        using var lease = fixture.Authorize(address);
        using var client = new UdpClient(new IPEndPoint(address, 0));
        var request = Request();
        var response = fixture.Exchange(client, address, request) ??
                       throw new InvalidOperationException("An authorized IPv4/IPv6 binding must receive a response.");
        var bytes = address.GetAddressBytes();
        Assert(response.Length == 28 + bytes.Length &&
               BinaryPrimitives.ReadUInt16BigEndian(response) == 0x0101 &&
               BinaryPrimitives.ReadUInt16BigEndian(response.AsSpan(2)) == response.Length - 20 &&
               response.AsSpan(4, 16).SequenceEqual(request.AsSpan(4, 16)),
               "Binding success must preserve the cookie, transaction ID and declared length.");
        Assert(BinaryPrimitives.ReadUInt16BigEndian(response.AsSpan(20)) == 0x0020 &&
               response[24] == 0 && response[25] == (bytes.Length == 4 ? 1 : 2),
               "The response must contain the correct XOR-MAPPED-ADDRESS family.");
        var port = ((IPEndPoint)client.Client.LocalEndPoint!).Port;
        Assert(BinaryPrimitives.ReadUInt16BigEndian(response.AsSpan(26)) == (port ^ 0x2112),
               "XOR-MAPPED-ADDRESS must contain the actual UDP source port.");
        for (var index = 0; index < bytes.Length; index++)
        {
            Assert((response[28 + index] ^ request[4 + index]) == bytes[index],
                   "The mapped IPv4/IPv6 address must decode with the request cookie and transaction ID.");
        }
    }

    private static void TestMalformedAttributes()
    {
        using var fixture = new StunFixture();
        using var lease = fixture.Authorize(IPAddress.Loopback);
        using var client = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        var truncated = Request(4);
        BinaryPrimitives.WriteUInt16BigEndian(truncated.AsSpan(20), 0x8022);
        BinaryPrimitives.WriteUInt16BigEndian(truncated.AsSpan(22), 1);
        Assert(fixture.Exchange(client, IPAddress.Loopback, truncated) == null,
               "A truncated STUN attribute must not receive success.");
        var padded = Request(8);
        BinaryPrimitives.WriteUInt16BigEndian(padded.AsSpan(20), 0x8022);
        BinaryPrimitives.WriteUInt16BigEndian(padded.AsSpan(22), 3);
        Assert(fixture.Exchange(client, IPAddress.Loopback, padded) != null,
               "A correctly padded optional SOFTWARE attribute must remain accepted.");
        var invalidCookie = Request();
        invalidCookie[4] ^= 1;
        Assert(fixture.Exchange(client, IPAddress.Loopback, invalidCookie) == null,
               "An invalid magic cookie must not receive success.");
    }

    private static void TestCapturePolicy()
    {
        var capture = Web.GetType("KNSoft.ZPigeon.Web.WindowCaptureWebSocket", true)!;
        var performance = Web.GetType("KNSoft.ZPigeon.Web.ConnectionPerformance", true)!;
        var adapt = capture.GetMethod("Adapt", BindingFlags.Static | BindingFlags.NonPublic)!;
        WindowCaptureOptions Apply(WindowCaptureOptions requested, bool automatic, byte speedClass) =>
            (WindowCaptureOptions)adapt.Invoke(null, [requested, JsonSerializer.Deserialize(JsonSerializer.Serialize(new
            {
                Connected = true,
                Automatic = automatic,
                ManualSpeedClass = speedClass,
                RoundTripMilliseconds = 1200U,
                Level = 1
            }), performance)!])!;
        var requested = new WindowCaptureOptions(MaxDimension: 3840, FrameRate: 30,
                                                 Mode: WindowCaptureMode.Video, MaxBitRate: 100_000_000);
        Assert(Apply(requested, true, 0) == requested,
               "High RTT alone must not reduce the requested resolution, frame rate or detail quality.");
        uint[] limits = [1_500_000, 5_000_000, 16_000_000, 50_000_000, 100_000_000];
        for (byte speedClass = 0; speedClass < limits.Length; speedClass++)
        {
            Assert(Apply(requested, false, speedClass) == requested with { MaxBitRate = limits[speedClass] },
                   "Manual bandwidth classes must cap bitrate while preserving the requested capture settings.");
        }
        requested = requested with { MaxBitRate = 1_000_000 };
        Assert(Apply(requested, false, 0) == requested,
               "The manual speed class must not raise an explicit lower bitrate cap.");
    }

    private static void TestWindowRelayBuffers()
    {
        const int chunkLength = 256 * 1024;
        const int frameLength = 16 * 1024 * 1024;
        var creationType = typeof(NativeServer).GetNestedType("WindowCaptureCreation", BindingFlags.NonPublic)!;
        var creation = Activator.CreateInstance(creationType, true)!;
        var stream = (WindowCaptureStream)Activator.CreateInstance(typeof(WindowCaptureStream),
                                                                 InstanceMembers,
                                                                 null,
                                                                 [nint.Zero, creation],
                                                                 null)!;
        var receive = typeof(WindowCaptureStream).GetMethod("Receive", InstanceMembers)!
                         .CreateDelegate<Func<nint, uint, bool>>(stream);
        var complete = typeof(WindowCaptureStream).GetMethod("Complete", InstanceMembers)!;
        var data = Marshal.AllocHGlobal(frameLength);
        try
        {
            var payload = new byte[frameLength];
            Array.Fill(payload, (byte)0x5a);
            Marshal.Copy(payload, 0, data, payload.Length);
            var header = new byte[21];
            header[0] = 3;
            BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(1), 1);
            BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(5), 3840);
            BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(7), 2160);
            BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(13), 3840);
            BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(15), 2160);
            BinaryPrimitives.WriteUInt32LittleEndian(header.AsSpan(17), frameLength);
            Marshal.Copy(header, 0, data, header.Length);
            Assert(receive(data, (uint)header.Length), "A relay frame header must fit the managed output queue.");
            Marshal.Copy(payload, 0, data, header.Length);
            for (var index = 0; index < frameLength / chunkLength; index++)
            {
                var chunk = data + index * chunkLength;
                Marshal.WriteByte(chunk, (byte)index);
                Assert(receive(chunk, chunkLength),
                       "A complete 16 MiB frame must fit as a header and 64 owned 256 KiB chunks.");
            }
            Assert(stream.Output.TryRead(out var record) && record.Span.SequenceEqual(header),
                   "The relay must preserve the frame header.");
            record!.Dispose();
            var consumedBytes = 0;
            for (var index = 0; index < frameLength / chunkLength; index++)
            {
                Assert(stream.Output.TryRead(out var chunk), "Every accepted frame chunk must remain available.");
                using (chunk)
                {
                    Assert(chunk!.Length == chunkLength && chunk.Span[0] == index && chunk.Span[^1] == 0x5a,
                           "Relay chunks must own complete copies, independent of the native source buffer.");
                    consumedBytes += chunk.Length;
                }
            }
            Assert(consumedBytes == frameLength && !stream.Output.TryRead(out _),
                   "The entire frame must be consumed without losing or retaining chunks.");

            Assert(receive(data, frameLength) && receive(data, frameLength),
                   "Two 16 MiB chunks must fit the 32 MiB relay payload budget.");
            Assert(!receive(data, 1), "The relay byte budget must reject overflow with chunk slots still available.");
            Assert(stream.Output.TryRead(out var held), "An accepted payload must remain queued.");
            Assert(!receive(data, 1), "Dequeuing alone must not release an owned buffer's byte budget.");
            held!.Dispose();
            held.Dispose();
            Assert(receive(data, frameLength), "Disposing a buffer must return its relay byte budget.");
            Assert(!receive(data, 1), "Repeated disposal must release the byte budget only once.");
            while (stream.Output.TryRead(out var buffer)) buffer.Dispose();

            var accepted = 0;
            while (accepted < 256 && receive(data, 1)) accepted++;
            Assert(accepted >= 65 && accepted < 256,
                   "Small relay chunks must also have a finite queue capacity independent of the byte budget.");
            var drained = 0;
            while (stream.Output.TryRead(out var buffer))
            {
                buffer.Dispose();
                drained++;
            }
            Assert(drained == accepted && receive(data, chunkLength),
                   "Draining owned buffers must restore both relay capacity and the byte budget.");
        }
        finally
        {
            // This fixture has no native session; complete only its managed channel and drain owned buffers.
            complete.Invoke(stream, [new ZpStatus(ZpStatusType.None, 0)]);
            while (stream.Output.TryRead(out var buffer)) buffer.Dispose();
            Marshal.FreeHGlobal(data);
        }
    }

    private static byte[] Request(int bodyLength = 0)
    {
        var request = new byte[20 + bodyLength];
        BinaryPrimitives.WriteUInt16BigEndian(request, 1);
        BinaryPrimitives.WriteUInt16BigEndian(request.AsSpan(2), (ushort)bodyLength);
        BinaryPrimitives.WriteUInt32BigEndian(request.AsSpan(4), 0x2112a442);
        for (var index = 8; index < 20; index++) request[index] = (byte)index;
        return request;
    }

    private static void Assert([DoesNotReturnIf(false)] bool condition, string message)
    {
        if (!condition) throw new InvalidOperationException(message);
    }

    private sealed class WindowInputFixture : IDisposable
    {
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int ChannelSend(nint channel, nint data, uint length);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int ChannelCancel(nint channel);

        [StructLayout(LayoutKind.Sequential)]
        private struct ChannelHeader
        {
            internal nint Cancel;
            internal nint Send;
            internal nint Close;
            internal int References;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct CapturePrefix
        {
            internal nint Lock;
            internal int References;
            internal int CallerClosed;
            internal nint Connection;
            internal nint Channel;
        }

        private readonly nint nativeStream = Marshal.AllocHGlobal(Marshal.SizeOf<CapturePrefix>());
        private readonly nint channel = Marshal.AllocHGlobal(Marshal.SizeOf<ChannelHeader>());
        private readonly ChannelSend send;
        private readonly ChannelCancel cancel;
        private readonly Action<ZpStatus> complete;
        private readonly Action writable;
        private int attempts;
        internal readonly WindowCaptureStream Stream;
        internal readonly List<byte[]> Sent = [];
        internal volatile bool Writable;
        internal int Attempts => Volatile.Read(ref attempts);

        internal WindowInputFixture()
        {
            send = (_, data, length) =>
            {
                Interlocked.Increment(ref attempts);
                if (!Writable) return unchecked((int)0xC000022D);
                var bytes = new byte[length];
                Marshal.Copy(data, bytes, 0, bytes.Length);
                Sent.Add(bytes);
                return 0;
            };
            cancel = _ => { Complete(); return 0; };
            Marshal.StructureToPtr(new ChannelHeader
            {
                Send = Marshal.GetFunctionPointerForDelegate(send),
                Cancel = Marshal.GetFunctionPointerForDelegate(cancel)
            }, channel, false);
            // Keep one test-owned reference so native Close cannot free this independently allocated prefix.
            Marshal.StructureToPtr(new CapturePrefix { References = 2, Channel = channel }, nativeStream, false);
            var creationType = typeof(NativeServer).GetNestedType("WindowCaptureCreation", BindingFlags.NonPublic)!;
            var creation = Activator.CreateInstance(creationType, true)!;
            creationType.GetField("Handle", InstanceMembers)!.SetValue(creation, GCHandle.Alloc(creation));
            Stream = (WindowCaptureStream)Activator.CreateInstance(typeof(WindowCaptureStream), InstanceMembers,
                                                                  null, [nativeStream, creation], null)!;
            complete = typeof(WindowCaptureStream).GetMethod("Complete", InstanceMembers)!
                           .CreateDelegate<Action<ZpStatus>>(Stream);
            writable = typeof(WindowCaptureStream).GetMethod("SignalWritable", InstanceMembers)!
                           .CreateDelegate<Action>(Stream);
        }

        internal void Complete() => complete(new ZpStatus(ZpStatusType.None, 0));
        internal void SignalWritable() => writable();

        public void Dispose()
        {
            Stream.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(2)).GetAwaiter().GetResult();
            Marshal.FreeHGlobal(nativeStream);
            Marshal.FreeHGlobal(channel);
            GC.KeepAlive(send);
            GC.KeepAlive(cancel);
        }
    }

    private sealed class StunFixture : IDisposable
    {
        internal readonly object Server = Activator.CreateInstance(Stun,
                                                                  InstanceMembers | BindingFlags.Public,
                                                                  null,
                                                                  [(ushort)0],
                                                                  null)!;
        private readonly int port;

        internal StunFixture()
        {
            Stun.GetMethod("Start", InstanceMembers)!.Invoke(Server, null);
            var socket = (Socket)Stun.GetField("socket", InstanceMembers)!.GetValue(Server)!;
            port = ((IPEndPoint)socket.LocalEndPoint!).Port;
        }

        internal IDisposable Authorize(params IPAddress[] addresses) =>
            (IDisposable)Stun.GetMethod("Authorize", InstanceMembers)!.Invoke(Server, [addresses])!;

        internal byte[]? Exchange(UdpClient client, IPAddress address, byte[] request)
        {
            client.Send(request, new IPEndPoint(address, port));
            if (!client.Client.Poll(500_000, SelectMode.SelectRead)) return null;
            var remote = new IPEndPoint(address, 0);
            var response = client.Receive(ref remote);
            Assert(remote.Port == port, "The STUN response must originate from the test server.");
            return response;
        }

        public void Dispose() => ((IDisposable)Server).Dispose();
    }
}
