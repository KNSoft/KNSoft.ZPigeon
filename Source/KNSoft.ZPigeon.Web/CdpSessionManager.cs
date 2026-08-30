using KNSoft.ZPigeon.Server.Managed;
using System.Collections.Concurrent;
using System.Globalization;
using System.Net;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace KNSoft.ZPigeon.Web;

internal sealed record CdpProfile(
    string Name,
    string Kind,
    [property: JsonIgnore] string Location,
    bool InUse);
internal sealed record CdpBrowser(
    string Id,
    string Name,
    [property: JsonIgnore] string Path,
    CdpProfile[] Profiles);
internal sealed record CdpDiscovery(CdpBrowser[] Browsers);
internal sealed record CdpProfileInspection(
    long ProfileSize,
    long AvailableSpace,
    bool BrowserRunning);
internal sealed record CdpTarget(
    string Id,
    string Type,
    string Title,
    string Url,
    string? DevtoolsFrontendUrl);
internal sealed record CdpSessionInfo(
    Guid Id,
    string Browser,
    string Profile,
    PortForwardInfo Forward);
internal sealed record CdpDataProfile(string Profile, string? UserData);

internal sealed class CdpSessionManager(
    NativeServer server,
    TcpForwardManager forwards) : IDisposable
{
    private const uint CurrentSession = uint.MaxValue;
    private const uint StatusNoSuchFile = 0xC000000F;
    private const uint StatusObjectNameNotFound = 0xC0000034;
    private const uint StatusObjectPathNotFound = 0xC000003A;
    private const uint StatusSharingViolation = 0xC0000043;
    private static readonly HttpClient Http = new() { Timeout = TimeSpan.FromSeconds(10) };
    private readonly ConcurrentDictionary<Guid, Session> sessions = new();
    private string? managedRoot;

    internal async Task<CdpDiscovery> DiscoverAsync() => (await DiscoverCoreAsync()).Discovery;

    private async Task<(CdpDiscovery Discovery, ManagedDiscovery Managed)> DiscoverCoreAsync()
    {
        var page = await server.EnumerateBrowsersAsync();
        var managed = await DiscoverManagedProfilesAsync();
        var discovery = new CdpDiscovery(page.Records.Where(record => record.Kind == BrowserKind.Browser)
            .Select(record =>
            {
                var id = record.Browser == BrowserType.Edge ? "edge" : "chrome";
                var profiles = page.Records
                    .Where(profile => profile.Kind == BrowserKind.Profile &&
                                      profile.Browser == record.Browser)
                    .Select(profile => new CdpProfile(profile.Identity, "source", profile.Location, false))
                    .Concat(managed.Profiles.GetValueOrDefault(id, []).Select(name =>
                    {
                        var location = Path.Combine(managed.Root, id, name);
                        return new CdpProfile(name, "managed", location, IsProfileInUse(location));
                    }))
                    .ToArray();
                return new CdpBrowser(id,
                                      record.Browser == BrowserType.Edge ? "Microsoft Edge" : "Google Chrome",
                                      record.Location,
                                      profiles);
            })
            .ToArray());
        return (discovery, managed);
    }

    internal async Task<CdpProfileInspection> InspectProfileAsync(
        string browserId,
        string profileKind,
        string profileName)
    {
        var (browser, profile, _) = await ResolveProfileAsync(browserId, profileKind, profileName);
        var managed = profile.Kind == "managed";
        var result = await server.InspectBrowserProfileAsync(
            browser.Id == "edge" ? BrowserType.Edge :
            browser.Id == "chrome" ? BrowserType.Chrome : throw new ArgumentException("浏览器无效。"),
            managed ? "Default" : profile.Name,
            managed ? profile.Location : null);
        return new(checked((long)result.ProfileSize),
                   checked((long)result.AvailableSpace),
                   managed ? profile.InUse : result.BrowserRunning);
    }

    internal async Task<CdpProfile> CloneProfileAsync(
        string browserId,
        string profileKind,
        string profileName,
        string targetName)
    {
        targetName = ValidateProfileName(targetName);
        var (browser, profile, managed) = await ResolveProfileAsync(browserId, profileKind, profileName);
        EnsureProfileDoesNotExist(browser, targetName);
        var userData = profile.Kind == "managed" ? profile.Location :
            Directory.GetParent(profile.Location)?.FullName ??
            throw new InvalidDataException("浏览器 Profile 路径无效。");
        var source = profile.Kind == "managed" ? Path.Combine(userData, "Default") : profile.Location;
        var destination = Path.Combine(managed.Root, browser.Id, targetName);
        var script = $$"""
            $source = {{PowerShellLiteral(source)}}
            $state = Join-Path {{PowerShellLiteral(userData)}} 'Local State'
            $destination = {{PowerShellLiteral(destination)}}
            $created = $false
            try {
                New-Item -ItemType Directory -Path $destination -ErrorAction Stop | Out-Null
                $created = $true
                if (Test-Path -LiteralPath $state -PathType Leaf) {
                    Copy-Item -LiteralPath $state -Destination $destination -Force -ErrorAction Stop
                }
                $profile = Join-Path $destination 'Default'
                New-Item -ItemType Directory -Path $profile -ErrorAction Stop | Out-Null
                if (Test-Path -LiteralPath $source -PathType Container) {
                    & robocopy.exe $source $profile /E /COPY:DAT /DCOPY:DAT /R:3 /W:1 /XJ /MT:8 /NFL /NDL /NJH /NJS /NP
                    if ($LASTEXITCODE -ge 8) { throw "Robocopy failed: $LASTEXITCODE" }
                }
                Write-Output 'ZP-CDP-CLONE:OK'
            } catch {
                if ($created) {
                    Remove-Item -LiteralPath $destination -Recurse -Force -ErrorAction SilentlyContinue
                }
                throw
            }
            """;
        var result = await RunScriptAsync(script);
        if (!result.Status.IsSuccess) throw new NativeException(result.Status);
        if (!result.Text.Contains("ZP-CDP-CLONE:OK", StringComparison.Ordinal))
        {
            throw new InvalidDataException("浏览器 Profile 副本创建失败。");
        }
        return new(targetName, "managed", destination, false);
    }

    internal async Task<CdpProfile> CreateProfileAsync(string browserId, string profileName)
    {
        profileName = ValidateProfileName(profileName);
        var (discovery, managed) = await DiscoverCoreAsync();
        var browser = discovery.Browsers.FirstOrDefault(item =>
            item.Id.Equals(browserId, StringComparison.OrdinalIgnoreCase)) ??
            throw new ArgumentException("浏览器无效。");
        EnsureProfileDoesNotExist(browser, profileName);
        var location = Path.Combine(managed.Root, browser.Id, profileName);
        var script = $$"""
            $destination = {{PowerShellLiteral(location)}}
            $created = $false
            try {
                New-Item -ItemType Directory -Path $destination -ErrorAction Stop | Out-Null
                $created = $true
                New-Item -ItemType Directory -Path (Join-Path $destination 'Default') -ErrorAction Stop | Out-Null
                Write-Output 'ZP-CDP-CREATE:OK'
            } catch {
                if ($created) {
                    Remove-Item -LiteralPath $destination -Recurse -Force -ErrorAction SilentlyContinue
                }
                throw
            }
            """;
        var result = await RunScriptAsync(script);
        if (!result.Status.IsSuccess) throw new NativeException(result.Status);
        if (!result.Text.Contains("ZP-CDP-CREATE:OK", StringComparison.Ordinal))
        {
            throw new InvalidDataException("浏览器 Profile 创建失败。");
        }
        return new(profileName, "managed", location, false);
    }

    internal async Task DeleteProfileAsync(string browserId, string profileName)
    {
        var (_, profile, _) = await ResolveProfileAsync(browserId, "managed", profileName);
        if (IsProfileInUse(profile.Location))
        {
            throw new ArgumentException("远程浏览器 Profile 正在使用。");
        }
        var script = $$"""
            Remove-Item -LiteralPath {{PowerShellLiteral(profile.Location)}} -Recurse -Force -ErrorAction Stop
            Write-Output 'ZP-CDP-DELETE:OK'
            """;
        var result = await RunScriptAsync(script);
        if (!result.Status.IsSuccess) throw new NativeException(result.Status);
        if (!result.Text.Contains("ZP-CDP-DELETE:OK", StringComparison.Ordinal))
        {
            throw new InvalidDataException("浏览器 Profile 删除失败。");
        }
    }

    internal async Task<CdpDataProfile> ResolveDataProfileAsync(string browserId, string profileName)
    {
        if (browserId is not ("edge" or "chrome")) throw new ArgumentException("浏览器无效。");
        var root = managedRoot ?? (await DiscoverManagedProfilesAsync()).Root;
        return new("Default", Path.Combine(root, browserId, ValidateProfileName(profileName)));
    }

    internal async Task<CdpSessionInfo> StartAsync(
        IPAddress sourceAddress,
        string browserId,
        string profileName)
    {
        var (browser, selected, _) = await ResolveProfileAsync(browserId, "managed", profileName);
        if (IsProfileInUse(selected.Location))
        {
            throw new ArgumentException("远程浏览器 Profile 正在使用。");
        }
        var profile = selected.Name;
        var profilePath = selected.Location;
        await DeleteDevToolsActivePortAsync(profilePath);
        var arguments = $"--headless=new --remote-debugging-port=0 --remote-debugging-address=127.0.0.1 " +
                        $"--user-data-dir=\"{profilePath}\" --profile-directory=Default " +
                        "--window-size=1280,800 --no-first-run --no-default-browser-check " +
                        "--disable-sync --disable-background-networking " +
                        "about:blank";
        var job = await server.StartExecutionAsync(new(
            ExecutionEngine.CreateProcess,
            ExecutionIdentity.Current,
            CurrentSession,
            ExecutionFlags.JobObject,
            browser.Path,
            arguments,
            null,
            null,
            null,
            null,
            null));
        try
        {
            var port = await WaitForPortAsync(Path.Combine(profilePath, "DevToolsActivePort"), job);
            var validationForward = forwards.Create(sourceAddress, IPAddress.Loopback, "CDP", "127.0.0.1", port);
            try
            {
                await WaitForDevToolsAsync(validationForward.Port, job);
            }
            finally
            {
                forwards.Close(validationForward.Id);
            }
            var forward = forwards.Create(sourceAddress, sourceAddress, "CDP", "127.0.0.1", port);
            var session = new Session(Guid.NewGuid(), browser.Name, profile, profilePath,
                                      job.JobId, forward.Id, forward.Port);
            sessions[session.Id] = session;
            return session.ToInfo(forward);
        }
        catch
        {
            await StopJobAsync(job.JobId);
            throw;
        }
    }

    internal async Task<CdpSessionInfo[]> GetSessionsAsync()
    {
        var result = new List<CdpSessionInfo>();
        foreach (var session in sessions.Values)
        {
            var forward = forwards.Get(session.ForwardId);
            if (forward is null || forward.State is "Closed" or "Expired" or "Failed")
            {
                await CloseAsync(session.Id);
            }
            else
            {
                result.Add(session.ToInfo(forward));
            }
        }
        return result.ToArray();
    }

    internal async Task<CdpTarget[]> GetTargetsAsync(Guid id, CancellationToken cancellationToken = default)
    {
        var session = GetSession(id);
        var targets = await Http.GetFromJsonAsync<CdpTarget[]>(
            Endpoint(session, "/json/list"),
            JsonSerializerOptions.Web,
            cancellationToken);
        return (targets?.Where(target => target.Type == "page") ?? [])
            .Select(NormalizeTarget)
            .ToArray();
    }

    internal async Task<CdpTarget> CreateTargetAsync(
        Guid id,
        string url,
        CancellationToken cancellationToken = default)
    {
        if (url is not { Length: > 0 and <= 2048 } ||
            !Uri.TryCreate(url, UriKind.Absolute, out var address) ||
            address.Scheme is not ("http" or "https" or "about"))
        {
            throw new ArgumentException("浏览器地址无效。");
        }
        using var request = new HttpRequestMessage(
            HttpMethod.Put,
            Endpoint(GetSession(id), $"/json/new?{Uri.EscapeDataString(url)}"));
        using var response = await Http.SendAsync(request, cancellationToken);
        response.EnsureSuccessStatusCode();
        var target = await response.Content.ReadFromJsonAsync<CdpTarget>(
                         JsonSerializerOptions.Web,
                         cancellationToken) ??
                     throw new InvalidDataException("浏览器没有返回新标签页。");
        return NormalizeTarget(target);
    }

    internal async Task<bool> CloseTargetAsync(
        Guid id,
        string targetId,
        CancellationToken cancellationToken = default)
    {
        using var response = await Http.GetAsync(Endpoint(
            GetSession(id),
            $"/json/close/{Uri.EscapeDataString(ValidateTargetId(targetId))}"),
            cancellationToken);
        return response.IsSuccessStatusCode;
    }

    internal async Task<Uri> GetControlEndpointAsync(
        Guid id,
        string targetId,
        CancellationToken cancellationToken = default)
    {
        targetId = ValidateTargetId(targetId);
        var session = GetSession(id);
        if (!(await GetTargetsAsync(id, cancellationToken)).Any(target => target.Id == targetId))
        {
            throw new KeyNotFoundException();
        }
        return new UriBuilder(Uri.UriSchemeWs, "127.0.0.1", session.ForwardPort,
                              $"/devtools/page/{targetId}").Uri;
    }

    internal async Task<bool> CloseAsync(Guid id)
    {
        if (!sessions.TryRemove(id, out var session)) return false;
        forwards.Close(session.ForwardId);
        await StopJobAsync(session.JobId);
        return true;
    }

    private static CdpTarget NormalizeTarget(CdpTarget target) => target with
    {
        // Never expose a vendor-provided external URL; the browser serves its matching frontend locally.
        DevtoolsFrontendUrl = string.IsNullOrEmpty(target.DevtoolsFrontendUrl) ?
            null : "/devtools/inspector.html"
    };

    internal Task<bool> CloseForwardAsync(Guid forwardId)
    {
        var session = sessions.Values.FirstOrDefault(value => value.ForwardId == forwardId);
        return session is null ? Task.FromResult(false) : CloseAsync(session.Id);
    }

    private async Task<(CdpBrowser Browser, CdpProfile Profile, ManagedDiscovery Managed)>
        ResolveProfileAsync(string browserId, string profileKind, string profileName)
    {
        var (discovery, managed) = await DiscoverCoreAsync();
        var browser = discovery.Browsers.FirstOrDefault(item =>
            item.Id.Equals(browserId, StringComparison.OrdinalIgnoreCase)) ??
            throw new ArgumentException("浏览器无效。");
        var profile = browser.Profiles.FirstOrDefault(item =>
            item.Kind == profileKind && item.Name.Equals(profileName, StringComparison.OrdinalIgnoreCase)) ??
            throw new ArgumentException("浏览器 Profile 不存在。");
        return (browser, profile, managed);
    }

    private bool IsProfileInUse(string location) => sessions.Values.Any(session =>
        session.ProfilePath.Equals(location, StringComparison.OrdinalIgnoreCase) &&
        forwards.Get(session.ForwardId) is { State: not ("Closed" or "Expired" or "Failed") });

    private static void EnsureProfileDoesNotExist(CdpBrowser browser, string profileName)
    {
        if (browser.Profiles.Any(profile => profile.Kind == "managed" &&
                                            profile.Name.Equals(profileName,
                                                StringComparison.OrdinalIgnoreCase)))
        {
            throw new ArgumentException("浏览器 Profile 已存在。");
        }
    }

    private async Task<ManagedDiscovery> DiscoverManagedProfilesAsync()
    {
        const string script = """
            $root = Join-Path $env:LOCALAPPDATA 'KNSoft\ZPigeon\CDP'
            function Emit($prefix, $value) {
                $bytes = [Text.Encoding]::UTF8.GetBytes($value)
                Write-Output ($prefix + [Convert]::ToBase64String($bytes))
            }
            Emit 'ZP-CDP-ROOT:' $root
            Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                $browser = $_.Name
                Get-ChildItem -LiteralPath $_.FullName -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                    Emit 'ZP-CDP-PROFILE:' ($browser + '|' + $_.Name)
                }
            }
            """;
        var result = await RunScriptAsync(script);
        if (!result.Status.IsSuccess) throw new NativeException(result.Status);
        var root = string.Empty;
        var profiles = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        foreach (var line in result.Text.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries))
        {
            if (TryDecode(line, "ZP-CDP-ROOT:", out var value))
            {
                root = value;
            }
            else if (TryDecode(line, "ZP-CDP-PROFILE:", out value))
            {
                var fields = value.Split('|', 2);
                if (fields.Length == 2)
                {
                    profiles.TryAdd(fields[0], []);
                    profiles[fields[0]].Add(fields[1]);
                }
            }
        }
        if (root.Length == 0)
        {
            throw new InvalidDataException("无法确定被控端远程浏览器 Profile 目录。");
        }
        managedRoot = root;
        return new(root, profiles);
    }

    private Task<RemoteCommandResult> RunScriptAsync(string script) =>
        RemoteCommand.RunAsync(server,
            "powershell.exe",
            "-NoProfile -NonInteractive -EncodedCommand " + Convert.ToBase64String(Encoding.Unicode.GetBytes(script)),
            columns: 1024);

    private static Uri Endpoint(Session session, string path) =>
        new(new UriBuilder(Uri.UriSchemeHttp, "127.0.0.1", session.ForwardPort).Uri, path);

    private PortForwardInfo GetForward(Session session)
    {
        var forward = forwards.Get(session.ForwardId);
        if (forward is null || forward.State is "Closed" or "Expired" or "Failed")
        {
            throw new KeyNotFoundException();
        }
        session.ForwardPort = forward.Port;
        return forward;
    }

    private Session GetSession(Guid id)
    {
        var session = sessions.GetValueOrDefault(id) ?? throw new KeyNotFoundException();
        _ = GetForward(session);
        return session;
    }

    private async Task StopJobAsync(string jobId)
    {
        try
        {
            await server.TerminateExecutionAsync(uint.Parse(jobId, CultureInfo.InvariantCulture));
        }
        catch (NativeException)
        {
        }
    }

    private async Task DeleteDevToolsActivePortAsync(string profilePath)
    {
        try
        {
            await server.DeleteFileAsync(Path.Combine(profilePath, "DevToolsActivePort"));
        }
        catch (NativeException exception) when (IsMissingFile(exception))
        {
        }
    }

    private static bool IsMissingFile(NativeException exception) =>
        exception.Status.Type == ZpStatusType.NtStatus &&
        exception.Status.Code is StatusNoSuchFile or StatusObjectNameNotFound or StatusObjectPathNotFound;

    private static bool IsPortFilePending(NativeException exception) =>
        IsMissingFile(exception) ||
        exception.Status is { Type: ZpStatusType.NtStatus, Code: StatusSharingViolation };

    private async Task EnsureJobRunningAsync(ExecutionJob started)
    {
        var current = (await server.EnumerateExecutionJobsAsync()).FirstOrDefault(job =>
            job.JobId == started.JobId && job.ProcessId == started.ProcessId &&
            job.CreateTime == started.CreateTime);
        if (current?.State != ExecutionJobState.Running)
        {
            throw new InvalidDataException("远程浏览器进程已退出。");
        }
    }

    private async Task<ushort> WaitForPortAsync(string path, ExecutionJob job)
    {
        var deadline = DateTime.UtcNow.AddSeconds(15);
        while (DateTime.UtcNow < deadline)
        {
            try
            {
                var text = await ReadTextAsync(path);
                var firstLine = text.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries).FirstOrDefault();
                if (ushort.TryParse(firstLine, out var port) && port != 0)
                {
                    await EnsureJobRunningAsync(job);
                    return port;
                }
            }
            catch (NativeException exception) when (IsPortFilePending(exception))
            {
            }
            await EnsureJobRunningAsync(job);
            await Task.Delay(200);
        }
        throw new TimeoutException("浏览器没有在 15 秒内创建 CDP 调试端点。");
    }

    private async Task WaitForDevToolsAsync(int port, ExecutionJob job)
    {
        var endpoint = new UriBuilder(Uri.UriSchemeHttp, "127.0.0.1", port, "/json/version").Uri;
        var deadline = DateTime.UtcNow.AddSeconds(15);
        while (DateTime.UtcNow < deadline)
        {
            await EnsureJobRunningAsync(job);
            try
            {
                using var response = await Http.GetAsync(endpoint);
                if (response.IsSuccessStatusCode)
                {
                    await using var stream = await response.Content.ReadAsStreamAsync();
                    using var document = await JsonDocument.ParseAsync(stream);
                    if (document.RootElement.TryGetProperty("webSocketDebuggerUrl", out var value) &&
                        value.ValueKind == JsonValueKind.String &&
                        !string.IsNullOrEmpty(value.GetString()))
                    {
                        return;
                    }
                }
            }
            catch (Exception exception) when (exception is HttpRequestException or
                                                          TaskCanceledException or
                                                          JsonException)
            {
            }
            await Task.Delay(200);
        }
        throw new TimeoutException("浏览器 CDP 调试端点没有在 15 秒内就绪。");
    }

    private async Task<string> ReadTextAsync(string path)
    {
        await using var transfer = await server.OpenFileReadAsync(path);
        using var output = new MemoryStream();
        await foreach (var data in transfer.Output.ReadAllAsync())
        {
            using (data)
            {
                if (output.Length + data.Length > 4096)
                {
                    throw new InvalidDataException("DevToolsActivePort 内容无效。");
                }
                output.Write(data.Span);
            }
        }
        var completion = await transfer.Completion;
        if (!completion.Status.IsSuccess) throw new NativeException(completion.Status);
        return Encoding.ASCII.GetString(output.GetBuffer(), 0, checked((int)output.Length));
    }

    private static bool TryDecode(string line, string marker, out string value)
    {
        var index = line.IndexOf(marker, StringComparison.Ordinal);
        if (index < 0)
        {
            value = string.Empty;
            return false;
        }
        try
        {
            var encoded = line.AsSpan(index + marker.Length).TrimStart();
            var length = 0;
            while (length < encoded.Length &&
                   (char.IsAsciiLetterOrDigit(encoded[length]) || encoded[length] is '+' or '/' or '='))
            {
                length++;
            }
            value = Encoding.UTF8.GetString(Convert.FromBase64String(encoded[..length].ToString()));
            return true;
        }
        catch (FormatException)
        {
            value = string.Empty;
            return false;
        }
    }

    private static string ValidateProfileName(string? value)
    {
        var name = value?.Trim() ?? string.Empty;
        if (name.Length is 0 or > 64 || name is "." or ".." || name[^1] is ' ' or '.' ||
            name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            throw new ArgumentException("浏览器 Profile 名称无效。");
        }
        return name;
    }

    private static string ValidateTargetId(string? value)
    {
        if (value is not { Length: > 0 and <= 128 } || !value.All(char.IsAsciiLetterOrDigit))
        {
            throw new ArgumentException("浏览器标签页无效。");
        }
        return value;
    }

    private static string PowerShellLiteral(string value) => $"'{value.Replace("'", "''")}'";

    public void Dispose()
    {
        foreach (var session in sessions.Values)
        {
            forwards.Close(session.ForwardId);
            StopJobAsync(session.JobId).GetAwaiter().GetResult();
        }
        sessions.Clear();
    }

    private sealed record ManagedDiscovery(string Root, Dictionary<string, List<string>> Profiles);

    private sealed record Session(
        Guid Id,
        string Browser,
        string Profile,
        string ProfilePath,
        string JobId,
        Guid ForwardId,
        int InitialForwardPort)
    {
        internal int ForwardPort { get; set; } = InitialForwardPort;

        internal CdpSessionInfo ToInfo(PortForwardInfo forward)
        {
            ForwardPort = forward.Port;
            return new(Id, Browser, Profile, forward);
        }
    }
}
