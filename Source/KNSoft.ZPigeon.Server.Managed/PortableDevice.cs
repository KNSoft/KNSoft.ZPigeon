using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

[Flags]
public enum PortableObjectFlags : uint
{
    Folder = 1,
    Storage = 2,
    CanDelete = 4
}

public sealed record PortableDevice(string Id, string Name, string Manufacturer, string Model);

public sealed record PortableObject(
    ulong Size,
    ulong ModifiedTime,
    ulong Capacity,
    ulong FreeSpace,
    PortableObjectFlags Flags,
    string Id,
    string PersistentId,
    string Name);

public sealed record PortableObjectPage(PortableObject[] Objects, uint NextOffset);

public sealed partial class NativeServer
{
    private static readonly NativeMethods.PortableDevicesCallback PortableDevicesCallback = CompletePortableDevices;
    private static readonly NativeMethods.PortableObjectsCallback PortableObjectsCallback = CompletePortableObjects;

    public Task<PortableDevice[]> EnumeratePortableDevicesAsync() =>
        RunManagementAsync<PortableDevice[]>(context =>
            NativeMethods.EnumeratePortableDevices(PortableDevicesCallback, context));

    public Task<PortableObjectPage> EnumeratePortableObjectsAsync(string deviceId, string? parentId, uint offset)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(deviceId);
        return RunManagementAsync<PortableObjectPage>(context =>
            NativeMethods.EnumeratePortableObjects(deviceId,
                                                    (uint)deviceId.Length,
                                                    parentId,
                                                    (uint)(parentId?.Length ?? 0),
                                                    offset,
                                                    PortableObjectsCallback,
                                                    context));
    }

    public Task CreatePortableFolderAsync(string deviceId, string parentId, string name) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.CreatePortableFolder(deviceId,
                                                (uint)deviceId.Length,
                                                parentId,
                                                (uint)parentId.Length,
                                                name,
                                                (uint)name.Length,
                                                callback,
                                                context));

    public Task DeletePortableObjectAsync(string deviceId, string objectId) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.DeletePortableObject(deviceId,
                                                (uint)deviceId.Length,
                                                objectId,
                                                (uint)objectId.Length,
                                                callback,
                                                context));

    public Task RenamePortableObjectAsync(string deviceId, string objectId, string name) =>
        RunStatusAsync((callback, context) =>
            NativeMethods.RenamePortableObject(deviceId,
                                                (uint)deviceId.Length,
                                                objectId,
                                                (uint)objectId.Length,
                                                name,
                                                (uint)name.Length,
                                                callback,
                                                context));

    public Task<FileTransfer> OpenPortableReadAsync(string deviceId, string objectId) =>
        OpenPortableTransferAsync(deviceId, objectId, null, 0, false);

    public Task<FileTransfer> OpenPortableWriteAsync(string deviceId, string parentId, string name, ulong fileSize) =>
        OpenPortableTransferAsync(deviceId, parentId, name, fileSize, true);

    private static Task<FileTransfer> OpenPortableTransferAsync(
        string deviceId,
        string objectId,
        string? name,
        ulong fileSize,
        bool write)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(deviceId);
        ArgumentException.ThrowIfNullOrWhiteSpace(objectId);
        if (write) ArgumentException.ThrowIfNullOrWhiteSpace(name);
        var creation = new FileTransferCreation(write);
        creation.Handle = GCHandle.Alloc(creation);
        var context = GCHandle.ToIntPtr(creation.Handle);
        var status = write ?
            NativeMethods.OpenPortableWrite(deviceId,
                                            (uint)deviceId.Length,
                                            objectId,
                                            (uint)objectId.Length,
                                            name!,
                                            (uint)name!.Length,
                                            fileSize,
                                            FileOpenCallback,
                                            FileWritableCallback,
                                            FileCloseCallback,
                                            context) :
            NativeMethods.OpenPortableRead(deviceId,
                                           (uint)deviceId.Length,
                                           objectId,
                                           (uint)objectId.Length,
                                           FileOpenCallback,
                                           FileDataCallback,
                                           FileCloseCallback,
                                           context);
        if (status < 0)
        {
            creation.Handle.Free();
            ThrowIfFailed(status);
        }
        return creation.Completion.Task;
    }

    private static void CompletePortableDevices(ZpStatus status, nint records, uint count, nint context)
    {
        var completion = GetCompletion<PortableDevice[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var devices = new PortableDevice[count];
        var size = Marshal.SizeOf<NativeMethods.PortableDeviceRecord>();
        for (var index = 0; index < devices.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.PortableDeviceRecord>(records + index * size);
            devices[index] = new PortableDevice(ToString(record.Id, record.IdLength),
                                                ToString(record.Name, record.NameLength),
                                                ToString(record.Manufacturer, record.ManufacturerLength),
                                                ToString(record.Model, record.ModelLength));
        }
        completion.SetResult(devices);
    }

    private static void CompletePortableObjects(
        ZpStatus status,
        nint records,
        uint count,
        uint nextOffset,
        nint context)
    {
        var completion = GetCompletion<PortableObjectPage>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var objects = new PortableObject[count];
        var size = Marshal.SizeOf<NativeMethods.PortableObjectRecord>();
        for (var index = 0; index < objects.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.PortableObjectRecord>(records + index * size);
            objects[index] = new PortableObject(record.Size,
                                                record.ModifiedTime,
                                                record.Capacity,
                                                record.FreeSpace,
                                                record.Flags,
                                                ToString(record.Id, record.IdLength),
                                                ToString(record.PersistentId, record.PersistentIdLength),
                                                ToString(record.Name, record.NameLength));
        }
        completion.SetResult(new PortableObjectPage(objects, nextOffset));
    }

    private static string ToString(nint value, uint length) =>
        Marshal.PtrToStringUni(value, (int)length) ?? string.Empty;
}

internal static partial class NativeMethods
{
    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct PortableDeviceRecord
    {
        internal readonly nint Id;
        internal readonly uint IdLength;
        internal readonly nint Name;
        internal readonly uint NameLength;
        internal readonly nint Manufacturer;
        internal readonly uint ManufacturerLength;
        internal readonly nint Model;
        internal readonly uint ModelLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct PortableObjectRecord
    {
        internal readonly ulong Size;
        internal readonly ulong ModifiedTime;
        internal readonly ulong Capacity;
        internal readonly ulong FreeSpace;
        internal readonly PortableObjectFlags Flags;
        internal readonly nint Id;
        internal readonly uint IdLength;
        internal readonly nint PersistentId;
        internal readonly uint PersistentIdLength;
        internal readonly nint Name;
        internal readonly uint NameLength;
    }

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void PortableDevicesCallback(ZpStatus status, nint records, uint count, nint context);

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void PortableObjectsCallback(
        ZpStatus status,
        nint records,
        uint count,
        uint nextOffset,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumeratePortableDevices")]
    internal static partial int EnumeratePortableDevices(PortableDevicesCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_EnumeratePortableObjects",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int EnumeratePortableObjects(
        string deviceId,
        uint deviceIdLength,
        string? parentId,
        uint parentIdLength,
        uint offset,
        PortableObjectsCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_CreatePortableFolder",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int CreatePortableFolder(
        string deviceId,
        uint deviceIdLength,
        string parentId,
        uint parentIdLength,
        string name,
        uint nameLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_DeletePortableObject",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int DeletePortableObject(
        string deviceId,
        uint deviceIdLength,
        string objectId,
        uint objectIdLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_RenamePortableObject",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int RenamePortableObject(
        string deviceId,
        uint deviceIdLength,
        string objectId,
        uint objectIdLength,
        string name,
        uint nameLength,
        StatusCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenPortableRead",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenPortableRead(
        string deviceId,
        uint deviceIdLength,
        string objectId,
        uint objectIdLength,
        FileOpenCallback openCallback,
        FileDataCallback dataCallback,
        FileCloseCallback closeCallback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenPortableWrite",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenPortableWrite(
        string deviceId,
        uint deviceIdLength,
        string parentId,
        uint parentIdLength,
        string name,
        uint nameLength,
        ulong fileSize,
        FileOpenCallback openCallback,
        FileWritableCallback writableCallback,
        FileCloseCallback closeCallback,
        nint context);
}
