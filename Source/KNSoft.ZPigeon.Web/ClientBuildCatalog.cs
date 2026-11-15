namespace KNSoft.ZPigeon.Web;

internal static class ClientBuildCatalog
{
    internal static readonly ClientBuildModule[] Modules =
    [
        Module(1, "system", "SYSTEM", "system"),
        Module(2, "process", "PROCESS", "processes"),
        Module(3, "service", "SERVICE", "services"),
        Module(4, "file", "FILE", "files"),
        Module(5, "terminal", "TERMINAL", "terminal"),
        Module(6, "eventLog", "EVENT_LOG", "events"),
        Module(7, "registry", "REGISTRY", "registry"),
        Module(8, "window", "WINDOW", "windows"),
        Module(9, "user", "USER", "users"),
        Module(10, "execution", "EXECUTION", "execution"),
        Module(11, "tunnel", "TUNNEL", "forwards"),
        Module(12, "browser", "BROWSER", "browsers"),
        Module(13, "wmi", "WMI", "wmi"),
        Module(14, "audio", "AUDIO", "audio"),
        Module(15, "video", "VIDEO", "video"),
        Module(16, "rtc", "RTC", "remoteDesktop"),
        Module(17, "serial", "SERIAL", "serial"),
        Module(18, "recording", "RECORDING", "recording"),
        Module(19, "portableDevice", "PORTABLE_DEVICE", "portableDevices"),
        Module(20, "software", "SOFTWARE", "software"),
        Module(21, "hardware", "HARDWARE", "hardware"),
        Module(22, "update", "UPDATE", "updates"),
        Module(23, "task", "TASK", "tasks"),
        Module(24, "firewall", "FIREWALL", "firewall"),
        Module(25, "power", "POWER", "power"),
        Module(26, "systemAdministration", "SYSTEM_ADMINISTRATION", "systemAdministration"),
        Module(27, "wlan", "WLAN", "wlan"),
        Module(28, "certificate", "CERTIFICATE", "certificates"),
        Module(29, "clipboard", "CLIPBOARD", "clipboard"),
        Module(30, "credential", "CREDENTIAL", "credentials"),
        Module(31, "firmware", "FIRMWARE", "firmware"),
        Module(32, "networkShare", "NETWORK_SHARE", "networkShares"),
        Module(33, "networkStatus", "NETWORK_STATUS", "networkAdapters"),
        Module(34, "pageFile", "PAGE_FILE", "pageFiles"),
        Module(35, "bluetooth", "BLUETOOTH", "bluetooth"),
        Module(36, "keyboard", "KEYBOARD", "keyboard"),
        Module(37, "location", "LOCATION", "location"),
        Module(38, "font", "FONT", "fonts"),
        Module(39, "appContainer", "APP_CONTAINER", "sandbox"),
        Module(40, "winobj", "WINOBJ", "winobj"),
        Module(41, "wsl", "WSL", "wsl"),
        Module(42, "uiAutomation", "UI_AUTOMATION", "uiAutomation"),
        Module(43, "proxyVpn", "PROXY_VPN", "proxyVpn"),
        Module(44, "clientStatus", "CLIENT_STATUS", "clientStatus"),
        Module(45, "shadowCopy", "SHADOW_COPY", "shadowCopies"),
        Module(46, "bitlocker", "BITLOCKER", "bitlocker")
    ];

    internal static readonly ClientBuildExecutionOption[] ExecutionOptions =
    [
        Option("runtimeDiscovery", "RUNTIME_DISCOVERY"),
        Option("shellExecute", "SHELL_EXECUTE"),
        Option("alternateIdentity", "ALTERNATE_IDENTITY"),
        Option("appContainer", "APP_CONTAINER"),
        Option("customToken", "CUSTOM_TOKEN"),
        Option("staging", "STAGING")
    ];

    internal static readonly ClientBuildTransport[] Transports =
    [
        new(1, "QUIC / TLS 1.3", "_KNSOFT_ZPIGEON_BUILD_TRANSPORT_QUIC_", 21943),
        new(2, "TCP / TLS", "_KNSOFT_ZPIGEON_BUILD_TRANSPORT_TCP_", 21943),
        new(3, "UDP / DTLS 1.2", "_KNSOFT_ZPIGEON_BUILD_TRANSPORT_UDP_", 21944)
    ];

    internal static ClientBuildSelection Validate(ClientBuildRequest request)
    {
        if (request.Modules is null || request.Modules.Length > Modules.Length ||
            request.Modules.Distinct().Count() != request.Modules.Length ||
            request.Modules.Any(id => Modules.All(module => module.Id != id)) ||
            request.Transports is null || request.Transports.Length is 0 or > 3 ||
            request.Transports.Distinct().Count() != request.Transports.Length ||
            request.Transports.Any(id => id is < 1 or > 3) ||
            request.ExecutionOptions is null || request.ExecutionOptions.Length > ExecutionOptions.Length ||
            request.ExecutionOptions.Distinct(StringComparer.Ordinal).Count() != request.ExecutionOptions.Length ||
            request.ExecutionOptions.Any(id => ExecutionOptions.All(option => option.Id != id)) ||
            request.ExecutionOptions.Length != 0 && !request.Modules.Contains(10) ||
            request.RequireTcpTls13 && !request.Transports.Contains(2))
        {
            throw new ArgumentException("Invalid module, transport or execution selection.");
        }
        return new(request.Modules.Order().ToArray(),
                   request.Transports.ToArray(),
                   request.ExecutionOptions.Order(StringComparer.Ordinal).ToArray());
    }

    internal static string[] GetRuntimeFiles(ClientBuildSelection selection) =>
        [
            ..selection.Modules.Contains(20) ?
                new[] { "Microsoft.Management.Deployment.dll", "Microsoft.Management.Deployment.winmd" } : []
        ];

    internal static string[] GetMacros(ClientBuildSelection selection) =>
        [
            "_KNSOFT_ZPIGEON_BUILD_CUSTOM_",
            ..Modules.Where(module => selection.Modules.Contains(module.Id)).Select(module => module.Macro),
            ..Transports.Where(transport => selection.Transports.Contains(transport.Id))
                        .Select(transport => transport.Macro),
            ..ExecutionOptions.Where(option => selection.ExecutionOptions.Contains(option.Id))
                              .Select(option => option.Macro)
        ];

    private static ClientBuildModule Module(int id, string key, string suffix, string label) =>
        new(id, key, $"module.{label}", $"_KNSOFT_ZPIGEON_BUILD_MODULE_{suffix}_");

    private static ClientBuildExecutionOption Option(string id, string suffix) =>
        new(id, $"console.execution.{id}", $"_KNSOFT_ZPIGEON_BUILD_EXECUTION_{suffix}_");
}

internal sealed record ClientBuildModule(int Id, string Key, string LabelKey, string Macro);
internal sealed record ClientBuildExecutionOption(string Id, string LabelKey, string Macro);
internal sealed record ClientBuildTransport(int Id, string Name, string Macro, int Port);
internal sealed record ClientBuildSelection(int[] Modules, int[] Transports, string[] ExecutionOptions);
internal sealed record ClientBuildRequest(
    string? Group,
    string? InstanceId,
    int[]? Modules,
    int[]? Transports,
    string[]? ExecutionOptions,
    bool RequireTcpTls13);
