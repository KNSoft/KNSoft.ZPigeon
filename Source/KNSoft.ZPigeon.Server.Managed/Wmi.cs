using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.WmiCallback WmiCallback = CompleteWmi;

    public Task<WmiRow[]> EnumerateWmiNamespacesAsync(string wmiNamespace) =>
        RunManagementAsync<WmiRow[]>(context => NativeMethods.EnumerateWmiNamespaces(ClientId,
            wmiNamespace,
            (uint)wmiNamespace.Length,
            WmiCallback,
            context));

    public Task<WmiRow[]> EnumerateWmiClassesAsync(string wmiNamespace) =>
        RunManagementAsync<WmiRow[]>(context => NativeMethods.EnumerateWmiClasses(ClientId,
            wmiNamespace,
            (uint)wmiNamespace.Length,
            WmiCallback,
            context));

    public Task<WmiRow[]> QueryWmiAsync(
        string wmiNamespace,
        string query,
        uint limit,
        bool systemProperties) =>
        RunManagementAsync<WmiRow[]>(context => NativeMethods.QueryWmi(ClientId,
            wmiNamespace,
            (uint)wmiNamespace.Length,
            query,
            (uint)query.Length,
            limit,
            systemProperties ? 1U : 0U,
            WmiCallback,
            context));

    private static void CompleteWmi(
        ZpStatus status,
        nint rows,
        uint rowCount,
        nint context)
    {
        var completion = GetCompletion<WmiRow[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new WmiRow[rowCount];
        var rowSize = Marshal.SizeOf<NativeMethods.WmiRow>();
        var cellSize = Marshal.SizeOf<NativeMethods.WmiCell>();
        for (var rowIndex = 0; rowIndex < result.Length; rowIndex++)
        {
            var nativeRow = Marshal.PtrToStructure<NativeMethods.WmiRow>(rows + rowIndex * rowSize);
            var cells = new WmiCell[nativeRow.CellCount];
            for (var cellIndex = 0; cellIndex < cells.Length; cellIndex++)
            {
                var cell = Marshal.PtrToStructure<NativeMethods.WmiCell>(
                    nativeRow.Cells + cellIndex * cellSize);
                cells[cellIndex] = new WmiCell(
                    cell.Type,
                    ReadString(cell.Name, cell.NameLength),
                    ReadString(cell.Value, cell.ValueLength));
            }
            result[rowIndex] = new WmiRow(cells);
        }
        completion.SetResult(result);
    }
}

public sealed record WmiCell(uint Type, string Name, string Value);

public sealed record WmiRow(WmiCell[] Cells);

internal static partial class NativeMethods
{
    internal delegate void WmiCallback(
        ZpStatus status,
        nint rows,
        uint rowCount,
        nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct WmiCell
    {
        internal readonly uint Type;
        internal readonly nint Name;
        internal readonly uint NameLength;
        internal readonly nint Value;
        internal readonly uint ValueLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct WmiRow
    {
        internal readonly nint Cells;
        internal readonly uint CellCount;
    }

    [LibraryImport(Library,
        EntryPoint = "ZpNative_EnumerateWmiNamespaces",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int EnumerateWmiNamespaces(
        ulong clientId,
        string wmiNamespace,
        uint namespaceLength,
        WmiCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_EnumerateWmiClasses",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int EnumerateWmiClasses(
        ulong clientId,
        string wmiNamespace,
        uint namespaceLength,
        WmiCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryWmi",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryWmi(
        ulong clientId,
        string wmiNamespace,
        uint namespaceLength,
        string query,
        uint queryLength,
        uint limit,
        uint flags,
        WmiCallback callback,
        nint context);
}
