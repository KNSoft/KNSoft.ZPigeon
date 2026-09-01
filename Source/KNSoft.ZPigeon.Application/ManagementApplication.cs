using KNSoft.ZPigeon.Server.Managed;
using System.Globalization;

namespace KNSoft.ZPigeon.Application;

public sealed partial class ZPigeonApplication
{
    public async Task<LimitedResult<ProcessSummary>> GetProcessesAsync(
        ulong clientId,
        string? query,
        int limit,
        CancellationToken cancellationToken = default)
    {
        ValidateOptionalText(query, 256, nameof(query));
        var values = await RunAsync(clientId, server.EnumerateProcessesAsync, cancellationToken)
            .ConfigureAwait(false);
        var filtered = string.IsNullOrWhiteSpace(query) ? values : values.Where(value =>
            Contains(value.ImageName, query) ||
            Contains(value.ImagePath, query) ||
            Contains(value.UserName, query) ||
            value.ServiceNames.Any(name => Contains(name, query)));
        return Limit(filtered.Select(value => new ProcessSummary(
            value.ProcessId,
            value.ParentProcessId,
            value.SessionId,
            value.ThreadCount,
            value.HandleCount,
            value.Flags,
            value.MachineType,
            value.PriorityClass,
            value.CreateTime.ToString(CultureInfo.InvariantCulture),
            value.UserTime.ToString(CultureInfo.InvariantCulture),
            value.KernelTime.ToString(CultureInfo.InvariantCulture),
            value.WorkingSetBytes.ToString(CultureInfo.InvariantCulture),
            value.PrivateBytes.ToString(CultureInfo.InvariantCulture),
            value.ImageName,
            value.UserName,
            value.ImagePath,
            value.ServiceNames)), limit);
    }

    public Task<ProcessInfo> GetProcessAsync(
        ulong clientId,
        uint processId,
        ulong createTime,
        CancellationToken cancellationToken = default) =>
        RunAsync(clientId,
                 () => server.QueryProcessAsync(processId, createTime),
                 cancellationToken);

    public async Task<LimitedResult<ProcessHandle>> GetProcessHandlesAsync(
        ulong clientId,
        uint processId,
        ulong createTime,
        string? query,
        int limit,
        CancellationToken cancellationToken = default)
    {
        ValidateOptionalText(query, 256, nameof(query));
        var values = await RunAsync(clientId,
                                    () => server.EnumerateProcessHandlesAsync(processId, createTime),
                                    cancellationToken)
            .ConfigureAwait(false);
        return Limit(string.IsNullOrWhiteSpace(query) ? values : values.Where(value =>
            Contains(value.HandleValue, query) ||
            Contains(value.TypeName, query) ||
            Contains(value.ObjectName, query)), limit);
    }

    public Task ControlProcessAsync(
        ulong clientId,
        uint processId,
        ulong createTime,
        ProcessControl control,
        uint value = 0,
        CancellationToken cancellationToken = default)
    {
        if (control is not (ProcessControl.Terminate or
                            ProcessControl.TerminateTree or
                            ProcessControl.Suspend or
                            ProcessControl.Resume or
                            ProcessControl.EfficiencyMode or
                            ProcessControl.Priority or
                            ProcessControl.UacVirtualization))
        {
            throw new ArgumentOutOfRangeException(nameof(control));
        }
        return RunAsync(clientId,
                        () => server.ControlProcessAsync(processId, createTime, control, value),
                        cancellationToken);
    }

    public async Task<LimitedResult<ServiceRecord>> GetServicesAsync(
        ulong clientId,
        string? query,
        int limit,
        CancellationToken cancellationToken = default)
    {
        ValidateOptionalText(query, 256, nameof(query));
        var values = await RunAsync(clientId, server.EnumerateServicesAsync, cancellationToken)
            .ConfigureAwait(false);
        return Limit(string.IsNullOrWhiteSpace(query) ? values : values.Where(value =>
            Contains(value.ServiceName, query) ||
            Contains(value.DisplayName, query) ||
            Contains(value.Description, query) ||
            Contains(value.StartName, query)), limit);
    }

    public Task<ServiceInfo> GetServiceAsync(
        ulong clientId,
        string serviceName,
        CancellationToken cancellationToken = default)
    {
        ValidateRequiredText(serviceName, 256, nameof(serviceName));
        return RunAsync(clientId, () => server.QueryServiceAsync(serviceName), cancellationToken);
    }

    public Task ControlServiceAsync(
        ulong clientId,
        string serviceName,
        ServiceControl control,
        string? argument,
        CancellationToken cancellationToken = default)
    {
        ValidateRequiredText(serviceName, 256, nameof(serviceName));
        ValidateOptionalText(argument, 32767, nameof(argument));
        if (control is not (ServiceControl.Start or
                            ServiceControl.Stop or
                            ServiceControl.Pause or
                            ServiceControl.Continue or
                            ServiceControl.Restart))
        {
            throw new ArgumentOutOfRangeException(nameof(control));
        }
        return RunAsync(clientId,
                        () => server.ControlServiceAsync(serviceName, control, argument),
                        cancellationToken);
    }

    public async Task<LimitedResult<WindowRecord>> GetWindowsAsync(
        ulong clientId,
        string? query,
        int limit,
        CancellationToken cancellationToken = default)
    {
        ValidateOptionalText(query, 256, nameof(query));
        var values = await RunAsync(clientId, server.EnumerateWindowsAsync, cancellationToken)
            .ConfigureAwait(false);
        return Limit(string.IsNullOrWhiteSpace(query) ? values : values.Where(value =>
            Contains(value.Caption, query) || Contains(value.ClassName, query)), limit);
    }

    public Task<FilePage> GetFilesAsync(
        ulong clientId,
        string? path,
        uint enumerationId,
        CancellationToken cancellationToken = default)
    {
        ValidateOptionalText(path, 32767, nameof(path));
        return RunAsync(clientId,
                        () => server.EnumerateFilesPageAsync(path, enumerationId),
                        cancellationToken);
    }

    public Task<KNSoft.ZPigeon.Server.Managed.FileInfo> GetFileAsync(
        ulong clientId,
        string path,
        CancellationToken cancellationToken = default)
    {
        ValidateRequiredText(path, 32767, nameof(path));
        return RunAsync(clientId, () => server.QueryFileAsync(path), cancellationToken);
    }

    public Task<FileHash> HashFileAsync(
        ulong clientId,
        string path,
        FileHashAlgorithm algorithm,
        CancellationToken cancellationToken = default)
    {
        ValidateRequiredText(path, 32767, nameof(path));
        if (algorithm is not (FileHashAlgorithm.Crc32 or
                              FileHashAlgorithm.Md5 or
                              FileHashAlgorithm.Sha1 or
                              FileHashAlgorithm.Sha256))
        {
            throw new ArgumentOutOfRangeException(nameof(algorithm));
        }
        return RunAsync(clientId, () => server.HashFileAsync(path, algorithm), cancellationToken);
    }

    public Task DeleteFileAsync(
        ulong clientId,
        string path,
        CancellationToken cancellationToken = default)
    {
        ValidateRequiredText(path, 32767, nameof(path));
        return RunAsync(clientId, () => server.DeleteFileAsync(path), cancellationToken);
    }

    public Task RenameFileAsync(
        ulong clientId,
        string path,
        string newPath,
        CancellationToken cancellationToken = default)
    {
        ValidateRequiredText(path, 32767, nameof(path));
        ValidateRequiredText(newPath, 32767, nameof(newPath));
        return RunAsync(clientId, () => server.RenameFileAsync(path, newPath), cancellationToken);
    }

    public Task<ExecutionJob> StartProcessAsync(
        ulong clientId,
        string fileName,
        string? arguments,
        string? workingDirectory,
        CancellationToken cancellationToken = default)
    {
        ValidateRequiredText(fileName, 32767, nameof(fileName));
        ValidateOptionalText(arguments, 32767, nameof(arguments));
        ValidateOptionalText(workingDirectory, 32767, nameof(workingDirectory));
        var start = new ExecutionStart(ExecutionEngine.CreateProcess,
                                       ExecutionIdentity.Current,
                                       uint.MaxValue,
                                       ExecutionFlags.Hidden | ExecutionFlags.JobObject,
                                       fileName,
                                       arguments,
                                       workingDirectory,
                                       null,
                                       null,
                                       null,
                                       null);
        return RunAsync(clientId, () => server.StartExecutionAsync(start), cancellationToken);
    }

    public Task<ExecutionJob[]> GetExecutionJobsAsync(
        ulong clientId,
        CancellationToken cancellationToken = default) =>
        RunAsync(clientId, server.EnumerateExecutionJobsAsync, cancellationToken);

    public Task TerminateExecutionAsync(
        ulong clientId,
        uint jobId,
        CancellationToken cancellationToken = default) =>
        RunAsync(clientId, () => server.TerminateExecutionAsync(jobId), cancellationToken);
}

public sealed record ProcessSummary(
    uint ProcessId,
    uint ParentProcessId,
    uint SessionId,
    uint ThreadCount,
    uint HandleCount,
    uint Flags,
    ushort MachineType,
    byte PriorityClass,
    string CreateTime,
    string UserTime,
    string KernelTime,
    string WorkingSetBytes,
    string PrivateBytes,
    string ImageName,
    string UserName,
    string ImagePath,
    string[] ServiceNames);
