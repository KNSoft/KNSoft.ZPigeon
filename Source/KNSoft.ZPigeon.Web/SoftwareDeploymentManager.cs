using System.Collections.Concurrent;
using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal sealed class SoftwareDeploymentManager : IDisposable
{
    private const uint EngineMsi = 0x00000001;
    private const uint EngineAppx = 0x00000002;
    private const uint EngineAppInstaller = 0x00000003;
    private const uint EngineWinGet = 0x00000004;
    private const uint EnginePip = 0x00000005;
    private const uint EngineNpm = 0x00000006;
    private const uint EngineChocolatey = 0x00000007;
    private const uint EngineDotNetTool = 0x00000008;
    private const uint EngineMask = 0x0000000F;
    private const uint SourceWinGet = 0x00000010;
    private const uint SourceStore = 0x00000020;
    private const uint ScopeUser = 0x00000100;
    private const uint ScopeMachine = 0x00000200;
    private const uint AllPackages = 0x00001000;
    private const int ActionShift = 16;
    private const uint ActionMask = 0x001F0000;
    private const uint RebootRequired = 0x80000000;
    private const uint StateMask = 0x000000FF;
    private const int ProgressShift = 8;
    private const uint ProgressMask = 0x003FFF00;
    private readonly NativeServer server;
    private readonly ulong clientId;
    private readonly ConcurrentDictionary<Guid, SoftwareDeploymentMetadata> jobs = [];
    private readonly HashSet<string> stagingPaths = new(StringComparer.OrdinalIgnoreCase);
    private readonly SemaphoreSlim gate = new(1, 1);
    private int disposed;

    internal SoftwareDeploymentManager(NativeServer server)
    {
        this.server = server;
        clientId = server.ClientId;
    }

    internal async Task<string> CreateStagingAsync(string name)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        using var selection = server.SelectClient(clientId);
        var path = await server.CreateExecutionStagingAsync(name);
        lock (stagingPaths) stagingPaths.Add(path);
        return path;
    }

    internal async Task DeleteStagingAsync(string path)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        await gate.WaitAsync();
        try
        {
            lock (stagingPaths)
            {
                if (!stagingPaths.Contains(path)) throw new InvalidOperationException("Unknown software staging path.");
            }
            using var selection = server.SelectClient(clientId);
            await server.DeleteFileAsync(path);
            lock (stagingPaths) stagingPaths.Remove(path);
        }
        finally
        {
            gate.Release();
        }
    }

    internal Task<SoftwareDeployment> InstallPackageAsync(string path, string name, string[] dependencies)
    {
        var engine = GetPackageEngine(path);
        if (engine is EngineMsi or EngineAppInstaller && dependencies.Length != 0)
        {
            throw new ArgumentException(engine == EngineMsi ?
                "MSI dependencies are not supported." :
                "App Installer dependencies are declared by the App Installer file.");
        }
        if (dependencies.Any(value => Path.GetExtension(value).ToLowerInvariant() is not (".appx" or ".msix")))
        {
            throw new ArgumentException("Unsupported AppX dependency type.");
        }
        return StartAsync(SoftwareDeploymentAction.Install,
                          AdministrationAction.Install,
                          engine,
                          name,
                          path,
                          [path, .. dependencies],
                          [path, name, .. dependencies]);
    }

    internal Task<SoftwareDeployment> InstallExistingPackageAsync(string path, string name) =>
        StartAsync(SoftwareDeploymentAction.Install,
                   AdministrationAction.Install,
                   GetPackageEngine(path),
                   name,
                   path,
                   [],
                   [path, name]);

    internal Task<SoftwareDeployment> UninstallWindowsAppAsync(string identity, string name) =>
        StartAsync(SoftwareDeploymentAction.Uninstall,
                   AdministrationAction.Uninstall,
                   EngineAppx,
                   name,
                   identity,
                   [],
                   [identity, name]);

    internal Task<SoftwareDeployment> ControlWinGetAsync(
        SoftwareDeploymentAction action,
        string? identity,
        string? source,
        string? scope)
    {
        var all = action == SoftwareDeploymentAction.UpgradeAll;
        if (all ? identity is not null : string.IsNullOrEmpty(identity))
        {
            throw new ArgumentException("Invalid WinGet package identity.");
        }
        var nativeAction = action switch
        {
            SoftwareDeploymentAction.Install => AdministrationAction.Install,
            SoftwareDeploymentAction.Upgrade or SoftwareDeploymentAction.UpgradeAll => AdministrationAction.Upgrade,
            SoftwareDeploymentAction.Uninstall => AdministrationAction.Uninstall,
            _ => throw new ArgumentOutOfRangeException(nameof(action))
        };
        var flags = EngineWinGet |
                    (all ? AllPackages :
                     action == SoftwareDeploymentAction.Uninstall ? 0 :
                     source!.Equals("msstore", StringComparison.OrdinalIgnoreCase) ? SourceStore : SourceWinGet) |
                    (scope == "user" ? ScopeUser : scope == "machine" ? ScopeMachine : 0);
        var packageIdentity = all ? "*" : identity!;
        return StartAsync(action,
                          nativeAction,
                          flags,
                          all ? "WinGet" : packageIdentity,
                          all ? null : packageIdentity,
                          [],
                          [packageIdentity, all ? "WinGet" : packageIdentity]);
    }

    internal async Task<AdministrationRecord[]> EnumeratePackageProvidersAsync()
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        using var selection = server.SelectClient(clientId);
        return await server.EnumerateAdministrationAsync(AdministrationOperation.EnumeratePackageProviders);
    }

    internal async Task<AdministrationRecord[]> EnumeratePackagesAsync(string provider)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        using var selection = server.SelectClient(clientId);
        return await server.QueryAdministrationAsync(AdministrationOperation.QueryPackages, provider);
    }

    internal Task<SoftwareDeployment> ControlPackageAsync(
        string provider,
        SoftwareDeploymentAction action,
        string? identity,
        string? version,
        string? scope)
    {
        if (provider.Equals("winget", StringComparison.OrdinalIgnoreCase))
            throw new ArgumentException("WinGet requires source information.", nameof(provider));
        var engine = GetProviderEngine(provider);
        var all = action == SoftwareDeploymentAction.UpgradeAll;
        if (all ? identity is not null || version is not null : string.IsNullOrEmpty(identity))
            throw new ArgumentException("Invalid package identity.", nameof(identity));
        var nativeAction = action switch
        {
            SoftwareDeploymentAction.Install => AdministrationAction.Install,
            SoftwareDeploymentAction.Upgrade or SoftwareDeploymentAction.UpgradeAll => AdministrationAction.Upgrade,
            SoftwareDeploymentAction.Uninstall => AdministrationAction.Uninstall,
            _ => throw new ArgumentOutOfRangeException(nameof(action))
        };
        var flags = engine | (all ? AllPackages : 0) | (scope == "user" ? ScopeUser : 0);
        var packageIdentity = all ? "*" : identity!;
        var name = all ? provider : packageIdentity;
        return StartAsync(action,
                          nativeAction,
                          flags,
                          name,
                          all ? null : packageIdentity,
                          [],
                          string.IsNullOrEmpty(version) ? [packageIdentity, name] : [packageIdentity, name, version]);
    }

    internal async Task<SoftwareDeployment[]> EnumerateJobsAsync()
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        using var selection = server.SelectClient(clientId);
        var records = await server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateSoftwareDeployments);
        var deployments = new List<SoftwareDeployment>(records.Length);
        foreach (var record in records)
        {
            if (record.Kind != AdministrationKind.SoftwareDeployment || !Guid.TryParse(record.Identity, out var id) ||
                !ulong.TryParse(record.Value, out var result))
            {
                throw new InvalidDataException("Invalid software deployment record.");
            }
            var metadata = jobs.GetOrAdd(id, _ => new(DateTime.UtcNow, []));
            var state = (SoftwareDeploymentState)(record.State & StateMask);
            DateTime? completedTime;
            lock (metadata)
            {
                if (state is SoftwareDeploymentState.Completed or SoftwareDeploymentState.Failed)
                {
                    metadata.CompletedTime ??= DateTime.UtcNow;
                }
                completedTime = metadata.CompletedTime;
            }
            if (state is SoftwareDeploymentState.Completed or SoftwareDeploymentState.Failed)
            {
                await CleanupAsync(metadata);
            }
            var nativeAction = (AdministrationAction)((record.Flags & ActionMask) >> ActionShift);
            var action = (record.Flags & AllPackages) != 0 ? SoftwareDeploymentAction.UpgradeAll : nativeAction switch
            {
                AdministrationAction.Install => SoftwareDeploymentAction.Install,
                AdministrationAction.Upgrade => SoftwareDeploymentAction.Upgrade,
                AdministrationAction.Uninstall => SoftwareDeploymentAction.Uninstall,
                _ => throw new InvalidDataException("Invalid software deployment action.")
            };
            var errorCode = (uint)result;
            var installerErrorCode = (uint)(result >> 32);
            deployments.Add(new(id,
                                action,
                                record.Flags & EngineMask,
                                record.Name,
                                string.IsNullOrEmpty(record.Description) ? null : record.Description,
                                state,
                                (record.State & ProgressMask) >> ProgressShift,
                                (record.Flags & RebootRequired) != 0,
                                metadata.CreatedTime,
                                completedTime,
                                errorCode == 0 ? null : errorCode,
                                installerErrorCode == 0 ? null : installerErrorCode,
                                string.IsNullOrEmpty(record.Detail) ? null : record.Detail));
        }
        return [.. deployments.OrderByDescending(job => job.CreatedTime)];
    }

    private async Task<SoftwareDeployment> StartAsync(
        SoftwareDeploymentAction action,
        AdministrationAction nativeAction,
        uint flags,
        string name,
        string? identity,
        string[] cleanupPaths,
        string[] payload)
    {
        ObjectDisposedException.ThrowIf(disposed != 0, this);
        await gate.WaitAsync();
        try
        {
            lock (stagingPaths)
            {
                if (cleanupPaths.Any(path => !stagingPaths.Contains(path)))
                {
                    throw new InvalidOperationException("Unknown software staging path.");
                }
            }
            var id = Guid.NewGuid();
            using var selection = server.SelectClient(clientId);
            await server.ControlAdministrationStringDataAsync(AdministrationOperation.ControlSoftware,
                                                               nativeAction,
                                                               flags,
                                                               id.ToString("D"),
                                                               PackStrings(payload));
            lock (stagingPaths)
            {
                foreach (var path in cleanupPaths) stagingPaths.Remove(path);
            }
            var metadata = new SoftwareDeploymentMetadata(DateTime.UtcNow, cleanupPaths);
            jobs[id] = metadata;
            return new(id,
                       action,
                       flags & EngineMask,
                       name,
                       identity,
                       SoftwareDeploymentState.Queued,
                       0,
                       false,
                       metadata.CreatedTime,
                       null,
                       null,
                       null,
                       null);
        }
        finally
        {
            gate.Release();
        }
    }

    private async Task CleanupAsync(SoftwareDeploymentMetadata metadata)
    {
        string[] paths;
        List<string>? failed = null;
        lock (metadata)
        {
            paths = metadata.CleanupPaths;
            metadata.CleanupPaths = [];
        }
        foreach (var path in paths)
        {
            try
            {
                await server.DeleteFileAsync(path);
            }
            catch (NativeException)
            {
                (failed ??= []).Add(path);
            }
        }
        if (failed is not null)
        {
            lock (metadata) metadata.CleanupPaths = [.. metadata.CleanupPaths, .. failed];
        }
    }

    private static string PackStrings(string[] values)
    {
        var length = values.Sum(value => checked(value.Length + 1));
        return string.Create(length, values, static (destination, source) =>
        {
            var offset = 0;
            foreach (var value in source)
            {
                value.AsSpan().CopyTo(destination[offset..]);
                offset += value.Length;
                destination[offset++] = '\0';
            }
        });
    }

    private static uint GetPackageEngine(string path) => Path.GetExtension(path).ToLowerInvariant() switch
    {
        ".msi" => EngineMsi,
        ".appinstaller" => EngineAppInstaller,
        ".appx" or ".appxbundle" or ".msix" or ".msixbundle" => EngineAppx,
        _ => throw new ArgumentException("Unsupported software package type.")
    };

    private static uint GetProviderEngine(string provider) => provider.ToLowerInvariant() switch
    {
        "pip" => EnginePip,
        "npm" => EngineNpm,
        "chocolatey" => EngineChocolatey,
        "dotnet" => EngineDotNetTool,
        _ => throw new ArgumentException("Unsupported package provider.", nameof(provider))
    };

    public void Dispose()
    {
        if (Interlocked.Exchange(ref disposed, 1) == 0) gate.Dispose();
    }

    private sealed class SoftwareDeploymentMetadata(DateTime createdTime, string[] cleanupPaths)
    {
        internal DateTime CreatedTime { get; } = createdTime;
        internal DateTime? CompletedTime;
        internal string[] CleanupPaths = cleanupPaths;
    }
}

internal enum SoftwareDeploymentAction : byte
{
    Install = 1,
    Upgrade,
    Uninstall,
    UpgradeAll
}

internal enum SoftwareDeploymentState : byte
{
    Queued = 1,
    Resolving,
    Downloading,
    Installing,
    Completed,
    Failed
}

internal sealed record SoftwareDeployment(
    Guid Id,
    SoftwareDeploymentAction Action,
    uint Engine,
    string Name,
    string? Identity,
    SoftwareDeploymentState State,
    uint Progress,
    bool RebootRequired,
    DateTime CreatedTime,
    DateTime? CompletedTime,
    uint? ErrorCode,
    uint? InstallerErrorCode,
    string? ErrorText);
