using System.Runtime.InteropServices;

namespace KNSoft.ZPigeon.Server.Managed;

public sealed partial class NativeServer
{
    private static readonly NativeMethods.ExecutionSessionsCallback ExecutionSessionsCallback =
        CompleteExecutionSessions;
    private static readonly NativeMethods.ExecutionJobsCallback ExecutionJobsCallback = CompleteExecutionJobs;
    private static readonly NativeMethods.ExecutionStagingCallback ExecutionStagingCallback =
        CompleteExecutionStaging;

    public Task<ExecutionSession[]> EnumerateExecutionSessionsAsync() =>
        RunManagementAsync<ExecutionSession[]>(context =>
            NativeMethods.EnumerateExecutionSessions(ExecutionSessionsCallback, context));

    public async Task<ExecutionJob> StartExecutionAsync(ExecutionStart start)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(start.FileName);
        var jobs = await RunManagementAsync<ExecutionJob[]>(context => NativeMethods.StartExecution(
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
            ExecutionJobsCallback,
            context));
        return jobs.Length == 1 ? jobs[0] : throw new InvalidDataException("The execution response is invalid.");
    }

    public Task<ExecutionJob[]> EnumerateExecutionJobsAsync() =>
        RunManagementAsync<ExecutionJob[]>(context =>
            NativeMethods.EnumerateExecutionJobs(ExecutionJobsCallback, context));

    public Task TerminateExecutionAsync(uint jobId) =>
        RunStatusAsync((callback, context) => NativeMethods.TerminateExecution(jobId, callback, context));

    public Task<string> CreateExecutionStagingAsync(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        return RunManagementAsync<string>(context => NativeMethods.CreateExecutionStaging(
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
                Marshal.PtrToStringUni(record.StationName, (int)record.StationNameLength) ?? string.Empty,
                Marshal.PtrToStringUni(record.UserName, (int)record.UserNameLength) ?? string.Empty);
        }
        completion.SetResult(result);
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
                record.JobId.ToString(),
                record.ProcessId == 0 ? null : record.ProcessId,
                record.SessionId,
                (ExecutionEngine)record.Engine,
                (ExecutionIdentity)record.Identity,
                (ExecutionJobState)record.State,
                (ExecutionFlags)record.Flags,
                DateTime.FromFileTimeUtc((long)record.CreateTime),
                record.ExitTime == 0 ? null : DateTime.FromFileTimeUtc((long)record.ExitTime),
                record.State == (ushort)ExecutionJobState.Exited ? record.ExitCode : null,
                Marshal.PtrToStringUni(record.FileName, (int)record.FileNameLength) ?? string.Empty);
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
        completion.SetResult(Marshal.PtrToStringUni(path, (int)pathLength) ?? string.Empty);
    }
}

public enum ExecutionEngine : ushort
{
    CreateProcess = 1,
    ShellExecute
}

public enum ExecutionIdentity : ushort
{
    Current = 1,
    Interactive,
    Administrator,
    System,
    TrustedInstaller,
    OtherUser
}

[Flags]
public enum ExecutionFlags : uint
{
    None = 0,
    Hidden = 0x00000001,
    DeleteFile = 0x00000002
}

public enum ExecutionJobState : ushort
{
    Running = 1,
    Exited
}

public sealed record ExecutionSession(uint SessionId, uint State, uint Flags, string StationName, string UserName);

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
    string? Password);

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
        internal readonly ushort Engine;
        internal readonly ushort Identity;
        internal readonly ushort State;
        internal readonly nint FileName;
        internal readonly uint FileNameLength;
    }

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateExecutionSessions")]
    internal static partial int EnumerateExecutionSessions(ExecutionSessionsCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_StartExecution",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int StartExecution(
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
        ExecutionJobsCallback callback,
        nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_EnumerateExecutionJobs")]
    internal static partial int EnumerateExecutionJobs(ExecutionJobsCallback callback, nint context);

    [LibraryImport(Library, EntryPoint = "ZpNative_TerminateExecution")]
    internal static partial int TerminateExecution(uint jobId, StatusCallback callback, nint context);

    [LibraryImport(Library,
        EntryPoint = "ZpNative_CreateExecutionStaging",
        StringMarshalling = StringMarshalling.Utf16)]
    internal static partial int CreateExecutionStaging(
        string name,
        uint nameLength,
        ExecutionStagingCallback callback,
        nint context);
}
