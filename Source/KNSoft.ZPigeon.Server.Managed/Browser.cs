using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.BrowserCallback BrowserCallback = CompleteBrowser;

    public Task<BrowserPage> EnumerateBrowsersAsync() =>
        RunManagementAsync<BrowserPage>(context =>
            NativeMethods.EnumerateBrowsers(BrowserCallback, context));

    public Task<BrowserPage> QueryBrowserAsync(
        BrowserType browser,
        BrowserKind kind,
        string profile,
        ulong cursor,
        uint limit = 100) =>
        RunManagementAsync<BrowserPage>(context => NativeMethods.QueryBrowser(
            browser,
            kind,
            profile,
            (uint)profile.Length,
            cursor,
            limit,
            BrowserCallback,
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
            result[index] = new BrowserRecord(
                (BrowserKind)record.Kind,
                (BrowserType)record.Browser,
                record.State,
                record.Flags,
                record.Id.ToString(),
                record.Time.ToString(),
                record.Value.ToString(),
                Marshal.PtrToStringUni(record.Identity, (int)record.IdentityLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.Name, (int)record.NameLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.Location, (int)record.LocationLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.Detail, (int)record.DetailLength) ?? string.Empty);
        }
        completion.SetResult(new BrowserPage(nextCursor.ToString(), result));
    }
}

public enum BrowserType : ushort
{
    Chrome = 1,
    Edge
}

public enum BrowserKind : ushort
{
    Browser = 1,
    Profile,
    History,
    Download,
    Bookmark,
    Setting,
    Extension,
    Cookie
}

public sealed record BrowserRecord(
    BrowserKind Kind,
    BrowserType Browser,
    uint State,
    uint Flags,
    string Id,
    string Time,
    string Value,
    string Identity,
    string Name,
    string Location,
    string Detail);

public sealed record BrowserPage(string NextCursor, BrowserRecord[] Records);

internal static partial class NativeMethods
{
    internal delegate void BrowserCallback(
        ZpStatus status,
        ulong nextCursor,
        nint records,
        uint recordCount,
        nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct BrowserRecord
    {
        internal readonly ushort Kind;
        internal readonly ushort Browser;
        internal readonly uint State;
        internal readonly uint Flags;
        internal readonly ulong Id;
        internal readonly ulong Time;
        internal readonly ulong Value;
        internal readonly nint Identity;
        internal readonly uint IdentityLength;
        internal readonly nint Name;
        internal readonly uint NameLength;
        internal readonly nint Location;
        internal readonly uint LocationLength;
        internal readonly nint Detail;
        internal readonly uint DetailLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateBrowsers")]
    internal static partial int EnumerateBrowsers(BrowserCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryBrowser",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryBrowser(
        BrowserType browser,
        BrowserKind kind,
        string profile,
        uint profileLength,
        ulong cursor,
        uint limit,
        BrowserCallback callback,
        nint context);
}
