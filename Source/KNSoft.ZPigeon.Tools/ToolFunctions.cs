using System.ComponentModel;
using System.Globalization;
using KNSoft.ZPigeon.Application;
using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Tools;

internal sealed class ToolFunctions(ZPigeonApplication application)
{
    [ZPigeonTool("list_clients",
                 "List connected ZPigeon clients. Use the exact returned id for every other external tool.",
                 true,
                 audience: ToolAudience.ExternalMcp)]
    public ConnectedClientSummary[] ListClients() => application.GetClients();

    [ZPigeonTool("get_system_info", "Get operating system and hardware summary for a client.", true)]
    public Task<SystemInfo> GetSystemInfo(
        [Description("Exact client id returned by list_clients.")] string clientId,
        CancellationToken cancellationToken) =>
        application.GetSystemInfoAsync(ParseClientId(clientId), cancellationToken);

    [ZPigeonTool("list_event_log_channels", "List Windows Event Log channel paths on a client.", true)]
    public Task<string[]> ListEventLogChannels(
        [Description("Exact client id returned by list_clients.")] string clientId,
        CancellationToken cancellationToken) =>
        application.GetEventLogChannelsAsync(ParseClientId(clientId), cancellationToken);

    [ZPigeonTool("query_event_log",
                 "Read one bounded page of Windows Event Log XML records. Pass the returned bookmark to continue.",
                 true)]
    public Task<EventLogPage> QueryEventLog(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Event Log channel path, for example System.")] string channelPath,
        [Description("Optional XPath event query.")] string? query = null,
        [Description("Bookmark returned by an earlier page.")] string? bookmark = null,
        [Description("Number of records, from 1 through 200.")] uint limit = 50,
        CancellationToken cancellationToken = default) =>
        application.QueryEventLogAsync(ParseClientId(clientId),
                                       channelPath,
                                       query,
                                       bookmark,
                                       limit,
                                       cancellationToken);

    [ZPigeonTool("list_processes", "List or search processes. Results include stable process creation times.", true)]
    public Task<LimitedResult<ProcessSummary>> ListProcesses(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Optional image, path, user, or service substring.")] string? query = null,
        [Description("Maximum results, from 1 through 200.")] int limit = 50,
        CancellationToken cancellationToken = default) =>
        application.GetProcessesAsync(ParseClientId(clientId), query, limit, cancellationToken);

    [ZPigeonTool("get_process", "Get detailed information for one process instance.", true)]
    public Task<ProcessInfo> GetProcess(
        [Description("Exact client id returned by list_clients.")] string clientId,
        uint processId,
        [Description("Exact createTime returned by list_processes.")] string createTime,
        CancellationToken cancellationToken = default) =>
        application.GetProcessAsync(ParseClientId(clientId),
                                    processId,
                                    ParseUInt64(createTime, nameof(createTime)),
                                    cancellationToken);

    [ZPigeonTool("list_process_handles", "List handles owned by one process instance.", true)]
    public Task<LimitedResult<ProcessHandle>> ListProcessHandles(
        [Description("Exact client id returned by list_clients.")] string clientId,
        uint processId,
        [Description("Exact createTime returned by list_processes.")] string createTime,
        [Description("Optional handle value, type, or object-name substring.")] string? query = null,
        [Description("Maximum results, from 1 through 200.")] int limit = 100,
        CancellationToken cancellationToken = default) =>
        application.GetProcessHandlesAsync(ParseClientId(clientId),
                                           processId,
                                           ParseUInt64(createTime, nameof(createTime)),
                                           query,
                                           limit,
                                           cancellationToken);

    [ZPigeonTool("control_process",
                 "Control one process instance. control is Terminate, TerminateTree, Suspend, Resume, " +
                 "EfficiencyMode, Priority, or UacVirtualization.",
                 false,
                 destructive: true)]
    public async Task<ToolOperationResult> ControlProcess(
        [Description("Exact client id returned by list_clients.")] string clientId,
        uint processId,
        [Description("Exact createTime returned by list_processes.")] string createTime,
        string control,
        [Description("Control-specific numeric value; normally zero.")] uint value = 0,
        CancellationToken cancellationToken = default)
    {
        await application.ControlProcessAsync(ParseClientId(clientId),
                                              processId,
                                              ParseUInt64(createTime, nameof(createTime)),
                                              ParseEnum<ProcessControl>(control),
                                              value,
                                              cancellationToken);
        return new(true);
    }

    [ZPigeonTool("list_services", "List or search Windows services.", true)]
    public Task<LimitedResult<ServiceRecord>> ListServices(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Optional service, display-name, description, or account substring.")] string? query = null,
        [Description("Maximum results, from 1 through 200.")] int limit = 50,
        CancellationToken cancellationToken = default) =>
        application.GetServicesAsync(ParseClientId(clientId), query, limit, cancellationToken);

    [ZPigeonTool("get_service", "Get detailed configuration and state for a Windows service.", true)]
    public Task<ServiceInfo> GetService(
        [Description("Exact client id returned by list_clients.")] string clientId,
        string serviceName,
        CancellationToken cancellationToken = default) =>
        application.GetServiceAsync(ParseClientId(clientId), serviceName, cancellationToken);

    [ZPigeonTool("control_service",
                 "Control a Windows service. control is Start, Stop, Pause, Continue, or Restart.",
                 false,
                 destructive: true)]
    public async Task<ToolOperationResult> ControlService(
        [Description("Exact client id returned by list_clients.")] string clientId,
        string serviceName,
        string control,
        [Description("Optional start argument.")] string? argument = null,
        CancellationToken cancellationToken = default)
    {
        await application.ControlServiceAsync(ParseClientId(clientId),
                                              serviceName,
                                              ParseEnum<ServiceControl>(control),
                                              argument,
                                              cancellationToken);
        return new(true);
    }

    [ZPigeonTool("list_windows", "List or search top-level and child windows.", true)]
    public Task<LimitedResult<WindowRecord>> ListWindows(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Optional caption or class-name substring.")] string? query = null,
        [Description("Maximum results, from 1 through 200.")] int limit = 50,
        CancellationToken cancellationToken = default) =>
        application.GetWindowsAsync(ParseClientId(clientId), query, limit, cancellationToken);

    [ZPigeonTool("list_files", "List one page of files and directories at a remote path.", true)]
    public Task<FilePage> ListFiles(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Remote directory path. Omit for roots.")] string? path = null,
        [Description("Enumeration id returned by the preceding page, or 0 for the first page.")]
        string enumerationId = "0",
        CancellationToken cancellationToken = default) =>
        application.GetFilesAsync(ParseClientId(clientId),
                                  path,
                                  ParseUInt32(enumerationId, nameof(enumerationId), true),
                                  cancellationToken);

    [ZPigeonTool("get_file_info", "Get metadata for a remote file or directory.", true)]
    public Task<KNSoft.ZPigeon.Server.Managed.FileInfo> GetFileInfo(
        [Description("Exact client id returned by list_clients.")] string clientId,
        string path,
        CancellationToken cancellationToken = default) =>
        application.GetFileAsync(ParseClientId(clientId), path, cancellationToken);

    [ZPigeonTool("hash_file", "Hash a remote file with CRC32, MD5, SHA1, or SHA256.", true)]
    public Task<FileHash> HashFile(
        [Description("Exact client id returned by list_clients.")] string clientId,
        string path,
        [Description("CRC32, MD5, SHA1, or SHA256.")] string algorithm = "SHA256",
        CancellationToken cancellationToken = default) =>
        application.HashFileAsync(ParseClientId(clientId),
                                  path,
                                  ParseEnum<FileHashAlgorithm>(algorithm),
                                  cancellationToken);

    [ZPigeonTool("delete_file", "Delete a remote file or directory.", false, destructive: true)]
    public async Task<ToolOperationResult> DeleteFile(
        [Description("Exact client id returned by list_clients.")] string clientId,
        string path,
        CancellationToken cancellationToken = default)
    {
        await application.DeleteFileAsync(ParseClientId(clientId), path, cancellationToken);
        return new(true);
    }

    [ZPigeonTool("rename_file", "Rename or move a remote file or directory.", false, destructive: true)]
    public async Task<ToolOperationResult> RenameFile(
        [Description("Exact client id returned by list_clients.")] string clientId,
        string path,
        string newPath,
        CancellationToken cancellationToken = default)
    {
        await application.RenameFileAsync(ParseClientId(clientId), path, newPath, cancellationToken);
        return new(true);
    }

    [ZPigeonTool("query_wmi", "Run a bounded read-only WQL query on a client.", true)]
    public Task<WmiRow[]> QueryWmi(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("WMI namespace, for example ROOT\\CIMV2.")] string wmiNamespace,
        [Description("WQL SELECT query.")] string query,
        [Description("Maximum rows, from 1 through 200.")] uint limit = 50,
        bool includeSystemProperties = false,
        CancellationToken cancellationToken = default) =>
        application.QueryWmiAsync(ParseClientId(clientId),
                                  wmiNamespace,
                                  query,
                                  limit,
                                  includeSystemProperties,
                                  cancellationToken);

    [ZPigeonTool("list_registry_keys", "List one page of Windows Registry subkeys.", true)]
    public Task<RegistryPage<RegistryKeyRecord>> ListRegistryKeys(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("ClassesRoot, CurrentUser, LocalMachine, Users, or CurrentConfig.")] string root,
        string path,
        string? cursor = null,
        [Description("Maximum results, from 1 through 200.")] uint limit = 100,
        CancellationToken cancellationToken = default) =>
        application.GetRegistryKeysAsync(ParseClientId(clientId),
                                         ParseEnum<RegistryRoot>(root),
                                         path,
                                         cursor,
                                         limit,
                                         cancellationToken);

    [ZPigeonTool("list_registry_values", "List one page of Windows Registry values.", true)]
    public Task<RegistryPage<RegistryValueRecord>> ListRegistryValues(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("ClassesRoot, CurrentUser, LocalMachine, Users, or CurrentConfig.")] string root,
        string path,
        string? cursor = null,
        [Description("Maximum results, from 1 through 200.")] uint limit = 100,
        CancellationToken cancellationToken = default) =>
        application.GetRegistryValuesAsync(ParseClientId(clientId),
                                           ParseEnum<RegistryRoot>(root),
                                           path,
                                           cursor,
                                           limit,
                                           cancellationToken);

    [ZPigeonTool("get_registry_value", "Read the type and complete binary data of one Registry value.", true)]
    public Task<RegistryValue> GetRegistryValue(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("ClassesRoot, CurrentUser, LocalMachine, Users, or CurrentConfig.")] string root,
        string path,
        [Description("Value name; use an empty string for the default value.")] string name,
        CancellationToken cancellationToken = default) =>
        application.GetRegistryValueAsync(ParseClientId(clientId),
                                          ParseEnum<RegistryRoot>(root),
                                          path,
                                          name,
                                          cancellationToken);

    [ZPigeonTool("list_browsers", "List installed Chrome and Edge browsers and profiles.", true)]
    public Task<BrowserPage> ListBrowsers(
        [Description("Exact client id returned by list_clients.")] string clientId,
        CancellationToken cancellationToken = default) =>
        application.GetBrowsersAsync(ParseClientId(clientId), cancellationToken);

    [ZPigeonTool("query_browser_data",
                 "Read a bounded page of History, Download, Bookmark, Setting, or Extension data.",
                 true)]
    public Task<BrowserPage> QueryBrowserData(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Chrome or Edge.")] string browser,
        [Description("History, Download, Bookmark, Setting, or Extension.")] string kind,
        [Description("Exact profile identity returned by list_browsers.")] string profile,
        [Description("Optional non-default user-data directory.")] string? userData = null,
        [Description("Cursor returned by the preceding page, or 0 for the first page.")] string cursor = "0",
        [Description("Maximum results, from 1 through 200.")] uint limit = 50,
        CancellationToken cancellationToken = default) =>
        application.QueryBrowserAsync(ParseClientId(clientId),
                                      ParseEnum<BrowserType>(browser),
                                      ParseBrowserKind(kind, false),
                                      profile,
                                      userData,
                                      ParseUInt64(cursor, nameof(cursor)),
                                      limit,
                                      cancellationToken);

    [ZPigeonTool("query_browser_secrets",
                 "Read a bounded page of sensitive Cookie or Password data only when explicitly requested.",
                 true,
                 sensitive: true)]
    public Task<BrowserPage> QueryBrowserSecrets(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Chrome or Edge.")] string browser,
        [Description("Cookie or Password.")] string kind,
        [Description("Exact profile identity returned by list_browsers.")] string profile,
        [Description("Optional non-default user-data directory.")] string? userData = null,
        [Description("Cursor returned by the preceding page, or 0 for the first page.")] string cursor = "0",
        [Description("Maximum results, from 1 through 200.")] uint limit = 50,
        CancellationToken cancellationToken = default) =>
        application.QueryBrowserAsync(ParseClientId(clientId),
                                      ParseEnum<BrowserType>(browser),
                                      ParseBrowserKind(kind, true),
                                      profile,
                                      userData,
                                      ParseUInt64(cursor, nameof(cursor)),
                                      limit,
                                      cancellationToken);

    [ZPigeonTool("list_administration_operations",
                 "List the explicitly exposed enumeration and query names plus valid control-action pairs.",
                 true)]
    public AdministrationCapabilities ListAdministrationOperations() =>
        ZPigeonApplication.GetAdministrationCapabilities();

    [ZPigeonTool("list_administration",
                 "Run an Enumerate* administration operation, covering users, software, devices, updates, tasks, " +
                 "networking, security, storage, and other Windows management areas.",
                 true)]
    public Task<LimitedResult<AdministrationRecord>> ListAdministration(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Exact enumeration name returned by list_administration_operations.")] string operation,
        [Description("Optional value, identity, name, description, or detail substring.")] string? query = null,
        [Description("Maximum results, from 1 through 200.")] int limit = 50,
        CancellationToken cancellationToken = default) =>
        application.GetAdministrationAsync(ParseClientId(clientId),
                                           ParseEnum<AdministrationOperation>(operation),
                                           query,
                                           limit,
                                           cancellationToken);

    [ZPigeonTool("query_administration",
                 "Run a Query* administration operation for one exact identity.",
                 true)]
    public Task<LimitedResult<AdministrationRecord>> QueryAdministration(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Exact query name returned by list_administration_operations.")] string operation,
        string identity,
        [Description("Maximum results, from 1 through 200.")] int limit = 50,
        CancellationToken cancellationToken = default) =>
        application.QueryAdministrationAsync(ParseClientId(clientId),
                                             ParseEnum<AdministrationOperation>(operation),
                                             identity,
                                             limit,
                                             cancellationToken);

    [ZPigeonTool("control_administration",
                 "Apply a Control* administration operation. Obtain exact operation and action names first.",
                 false,
                 destructive: true)]
    public async Task<ToolOperationResult> ControlAdministration(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Exact control name returned by list_administration_operations.")] string operation,
        [Description("Exact action name returned by list_administration_operations.")] string action,
        string? identity = null,
        string? argument = null,
        [Description("Optional secret such as a password; never place it in another argument.")] string? secret = null,
        CancellationToken cancellationToken = default)
    {
        await application.ControlAdministrationAsync(ParseClientId(clientId),
                                                     ParseEnum<AdministrationOperation>(operation),
                                                     ParseEnum<AdministrationAction>(action),
                                                     identity,
                                                     argument,
                                                     secret,
                                                     cancellationToken);
        return new(true);
    }

    [ZPigeonTool("start_process",
                 "Start a hidden process under the connected client identity and return its execution job.",
                 false,
                 destructive: true,
                 openWorld: true)]
    public Task<ExecutionJob> StartProcess(
        [Description("Exact client id returned by list_clients.")] string clientId,
        string fileName,
        string? arguments = null,
        string? workingDirectory = null,
        CancellationToken cancellationToken = default) =>
        application.StartProcessAsync(ParseClientId(clientId),
                                      fileName,
                                      arguments,
                                      workingDirectory,
                                      cancellationToken);

    [ZPigeonTool("list_execution_jobs", "List processes started through the execution subsystem.", true)]
    public Task<ExecutionJob[]> ListExecutionJobs(
        [Description("Exact client id returned by list_clients.")] string clientId,
        CancellationToken cancellationToken = default) =>
        application.GetExecutionJobsAsync(ParseClientId(clientId), cancellationToken);

    [ZPigeonTool("terminate_execution", "Terminate an execution job.", false, destructive: true)]
    public async Task<ToolOperationResult> TerminateExecution(
        [Description("Exact client id returned by list_clients.")] string clientId,
        [Description("Numeric jobId returned by start_process or list_execution_jobs.")] string jobId,
        CancellationToken cancellationToken = default)
    {
        await application.TerminateExecutionAsync(ParseClientId(clientId),
                                                  ParseUInt32(jobId, nameof(jobId)),
                                                  cancellationToken);
        return new(true);
    }

    private static ulong ParseClientId(string value) => ParseUInt64(value, nameof(value));

    private static ulong ParseUInt64(string value, string parameterName) =>
        ulong.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var result) && result != 0 ?
            result :
            throw new ArgumentException(null, parameterName);

    private static uint ParseUInt32(string value, string parameterName, bool allowZero = false) =>
        uint.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var result) &&
        (allowZero || result != 0) ?
            result :
            throw new ArgumentException(null, parameterName);

    private static BrowserKind ParseBrowserKind(string value, bool sensitive)
    {
        var result = ParseEnum<BrowserKind>(value);
        if ((result is BrowserKind.Cookie or BrowserKind.Password) != sensitive)
        {
            throw new ArgumentException(null, nameof(value));
        }
        return result;
    }

    private static T ParseEnum<T>(string value) where T : struct, Enum =>
        Enum.TryParse<T>(value, true, out var result) && Enum.IsDefined(result) ?
            result :
            throw new ArgumentException(null, nameof(value));
}
