using System.Globalization;
using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.BrowserCallback BrowserCallback = CompleteBrowser;
    private static readonly NativeMethods.BrowserProfileInspectionCallback BrowserProfileInspectionCallback =
        CompleteBrowserProfileInspection;
    private static readonly NativeMethods.BrowserDocumentCallback BrowserDocumentCallback =
        CompleteBrowserDocument;

    public Task<BrowserPage> EnumerateBrowsersAsync() =>
        RunManagementAsync<BrowserPage>(context =>
            NativeMethods.EnumerateBrowsers(ClientId, BrowserCallback, context));

    public Task<BrowserPage> QueryBrowserAsync(
        BrowserType browser,
        BrowserKind kind,
        string profile,
        ulong cursor,
        uint limit = 100) =>
        RunManagementAsync<BrowserPage>(context => NativeMethods.QueryBrowser(ClientId,
            browser,
            kind,
            profile,
            (uint)profile.Length,
            cursor,
            limit,
            BrowserCallback,
            context));

    public Task<BrowserProfileInspection> InspectBrowserProfileAsync(BrowserType browser, string profile) =>
        RunManagementAsync<BrowserProfileInspection>(context => NativeMethods.InspectBrowserProfile(ClientId,
            browser,
            profile,
            (uint)profile.Length,
            BrowserProfileInspectionCallback,
            context));

    public Task<BrowserDocumentPage> OpenBrowserDocumentAsync(
        BrowserType browser,
        BrowserKind kind,
        string profile) =>
        RunManagementAsync<BrowserDocumentPage>(context => NativeMethods.OpenBrowserDocument(ClientId,
            browser,
            kind,
            profile,
            (uint)profile.Length,
            BrowserDocumentCallback,
            context));

    public Task<BrowserDocumentPage> QueryBrowserDocumentNodeAsync(
        uint snapshotId,
        uint nodeId,
        uint cursor) =>
        RunManagementAsync<BrowserDocumentPage>(context => NativeMethods.QueryBrowserDocumentNode(ClientId,
            snapshotId,
            nodeId,
            cursor,
            100,
            BrowserDocumentCallback,
            context));

    public Task CloseBrowserDocumentAsync(uint snapshotId) =>
        RunStatusAsync((callback, context) => NativeMethods.CloseBrowserDocument(ClientId, snapshotId,
                                                                                  callback,
                                                                                  context));

    private static void CompleteBrowser(
        ZpStatus status,
        ulong nextCursor,
        nint records,
        uint recordCount,
        nint context)
    {
        var completion = GetCompletion<BrowserPage>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new BrowserRecord[recordCount];
        var size = Marshal.SizeOf<NativeMethods.BrowserRecord>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.BrowserRecord>(records + index * size);
            var kind = (BrowserKind)record.Kind;
            object? data = kind switch
            {
                BrowserKind.History => new BrowserHistoryData(
                    record.Data.History.LastVisitTime.ToString(CultureInfo.InvariantCulture),
                    record.Data.History.VisitCount,
                    record.Data.History.TypedCount),
                BrowserKind.Download => new BrowserDownloadData(
                    record.Data.Download.StartTime.ToString(CultureInfo.InvariantCulture),
                    record.Data.Download.EndTime.ToString(CultureInfo.InvariantCulture),
                    record.Data.Download.ReceivedBytes.ToString(CultureInfo.InvariantCulture),
                    record.Data.Download.TotalBytes.ToString(CultureInfo.InvariantCulture),
                    record.Data.Download.State,
                    record.Data.Download.InterruptReason),
                BrowserKind.Cookie => new BrowserCookieData(
                    record.Data.Cookie.CreationTime.ToString(CultureInfo.InvariantCulture),
                    record.Data.Cookie.ExpirationTime.ToString(CultureInfo.InvariantCulture),
                    record.Data.Cookie.LastAccessTime.ToString(CultureInfo.InvariantCulture),
                    record.Data.Cookie.SameSite,
                    record.Data.Cookie.Flags),
                BrowserKind.Password => new BrowserPasswordData(
                    record.Data.Password.CreationTime.ToString(CultureInfo.InvariantCulture),
                    record.Data.Password.Flags),
                _ => null
            };
            result[index] = new BrowserRecord(
                kind,
                (BrowserType)record.Browser,
                record.Id.ToString(CultureInfo.InvariantCulture),
                ReadString(record.Identity, record.IdentityLength),
                ReadString(record.Name, record.NameLength),
                ReadString(record.Location, record.LocationLength),
                ReadString(record.Detail, record.DetailLength),
                data);
        }
        completion.SetResult(new BrowserPage(nextCursor.ToString(CultureInfo.InvariantCulture), result));
    }

    private static void CompleteBrowserDocument(
        ZpStatus status,
        uint snapshotId,
        byte parentType,
        uint nextCursor,
        nint nodes,
        uint nodeCount,
        nint context)
    {
        var completion = GetCompletion<BrowserDocumentPage>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new BrowserDocumentNode[nodeCount];
        var size = Marshal.SizeOf<NativeMethods.BrowserDocumentNode>();
        for (var index = 0; index < result.Length; index++)
        {
            var node = Marshal.PtrToStructure<NativeMethods.BrowserDocumentNode>(nodes + index * size);
            result[index] = new(node.Id,
                                (BrowserDocumentType)node.Type,
                                (node.Flags & 1) != 0,
                                ReadString(node.Name, node.NameLength),
                                ReadString(node.Value, node.ValueLength));
        }
        completion.SetResult(new(snapshotId, (BrowserDocumentType)parentType, nextCursor, result));
    }

    private static void CompleteBrowserProfileInspection(ZpStatus status, nint inspection, nint context)
    {
        var completion = GetCompletion<BrowserProfileInspection>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = Marshal.PtrToStructure<NativeMethods.BrowserProfileInspection>(inspection);
        completion.SetResult(new(result.ProfileSize, result.AvailableSpace, result.BrowserRunning));
    }
}

public enum BrowserType : byte
{
    Chrome = 1,
    Edge
}

public enum BrowserKind : byte
{
    Browser = 1,
    Profile,
    History,
    Download,
    Bookmark,
    Setting,
    Extension,
    Cookie,
    Password
}

public sealed record BrowserRecord(
    BrowserKind Kind,
    BrowserType Browser,
    string Id,
    string Identity,
    string Name,
    string Location,
    string Detail,
    object? Data);

public sealed record BrowserHistoryData(string LastVisitTime, uint VisitCount, uint TypedCount);
public sealed record BrowserDownloadData(
    string StartTime,
    string EndTime,
    string ReceivedBytes,
    string TotalBytes,
    uint State,
    uint InterruptReason);
public sealed record BrowserCookieData(
    string CreationTime,
    string ExpirationTime,
    string LastAccessTime,
    uint SameSite,
    uint Flags);
public sealed record BrowserPasswordData(string CreationTime, uint Flags);

public sealed record BrowserPage(string NextCursor, BrowserRecord[] Records);
public sealed record BrowserProfileInspection(ulong ProfileSize, ulong AvailableSpace, bool BrowserRunning);
#pragma warning disable CA1720 // Names mirror the native protocol vocabulary.
public enum BrowserDocumentType : byte
{
    Object = 1,
    Array,
    String,
    Number,
    Boolean,
    Null
}
#pragma warning restore CA1720
public sealed record BrowserDocumentNode(
    uint Id,
    BrowserDocumentType Type,
    bool HasChildren,
    string Name,
    string Value);
public sealed record BrowserDocumentPage(
    uint SnapshotId,
    BrowserDocumentType ParentType,
    uint NextCursor,
    BrowserDocumentNode[] Nodes);

internal static partial class NativeMethods
{
    internal delegate void BrowserCallback(
        ZpStatus status,
        ulong nextCursor,
        nint records,
        uint recordCount,
        nint context);

    internal delegate void BrowserProfileInspectionCallback(ZpStatus status, nint inspection, nint context);

    internal delegate void BrowserDocumentCallback(
        ZpStatus status,
        uint snapshotId,
        byte parentType,
        uint nextCursor,
        nint nodes,
        uint nodeCount,
        nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct BrowserRecord
    {
        internal readonly byte Kind;
        internal readonly byte Browser;
        internal readonly ulong Id;
        internal readonly BrowserRecordData Data;
        internal readonly nint Identity;
        internal readonly uint IdentityLength;
        internal readonly nint Name;
        internal readonly uint NameLength;
        internal readonly nint Location;
        internal readonly uint LocationLength;
        internal readonly nint Detail;
        internal readonly uint DetailLength;
    }

    [StructLayout(LayoutKind.Explicit)]
    internal readonly struct BrowserRecordData
    {
        [FieldOffset(0)] internal readonly BrowserHistoryData History;
        [FieldOffset(0)] internal readonly BrowserDownloadData Download;
        [FieldOffset(0)] internal readonly BrowserCookieData Cookie;
        [FieldOffset(0)] internal readonly BrowserPasswordData Password;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct BrowserHistoryData
    {
        internal readonly ulong LastVisitTime;
        internal readonly uint VisitCount;
        internal readonly uint TypedCount;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct BrowserDownloadData
    {
        internal readonly ulong StartTime;
        internal readonly ulong EndTime;
        internal readonly ulong ReceivedBytes;
        internal readonly ulong TotalBytes;
        internal readonly uint State;
        internal readonly uint InterruptReason;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct BrowserCookieData
    {
        internal readonly ulong CreationTime;
        internal readonly ulong ExpirationTime;
        internal readonly ulong LastAccessTime;
        internal readonly uint SameSite;
        internal readonly uint Flags;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct BrowserPasswordData
    {
        internal readonly ulong CreationTime;
        internal readonly uint Flags;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct BrowserProfileInspection
    {
        internal readonly ulong ProfileSize;
        internal readonly ulong AvailableSpace;
        [MarshalAs(UnmanagedType.U1)] internal readonly bool BrowserRunning;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct BrowserDocumentNode
    {
        internal readonly uint Id;
        internal readonly byte Type;
        internal readonly byte Flags;
        internal readonly nint Name;
        internal readonly uint NameLength;
        internal readonly nint Value;
        internal readonly uint ValueLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateBrowsers")]
    internal static partial int EnumerateBrowsers(ulong clientId, BrowserCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryBrowser",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryBrowser(
        ulong clientId,
        BrowserType browser,
        BrowserKind kind,
        string profile,
        uint profileLength,
        ulong cursor,
        uint limit,
        BrowserCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_InspectBrowserProfile",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int InspectBrowserProfile(
        ulong clientId,
        BrowserType browser,
        string profile,
        uint profileLength,
        BrowserProfileInspectionCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_OpenBrowserDocument",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int OpenBrowserDocument(
        ulong clientId,
        BrowserType browser,
        BrowserKind kind,
        string profile,
        uint profileLength,
        BrowserDocumentCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryBrowserDocumentNode")]
    internal static partial int QueryBrowserDocumentNode(
        ulong clientId,
        uint snapshotId,
        uint nodeId,
        uint cursor,
        uint limit,
        BrowserDocumentCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_CloseBrowserDocument")]
    internal static partial int CloseBrowserDocument(
        ulong clientId,
        uint snapshotId,
        StatusCallback callback,
        nint context);
}
