using KNSoft.ZPigeon.Server.Managed;
using System.Collections.Concurrent;
using System.Net;
using System.Text;

namespace KNSoft.ZPigeon.Web;

internal sealed record CdpBrowser(
    string Id,
    string Name,
    string Path,
    string[] Profiles);

internal sealed record CdpDiscovery(CdpBrowser[] Browsers, string ProfileRoot);

internal sealed record CdpSessionInfo(
    Guid Id,
    string Browser,
    string Profile,
    string JobId,
    PortForwardInfo Forward);

internal sealed class CdpSessionManager(
    NativeServer server,
    TcpForwardManager forwards) : IDisposable
{
    private const uint CurrentSession = uint.MaxValue;
    private const uint ClientSessionFlag = 1;
    private const uint ActiveSessionFlag = 2;
    private readonly ConcurrentDictionary<Guid, Session> sessions = new();

    internal async Task<CdpDiscovery> DiscoverAsync()
    {
        const string script = """
            $root = Join-Path $env:LOCALAPPDATA 'KNSoft\ZPigeon\CDP'
            $pf86 = ${env:ProgramFiles(x86)}
            $items = @(
                @('edge', 'Microsoft Edge', (Join-Path $pf86 'Microsoft\Edge\Application\msedge.exe')),
                @('edge', 'Microsoft Edge', (Join-Path $env:ProgramFiles 'Microsoft\Edge\Application\msedge.exe')),
                @('chrome', 'Google Chrome', (Join-Path $env:ProgramFiles 'Google\Chrome\Application\chrome.exe')),
                @('chrome', 'Google Chrome', (Join-Path $pf86 'Google\Chrome\Application\chrome.exe')),
                @('chrome', 'Google Chrome', (Join-Path $env:LOCALAPPDATA 'Google\Chrome\Application\chrome.exe'))
            )
            function Emit($prefix, $value) {
                $bytes = [Text.Encoding]::Unicode.GetBytes($value)
                Write-Output ($prefix + [Convert]::ToBase64String($bytes))
            }
            Emit 'ZP-CDP-ROOT:' $root
            $seen = @{}
            foreach ($item in $items) {
                if (!$seen[$item[0]] -and (Test-Path -LiteralPath $item[2] -PathType Leaf)) {
                    $seen[$item[0]] = $true
                    Emit 'ZP-CDP-BROWSER:' ($item -join '|')
                }
            }
            Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                $browser = $_.Name
                Get-ChildItem -LiteralPath $_.FullName -Directory -ErrorAction SilentlyContinue | ForEach-Object {
                    Emit 'ZP-CDP-PROFILE:' ($browser + '|' + $_.Name)
                }
            }
            """;
        var command = "powershell.exe -NoProfile -NonInteractive -EncodedCommand " +
                      Convert.ToBase64String(Encoding.Unicode.GetBytes(script));
        var result = await RemoteCommand.RunAsync(server, command);
        if (!result.Status.IsSuccess)
        {
            throw new NativeException(result.Status);
        }
        var root = string.Empty;
        var browsers = new Dictionary<string, (string Name, string Path)>(StringComparer.OrdinalIgnoreCase);
        var profiles = new Dictionary<string, List<string>>(StringComparer.OrdinalIgnoreCase);
        foreach (var line in result.Text.Split(['\r', '\n'], StringSplitOptions.RemoveEmptyEntries))
        {
            if (TryDecode(line, "ZP-CDP-ROOT:", out var value))
            {
                root = value;
            }
            else if (TryDecode(line, "ZP-CDP-BROWSER:", out value))
            {
                var fields = value.Split('|', 3);
                if (fields.Length == 3)
                {
                    browsers.TryAdd(fields[0], (fields[1], fields[2]));
                }
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
            throw new InvalidDataException("无法确定被控端 CDP 配置目录。");
        }
        return new(
            browsers.Select(item => new CdpBrowser(
                item.Key,
                item.Value.Name,
                item.Value.Path,
                profiles.GetValueOrDefault(item.Key)?.ToArray() ?? [])).ToArray(),
            root);
    }

    internal async Task<CdpSessionInfo> StartAsync(
        IPAddress sourceAddress,
        string browserId,
        string mode,
        string? profileName)
    {
        var discovery = await DiscoverAsync();
        var browser = discovery.Browsers.FirstOrDefault(
            item => item.Id.Equals(browserId, StringComparison.OrdinalIgnoreCase));
        if (browser is null || mode is not ("fresh" or "incognito" or "profile"))
        {
            throw new ArgumentException("CDP 启动选项无效。");
        }
        var temporary = mode != "profile";
        string profilePath;
        string profile;
        if (temporary)
        {
            profilePath = await server.CreateExecutionStagingAsync("CDP");
            profile = mode == "incognito" ? "无痕" : "全新临时配置";
        }
        else
        {
            profile = ValidateProfileName(profileName);
            profilePath = Path.Combine(discovery.ProfileRoot, browser.Id, profile);
        }
        var arguments = $"--remote-debugging-port=0 --remote-debugging-address=127.0.0.1 " +
                        $"--user-data-dir=\"{profilePath}\" " +
                        "--no-first-run --no-default-browser-check " +
                        (mode == "incognito" ? "--incognito " : string.Empty) +
                        "about:blank";
        var executionSessions = await server.EnumerateExecutionSessionsAsync();
        var clientSession = executionSessions.FirstOrDefault(item => (item.Flags & ClientSessionFlag) != 0);
        var activeSession = executionSessions.FirstOrDefault(item => (item.Flags & ActiveSessionFlag) != 0);
        var identity = clientSession is not null && clientSession.SessionId != 0 ?
            ExecutionIdentity.Current :
            ExecutionIdentity.Interactive;
        var sessionId = identity == ExecutionIdentity.Current ?
            CurrentSession :
            activeSession?.SessionId ?? throw new InvalidOperationException("被控端没有活动交互会话。");
        var job = await server.StartExecutionAsync(new(
            ExecutionEngine.CreateProcess,
            identity,
            sessionId,
            ExecutionFlags.None,
            browser.Path,
            arguments,
            null,
            null,
            null,
            null));
        try
        {
            var port = await WaitForPortAsync(Path.Combine(profilePath, "DevToolsActivePort"));
            var forward = forwards.Create(sourceAddress, sourceAddress, "CDP", "127.0.0.1", port);
            var session = new Session(
                Guid.NewGuid(),
                browser.Name,
                profile,
                profilePath,
                temporary,
                job.JobId,
                forward.Id);
            sessions[session.Id] = session;
            return session.ToInfo(forward);
        }
        catch
        {
            try
            {
                await server.TerminateExecutionAsync(uint.Parse(job.JobId));
            }
            catch (NativeException)
            {
            }
            if (temporary)
            {
                await RemoveTemporaryProfileAsync(profilePath);
            }
            throw;
        }
    }

    internal CdpSessionInfo[] GetSessions() =>
        sessions.Values.Select(session => session.ToInfo(
            forwards.Get(session.ForwardId) ?? throw new InvalidOperationException())).ToArray();

    internal async Task<bool> CloseAsync(Guid id)
    {
        if (!sessions.TryRemove(id, out var session))
        {
            return false;
        }
        forwards.Close(session.ForwardId);
        try
        {
            await server.TerminateExecutionAsync(uint.Parse(session.JobId));
        }
        catch (NativeException)
        {
        }
        if (session.Temporary)
        {
            await RemoveTemporaryProfileAsync(session.ProfilePath);
        }
        return true;
    }

    internal Task<bool> CloseForwardAsync(Guid forwardId)
    {
        var session = sessions.Values.FirstOrDefault(value => value.ForwardId == forwardId);
        return session is null ? Task.FromResult(false) : CloseAsync(session.Id);
    }

    private async Task RemoveTemporaryProfileAsync(string path)
    {
        var escaped = path.Replace("\"", "\"\"");
        try
        {
            await RemoteCommand.RunAsync(server, $"cmd.exe /D /Q /C rd /S /Q \"{escaped}\"");
        }
        catch (NativeException)
        {
        }
    }

    private async Task<ushort> WaitForPortAsync(string path)
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
                    return port;
                }
            }
            catch (NativeException)
            {
            }
            await Task.Delay(200);
        }
        throw new TimeoutException("浏览器没有在 15 秒内创建 CDP 调试端点。");
    }

    private async Task<string> ReadTextAsync(string path)
    {
        await using var transfer = await server.OpenFileReadAsync(path);
        using var output = new MemoryStream();
        await foreach (var data in transfer.Output.ReadAllAsync())
        {
            if (output.Length + data.Length > 4096)
            {
                throw new InvalidDataException("DevToolsActivePort 内容无效。");
            }
            output.Write(data.Span);
        }
        var completion = await transfer.Completion;
        if (!completion.Status.IsSuccess)
        {
            throw new NativeException(completion.Status);
        }
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
            value = Encoding.Unicode.GetString(Convert.FromBase64String(line[(index + marker.Length)..].Trim()));
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
        if (name.Length is 0 or > 64 || name is "." or ".." ||
            name.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
        {
            throw new ArgumentException("CDP 配置名称无效。");
        }
        return name;
    }

    public void Dispose()
    {
        foreach (var session in sessions.Values)
        {
            forwards.Close(session.ForwardId);
        }
    }

    private sealed record Session(
        Guid Id,
        string Browser,
        string Profile,
        string ProfilePath,
        bool Temporary,
        string JobId,
        Guid ForwardId)
    {
        internal CdpSessionInfo ToInfo(PortForwardInfo forward) =>
            new(Id, Browser, Profile, JobId, forward);
    }
}
