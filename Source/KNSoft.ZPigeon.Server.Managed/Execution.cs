using System.Globalization;
using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.ExecutionSessionsCallback ExecutionSessionsCallback =
        CompleteExecutionSessions;
    private static readonly NativeMethods.ExecutionEnvironmentCallback ExecutionEnvironmentCallback =
        CompleteExecutionEnvironment;
    private static readonly NativeMethods.ExecutionImageCallback ExecutionImageCallback = CompleteExecutionImage;
    private static readonly NativeMethods.ExecutionJobsCallback ExecutionJobsCallback = CompleteExecutionJobs;
    private static readonly NativeMethods.ExecutionStagingCallback ExecutionStagingCallback =
        CompleteExecutionStaging;

    public Task<ExecutionSession[]> EnumerateExecutionSessionsAsync() =>
        RunManagementAsync<ExecutionSession[]>(context =>
            NativeMethods.EnumerateExecutionSessions(ClientId, ExecutionSessionsCallback, context));

    public Task<ExecutionEnvironment> QueryExecutionEnvironmentAsync() =>
        RunManagementAsync<ExecutionEnvironment>(context =>
            NativeMethods.QueryExecutionEnvironment(ClientId, ExecutionEnvironmentCallback, context));

    public Task<ExecutionImageInfo> QueryExecutionImageAsync(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);
        return RunManagementAsync<ExecutionImageInfo>(context => NativeMethods.QueryExecutionImage(ClientId,
            path,
            (uint)path.Length,
            ExecutionImageCallback,
            context));
    }

    public unsafe Task<ExecutionJob> StartExecutionAsync(ExecutionStart start)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(start.FileName);
        Task<ExecutionJob[]> task;
        fixed (byte* customToken = start.CustomToken)
        {
            var customTokenAddress = (nint)customToken;
            task = RunManagementAsync<ExecutionJob[]>(context => NativeMethods.StartExecution(ClientId,
                (ushort)start.Engine,
                (ushort)start.Identity,
                start.SessionId,
                (uint)start.Flags,
                start.FileName,
                (uint)start.FileName.Length,
                start.Arguments,
                (uint)(start.Arguments?.Length ?? 0),
                start.WorkingDirectory,
                (uint)(start.WorkingDirectory?.Length ?? 0),
                start.Verb,
                (uint)(start.Verb?.Length ?? 0),
                start.UserName,
                (uint)(start.UserName?.Length ?? 0),
                start.Password,
                (uint)(start.Password?.Length ?? 0),
                start.AppContainerSid,
                (uint)(start.AppContainerSid?.Length ?? 0),
                (byte*)customTokenAddress,
                (uint)(start.CustomToken?.Length ?? 0),
                ExecutionJobsCallback,
                context));
        }
        return CompleteStartExecutionAsync(task);
    }

    private static async Task<ExecutionJob> CompleteStartExecutionAsync(Task<ExecutionJob[]> task)
    {
        var jobs = await task;
        return jobs.Length == 1 ? jobs[0] : throw new InvalidDataException("The execution response is invalid.");
    }

    public Task<ExecutionJob[]> EnumerateExecutionJobsAsync() =>
        RunManagementAsync<ExecutionJob[]>(context =>
            NativeMethods.EnumerateExecutionJobs(ClientId, ExecutionJobsCallback, context));

    public Task TerminateExecutionAsync(uint jobId) =>
        RunStatusAsync((callback, context) => NativeMethods.TerminateExecution(ClientId, jobId, callback, context));

    public Task<string> CreateExecutionStagingAsync(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        return RunManagementAsync<string>(context => NativeMethods.CreateExecutionStaging(ClientId,
            name,
            (uint)name.Length,
            ExecutionStagingCallback,
            context));
    }

    private static void CompleteExecutionSessions(ZpStatus status, nint records, uint count, nint context)
    {
        var completion = GetCompletion<ExecutionSession[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new ExecutionSession[count];
        var size = Marshal.SizeOf<NativeMethods.ExecutionSession>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.ExecutionSession>(records + index * size);
            result[index] = new ExecutionSession(
                record.SessionId,
                record.State,
                record.Flags,
                ReadString(record.StationName, record.StationNameLength),
                ReadString(record.UserName, record.UserNameLength));
        }
        completion.SetResult(result);
    }

    private static void CompleteExecutionEnvironment(
        ZpStatus status,
        uint flags,
        nint records,
        uint count,
        nint context)
    {
        var completion = GetCompletion<ExecutionEnvironment>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new ExecutionRuntime[count];
        var size = Marshal.SizeOf<NativeMethods.ExecutionRuntime>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.ExecutionRuntime>(records + index * size);
            result[index] = new ExecutionRuntime(
                (ExecutionRuntimeKind)record.Kind,
                new ExecutionImageInfo(record.Machine,
                                       record.Subsystem,
                                       new Version(record.Version0,
                                                   record.Version1,
                                                   record.Version2,
                                                   record.Version3)),
                ReadString(record.Path, record.PathLength));
        }
        completion.SetResult(new ExecutionEnvironment(
            (ExecutionEnvironmentFlags)flags,
            result));
    }

    private static void CompleteExecutionImage(
        ZpStatus status,
        ushort machine,
        ushort subsystem,
        nint version,
        nint context)
    {
        var completion = GetCompletion<ExecutionImageInfo>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(new ExecutionImageInfo(
            machine,
            subsystem,
            new Version((ushort)Marshal.ReadInt16(version),
                        (ushort)Marshal.ReadInt16(version, sizeof(ushort)),
                        (ushort)Marshal.ReadInt16(version, 2 * sizeof(ushort)),
                        (ushort)Marshal.ReadInt16(version, 3 * sizeof(ushort)))));
    }

    private static void CompleteExecutionJobs(ZpStatus status, nint records, uint count, nint context)
    {
        var completion = GetCompletion<ExecutionJob[]>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        var result = new ExecutionJob[count];
        var size = Marshal.SizeOf<NativeMethods.ExecutionJob>();
        for (var index = 0; index < result.Length; index++)
        {
            var record = Marshal.PtrToStructure<NativeMethods.ExecutionJob>(records + index * size);
            result[index] = new ExecutionJob(
                record.JobId.ToString(CultureInfo.InvariantCulture),
                record.ProcessId == 0 ? null : record.ProcessId,
                record.SessionId,
                (ExecutionEngine)record.Engine,
                (ExecutionIdentity)record.Identity,
                (ExecutionJobState)record.State,
                (ExecutionFlags)record.Flags,
                DateTime.FromFileTimeUtc((long)record.CreateTime),
                record.ExitTime == 0 ? null : DateTime.FromFileTimeUtc((long)record.ExitTime),
                record.State == (ushort)ExecutionJobState.Exited ? record.ExitCode : null,
                ReadString(record.FileName, record.FileNameLength));
        }
        completion.SetResult(result);
    }

    private static void CompleteExecutionStaging(ZpStatus status, nint path, uint pathLength, nint context)
    {
        var completion = GetCompletion<string>(context);
        if (!status.IsSuccess)
        {
            completion.SetException(new NativeException(status));
            return;
        }
        completion.SetResult(ReadString(path, pathLength));
    }
}

public enum ExecutionEngine : byte
{
    CreateProcess = 1,
    ShellExecute
}

public enum ExecutionIdentity : byte
{
    Current = 1,
    Interactive,
    Administrator,
    System,
    TrustedInstaller,
    OtherUser,
    AppContainer,
    CustomToken
}

public enum ExecutionRuntimeKind : byte
{
    CommandPrompt = 1,
    WindowsPowerShell,
    PowerShell,
    ConsoleScriptHost,
    WindowsScriptHost,
    HtmlApplication,
    Node,
    Python,
    PythonWindow,
    Go
}

[Flags]
public enum ExecutionEnvironmentFlags : uint
{
    None = 0,
    Administrator = 0x00000001
}

[Flags]
public enum ExecutionFlags : uint
{
    None = 0,
    Hidden = 0x00000001,
    DeleteFile = 0x00000002,
    JobObject = 0x00000004
}

public enum ExecutionJobState : byte
{
    Running = 1,
    Exited
}

public sealed record ExecutionSession(uint SessionId, uint State, uint Flags, string StationName, string UserName);

public sealed record ExecutionImageInfo(ushort Machine, ushort Subsystem, Version Version);
public sealed record ExecutionRuntime(ExecutionRuntimeKind Kind, ExecutionImageInfo Image, string Path);
public sealed record ExecutionEnvironment(ExecutionEnvironmentFlags Flags, ExecutionRuntime[] Runtimes);

public sealed record ExecutionStart(
    ExecutionEngine Engine,
    ExecutionIdentity Identity,
    uint SessionId,
    ExecutionFlags Flags,
    string FileName,
    string? Arguments,
    string? WorkingDirectory,
    string? Verb,
    string? UserName,
    string? Password,
    string? AppContainerSid,
    byte[]? CustomToken = null);

public sealed record ExecutionJob(
    string JobId,
    uint? ProcessId,
    uint SessionId,
    ExecutionEngine Engine,
    ExecutionIdentity Identity,
    ExecutionJobState State,
    ExecutionFlags Flags,
    DateTime CreateTime,
    DateTime? ExitTime,
    uint? ExitCode,
    string FileName);

internal static partial class NativeMethods
{
    internal delegate void ExecutionSessionsCallback(ZpStatus status, nint records, uint count, nint context);
    internal delegate void ExecutionEnvironmentCallback(
        ZpStatus status,
        uint flags,
        nint records,
        uint count,
        nint context);
    internal delegate void ExecutionImageCallback(
        ZpStatus status,
        ushort machine,
        ushort subsystem,
        nint version,
        nint context);
    internal delegate void ExecutionJobsCallback(ZpStatus status, nint records, uint count, nint context);
    internal delegate void ExecutionStagingCallback(ZpStatus status, nint path, uint pathLength, nint context);

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ExecutionSession
    {
        internal readonly uint SessionId;
        internal readonly uint State;
        internal readonly uint Flags;
        internal readonly nint StationName;
        internal readonly uint StationNameLength;
        internal readonly nint UserName;
        internal readonly uint UserNameLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ExecutionJob
    {
        internal readonly uint JobId;
        internal readonly ulong CreateTime;
        internal readonly ulong ExitTime;
        internal readonly uint ProcessId;
        internal readonly uint SessionId;
        internal readonly uint ExitCode;
        internal readonly uint Flags;
        internal readonly byte Engine;
        internal readonly byte Identity;
        internal readonly byte State;
        internal readonly nint FileName;
        internal readonly uint FileNameLength;
    }

    [StructLayout(LayoutKind.Sequential)]
    internal readonly struct ExecutionRuntime
    {
        internal readonly byte Kind;
        internal readonly ushort Machine;
        internal readonly ushort Subsystem;
        internal readonly ushort Version0;
        internal readonly ushort Version1;
        internal readonly ushort Version2;
        internal readonly ushort Version3;
        internal readonly nint Path;
        internal readonly uint PathLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateExecutionSessions")]
    internal static partial int EnumerateExecutionSessions(
        ulong clientId,
        ExecutionSessionsCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_QueryExecutionEnvironment")]
    internal static partial int QueryExecutionEnvironment(
        ulong clientId,
        ExecutionEnvironmentCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_QueryExecutionImage",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int QueryExecutionImage(
        ulong clientId,
        string path,
        uint pathLength,
        ExecutionImageCallback callback,
        nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_StartExecution",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static unsafe partial int StartExecution(
        ulong clientId,
        ushort engine,
        ushort identity,
        uint sessionId,
        uint flags,
        string fileName,
        uint fileNameLength,
        string? arguments,
        uint argumentsLength,
        string? workingDirectory,
        uint workingDirectoryLength,
        string? verb,
        uint verbLength,
        string? userName,
        uint userNameLength,
        string? password,
        uint passwordLength,
        string? appContainerSid,
        uint appContainerSidLength,
        byte* customToken,
        uint customTokenLength,
        ExecutionJobsCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateExecutionJobs")]
    internal static partial int EnumerateExecutionJobs(ulong clientId, ExecutionJobsCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_TerminateExecution")]
    internal static partial int TerminateExecution(ulong clientId, uint jobId, StatusCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_CreateExecutionStaging",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int CreateExecutionStaging(
        ulong clientId,
        string name,
        uint nameLength,
        ExecutionStagingCallback callback,
        nint context);
}
