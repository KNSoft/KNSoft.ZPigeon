using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.RegistryPageCallback
        RegistryKeyPageCallback = CompleteRegistryKeyPage;
    private static readonly NativeMethods.RegistryPageCallback
        RegistryValuePageCallback = CompleteRegistryValuePage;
    private static readonly NativeMethods.RegistryValueCallback
        RegistryValueCallback = CompleteRegistryValue;
    private static readonly NativeMethods.RegistryRangeCallback
        RegistryRangeCallback = CompleteRegistryRange;

    public Task<RegistryPage<RegistryKeyRecord>> EnumerateRegistryKeysPageAsync(
        RegistryRoot root,
        string path,
        string? cursor,
        uint maxEntries) =>
        RunRegistryPageAsync<RegistryKeyRecord>(
            (callback, context) => NativeMethods.EnumerateRegistryKeysPage(
                root,
                path,
                (uint)path.Length,
                cursor,
                (uint)(cursor?.Length ?? 0),
                maxEntries,
                callback,
                context),
            RegistryKeyPageCallback);

    public Task<RegistryPage<RegistryValueRecord>>
        EnumerateRegistryValuesPageAsync(
            RegistryRoot root,
            string path,
            string? cursor,
            uint maxEntries) =>
        RunRegistryPageAsync<RegistryValueRecord>(
            (callback, context) => NativeMethods.EnumerateRegistryValuesPage(
                root,
                path,
                (uint)path.Length,
                cursor,
                (uint)(cursor?.Length ?? 0),
                maxEntries,
                callback,
                context),
            RegistryValuePageCallback);

    public Task<RegistryValue> QueryRegistryValueAsync(
        RegistryRoot root,
        string path,
        string name)
    {
        var completion = new TaskCompletionSource<RegistryValue>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = NativeMethods.QueryRegistryValue(
            root,
            path,
            (uint)path.Length,
            name,
            (uint)name.Length,
            RegistryValueCallback,
            GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task<RegistryRange> QueryRegistryValueRangeAsync(
        RegistryRoot root,
        string path,
        string name,
        uint offset,
        uint length)
    {
        var completion = new TaskCompletionSource<RegistryRange>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = NativeMethods.QueryRegistryValueRange(
            root,
            path,
            (uint)path.Length,
            name,
            (uint)name.Length,
            offset,
            length,
            RegistryRangeCallback,
            GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public unsafe Task WriteRegistryValueRangeAsync(
        RegistryRoot root,
        string path,
        string name,
        uint offset,
        byte[] data)
    {
        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        int status;
        fixed (byte* pointer = data)
        {
            status = NativeMethods.WriteRegistryValueRange(
                root,
                path,
                (uint)path.Length,
                name,
                (uint)name.Length,
                offset,
                (nint)pointer,
                (uint)data.Length,
                StatusCallback,
                GCHandle.ToIntPtr(handle));
        }
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task<RegistryValue> QueryRegistrySecurityAsync(
        RegistryRoot root,
        string path)
    {
        var completion = new TaskCompletionSource<RegistryValue>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = NativeMethods.QueryRegistrySecurity(
            root,
            path,
            (uint)path.Length,
            RegistryValueCallback,
            GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task SetRegistrySecurityAsync(RegistryRoot root, string path, string sddl) =>
        RunStatusAsync((callback, context) => NativeMethods.SetRegistrySecurity(
            root,
            path,
            (uint)path.Length,
            sddl,
            (uint)sddl.Length,
            callback,
            context));

    public unsafe Task SetRegistryValueAsync(
        RegistryRoot root,
        string path,
        string name,
        uint type,
        byte[] data)
    {
        var completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        int status;
        fixed (byte* dataPointer = data)
        {
            status = NativeMethods.ExecuteRegistryStatus(
                4,
                root,
                path,
                (uint)path.Length,
                name,
                (uint)name.Length,
                null,
                0,
                type,
                (nint)dataPointer,
                (uint)data.Length,
                StatusCallback,
                GCHandle.ToIntPtr(handle));
        }
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    public Task DeleteRegistryValueAsync(
        RegistryRoot root,
        string path,
        string name) =>
        RunRegistryStatusAsync(5, root, path, name, null);

    public Task CreateRegistryKeyAsync(
        RegistryRoot root,
        string path) =>
        RunRegistryStatusAsync(6, root, path, null, null);

    public Task DeleteRegistryKeyAsync(
        RegistryRoot root,
        string path) =>
        RunRegistryStatusAsync(7, root, path, null, null);

    public Task RenameRegistryKeyAsync(
        RegistryRoot root,
        string path,
        string name,
        string newName) =>
        RunRegistryStatusAsync(8, root, path, name, newName);

    public Task RenameRegistryValueAsync(
        RegistryRoot root,
        string path,
        string name,
        string newName) =>
        RunRegistryStatusAsync(9, root, path, name, newName);

    private static Task RunRegistryStatusAsync(
        ushort operationId,
        RegistryRoot root,
        string path,
        string? name,
        string? newName) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ExecuteRegistryStatus(operationId,
                                                root,
                                                path,
                                                (uint)path.Length,
                                                name,
                                                (uint)(name?.Length ?? 0),
                                                newName,
                                                (uint)(newName?.Length ?? 0),
                                                0,
                                                0,
                                                0,
                                                callback,
                                                context));

    private static Task<RegistryPage<T>> RunRegistryPageAsync<T>(
        Func<NativeMethods.RegistryPageCallback, nint, int> start,
        NativeMethods.RegistryPageCallback callback)
    {
        var completion = new TaskCompletionSource<RegistryPage<T>>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var handle = GCHandle.Alloc(completion);
        var status = start(callback, GCHandle.ToIntPtr(handle));
        if (status < 0)
        {
            handle.Free();
            ThrowIfFailed(status);
        }
        return completion.Task;
    }

    private static void CompleteRegistryKeyPage(
        ZpStatus status,
        [MarshalAs(UnmanagedType.U1)] bool hasMore,
        nint nextCursor,
        uint nextCursorLength,
        nint records,
        uint recordCount,
        nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion =
            (TaskCompletionSource<RegistryPage<RegistryKeyRecord>>)handle.Target!;
        handle.Free();
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new RegistryKeyRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.RegistryKeyRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.RegistryKeyRecord>(
                records + index * size);
            result[index] = new RegistryKeyRecord(
                Marshal.PtrToStringUni(record.Name, (int)record.NameLength) ??
                    string.Empty,
                record.LastWriteTime,
                record.HasChildren);
        }
        completion.SetResult(new RegistryPage<RegistryKeyRecord>(
            hasMore,
            Marshal.PtrToStringUni(nextCursor, (int)nextCursorLength) ??
                string.Empty,
            result));
    }

    private static void CompleteRegistryValuePage(
        ZpStatus status,
        [MarshalAs(UnmanagedType.U1)] bool hasMore,
        nint nextCursor,
        uint nextCursorLength,
        nint records,
        uint recordCount,
        nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion =
            (TaskCompletionSource<RegistryPage<RegistryValueRecord>>)handle.Target!;
        handle.Free();
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new RegistryValueRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.RegistryValueRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record =
                Marshal.PtrToStructure<NativeMethods.RegistryValueRecord>(
                    records + index * size);
            var preview = new byte[record.PreviewLength];
            if (record.PreviewLength != 0)
            {
                Marshal.Copy(record.Preview,
                             preview,
                             0,
                             (int)record.PreviewLength);
            }
            result[index] = new RegistryValueRecord(
                Marshal.PtrToStringUni(record.Name, (int)record.NameLength) ??
                    string.Empty,
                record.Type,
                record.DataLength,
                preview);
        }
        completion.SetResult(new RegistryPage<RegistryValueRecord>(
            hasMore,
            Marshal.PtrToStringUni(nextCursor, (int)nextCursorLength) ??
                string.Empty,
            result));
    }

    private static void CompleteRegistryValue(
        ZpStatus status,
        uint type,
        nint data,
        uint dataLength,
        nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion = (TaskCompletionSource<RegistryValue>)handle.Target!;
        handle.Free();
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new byte[dataLength];
        if (dataLength != 0)
        {
            Marshal.Copy(data, result, 0, (int)dataLength);
        }
        completion.SetResult(new RegistryValue(type, result));
    }

    private static void CompleteRegistryRange(
        ZpStatus status,
        uint totalLength,
        nint data,
        uint dataLength,
        nint context)
    {
        var handle = GCHandle.FromIntPtr(context);
        var completion = (TaskCompletionSource<RegistryRange>)handle.Target!;
        handle.Free();
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new byte[dataLength];
        if (dataLength != 0) Marshal.Copy(data, result, 0, (int)dataLength);
        completion.SetResult(new RegistryRange(totalLength, result));
    }
}

public enum RegistryRoot : ushort
{
    ClassesRoot = 1,
    CurrentUser,
    LocalMachine,
    Users,
    CurrentConfig
}

public sealed record RegistryKeyRecord(
    string Name,
    ulong LastWriteTime,
    bool HasChildren);
public sealed record RegistryValueRecord(
    string Name,
    uint Type,
    uint DataLength,
    byte[] Preview);
public sealed record RegistryValue(uint Type, byte[] Data);
public sealed record RegistryRange(uint TotalLength, byte[] Data);
public sealed record RegistryPage<T>(bool HasMore, string NextCursor, T[] Records);

internal static partial class NativeMethods
{
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void RegistryPageCallback(
        ZpStatus status,
        [MarshalAs(UnmanagedType.U1)] bool hasMore,
        nint nextCursor,
        uint nextCursorLength,
        nint records,
        uint recordCount,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void RegistryValueCallback(
        ZpStatus status,
        uint type,
        nint data,
        uint dataLength,
        nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void RegistryRangeCallback(
        ZpStatus status,
        uint totalLength,
        nint data,
        uint dataLength,
        nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct RegistryKeyRecord
    {
        internal readonly nint Name;
        internal readonly uint NameLength;
        internal readonly ulong LastWriteTime;
        [MarshalAs(UnmanagedType.U1)]
        internal readonly bool HasChildren;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct RegistryValueRecord
    {
        internal readonly nint Name;
        internal readonly uint NameLength;
        internal readonly uint Type;
        internal readonly uint DataLength;
        internal readonly nint Preview;
        internal readonly uint PreviewLength;
    }

    [LibraryImport(Library,
        EntryPoint = "ZpNative_EnumerateRegistryKeysPage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int EnumerateRegistryKeysPage(
        RegistryRoot root,
        string path,
        uint pathLength,
        string? cursor,
        uint cursorLength,
        uint maxEntries,
        RegistryPageCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_EnumerateRegistryValuesPage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int EnumerateRegistryValuesPage(
        RegistryRoot root,
        string path,
        uint pathLength,
        string? cursor,
        uint cursorLength,
        uint maxEntries,
        RegistryPageCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryRegistryValue",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryRegistryValue(
        RegistryRoot root,
        string path,
        uint pathLength,
        string? name,
        uint nameLength,
        RegistryValueCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryRegistryValueRange",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryRegistryValueRange(
        RegistryRoot root,
        string path,
        uint pathLength,
        string? name,
        uint nameLength,
        uint offset,
        uint length,
        RegistryRangeCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_WriteRegistryValueRange",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int WriteRegistryValueRange(
        RegistryRoot root,
        string path,
        uint pathLength,
        string? name,
        uint nameLength,
        uint offset,
        nint data,
        uint dataLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryRegistrySecurity",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryRegistrySecurity(
        RegistryRoot root,
        string path,
        uint pathLength,
        RegistryValueCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_SetRegistrySecurity",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int SetRegistrySecurity(
        RegistryRoot root,
        string path,
        uint pathLength,
        string sddl,
        uint sddlLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ExecuteRegistryStatus",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ExecuteRegistryStatus(
        ushort operationId,
        RegistryRoot root,
        string path,
        uint pathLength,
        string? name,
        uint nameLength,
        string? newName,
        uint newNameLength,
        uint type,
        nint data,
        uint dataLength,
        StatusCallback callback,
        nint context);

}
