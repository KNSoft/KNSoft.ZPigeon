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
            (callback, context) => NativeMethods.EnumerateRegistryKeysPage(ClientId,
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
            (callback, context) => NativeMethods.EnumerateRegistryValuesPage(ClientId,
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
        string name) =>
        RunOperationAsync<RegistryValue>(context => NativeMethods.QueryRegistryValue(ClientId,
            root,
            path,
            (uint)path.Length,
            name,
            (uint)name.Length,
            RegistryValueCallback,
            context));

    public Task<RegistryRange> QueryRegistryValueRangeAsync(
        RegistryRoot root,
        string path,
        string name,
        uint offset,
        uint length) =>
        RunOperationAsync<RegistryRange>(context => NativeMethods.QueryRegistryValueRange(ClientId,
            root,
            path,
            (uint)path.Length,
            name,
            (uint)name.Length,
            offset,
            length,
            RegistryRangeCallback,
            context));

    public unsafe Task WriteRegistryValueRangeAsync(
        RegistryRoot root,
        string path,
        string name,
        uint offset,
        byte[] data)
    {
        fixed (byte* pointer = data)
        {
            var buffer = (nint)pointer;
            return RunStatusAsync((callback, context) => NativeMethods.WriteRegistryValueRange(ClientId,
                root,
                path,
                (uint)path.Length,
                name,
                (uint)name.Length,
                offset,
                buffer,
                (uint)data.Length,
                callback,
                context));
        }
    }

    public Task<SecurityDescriptor> QueryRegistrySecurityAsync(
        RegistryRoot root,
        string path) =>
        RunOperationAsync<SecurityDescriptor>(context => NativeMethods.QueryRegistrySecurity(ClientId,
            root,
            path,
            (uint)path.Length,
            SecurityDescriptorCallback,
            context));

    public Task SetRegistrySecurityAsync(RegistryRoot root, string path, string sddl, bool daclProtected) =>
        RunStatusAsync((callback, context) => NativeMethods.SetRegistrySecurity(ClientId,
            root,
            path,
            (uint)path.Length,
            sddl,
            (uint)sddl.Length,
            daclProtected,
            callback,
            context));

    public unsafe Task SetRegistryValueAsync(
        RegistryRoot root,
        string path,
        string name,
        uint type,
        byte[] data)
    {
        fixed (byte* dataPointer = data)
        {
            var buffer = (nint)dataPointer;
            return RunStatusAsync((callback, context) => NativeMethods.ExecuteRegistryStatus(ClientId,
                4,
                root,
                path,
                (uint)path.Length,
                name,
                (uint)name.Length,
                null,
                0,
                type,
                buffer,
                (uint)data.Length,
                callback,
                context));
        }
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

    private Task RunRegistryStatusAsync(
        ushort operationId,
        RegistryRoot root,
        string path,
        string? name,
        string? newName) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.ExecuteRegistryStatus(ClientId, operationId,
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

    private Task<RegistryPage<T>> RunRegistryPageAsync<T>(
        Func<NativeMethods.RegistryPageCallback, nint, int> start,
        NativeMethods.RegistryPageCallback callback) =>
        RunOperationAsync<RegistryPage<T>>(context => start(callback, context));

    private static void CompleteRegistryKeyPage(
        ZpStatus status,
        [MarshalAs(UnmanagedType.U1)] bool hasMore,
        nint nextCursor,
        uint nextCursorLength,
        nint records,
        uint recordCount,
        nint context)
    {
        var completion = GetCompletion<RegistryPage<RegistryKeyRecord>>(context);
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
                ReadString(record.Name, record.NameLength),
                record.LastWriteTime,
                record.HasChildren);
        }
        completion.SetResult(new RegistryPage<RegistryKeyRecord>(
            hasMore,
            ReadString(nextCursor, nextCursorLength),
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
        var completion = GetCompletion<RegistryPage<RegistryValueRecord>>(context);
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
            var preview = GC.AllocateUninitializedArray<byte>(checked((int)record.PreviewLength));
            if (record.PreviewLength != 0)
            {
                Marshal.Copy(record.Preview,
                             preview,
                             0,
                             (int)record.PreviewLength);
            }
            result[index] = new RegistryValueRecord(
                ReadString(record.Name, record.NameLength),
                record.Type,
                record.DataLength,
                preview);
        }
        completion.SetResult(new RegistryPage<RegistryValueRecord>(
            hasMore,
            ReadString(nextCursor, nextCursorLength),
            result));
    }

    private static void CompleteRegistryValue(
        ZpStatus status,
        uint type,
        nint data,
        uint dataLength,
        nint context)
    {
        var completion = GetCompletion<RegistryValue>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = GC.AllocateUninitializedArray<byte>(checked((int)dataLength));
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
        var completion = GetCompletion<RegistryRange>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = GC.AllocateUninitializedArray<byte>(checked((int)dataLength));
        if (dataLength != 0) Marshal.Copy(data, result, 0, (int)dataLength);
        completion.SetResult(new RegistryRange(totalLength, result));
    }
}

public enum RegistryRoot : byte
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
        ulong clientId,
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
        ulong clientId,
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
        ulong clientId,
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
        ulong clientId,
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
        ulong clientId,
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
        ulong clientId,
        RegistryRoot root,
        string path,
        uint pathLength,
        SecurityDescriptorCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_SetRegistrySecurity",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int SetRegistrySecurity(
        ulong clientId,
        RegistryRoot root,
        string path,
        uint pathLength,
        string sddl,
        uint sddlLength,
        [MarshalAs(UnmanagedType.U1)] bool daclProtected,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_ExecuteRegistryStatus",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int ExecuteRegistryStatus(
        ulong clientId,
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
