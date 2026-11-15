using System.ComponentModel;
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Xml;
using System.Xml.Linq;

namespace KNSoft.ZPigeon.Web;

internal sealed class ClientBuildService : IAsyncDisposable
{
    internal const string ExecutableName = "KNSoft.ZPigeon.Client.exe";
    private const int MaximumJobs = 32;
    private readonly Lock sync = new();
    private readonly Dictionary<Guid, ClientBuildJob> jobs = [];
    private readonly Dictionary<string, List<ClientBuildInstance>> instances = new(StringComparer.Ordinal);
    private readonly Dictionary<string, ClientBuildPackageFingerprint> packageFingerprints =
        new(StringComparer.Ordinal);
    private readonly Queue<ClientBuildJob> queue = [];
    private readonly SemaphoreSlim pending = new(0);
    private readonly CancellationTokenSource stopping = new();
    private readonly string root = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "KNSoft", "ZPigeon", "ClientBuild");
    private readonly string? source = FindSource();
    private readonly Task worker;

    internal ClientBuildService()
    {
        worker = ProcessQueueAsync();
    }

    internal async Task<string?> GetAvailabilityAsync(CancellationToken cancellationToken)
    {
        if (source is null) return "Source solution is unavailable. Run Web from a repository build.";
        try
        {
            _ = await ResolveToolsAsync(cancellationToken);
            return null;
        }
        catch (Exception exception) when (exception is IOException or Win32Exception or InvalidOperationException)
        {
            return exception.Message;
        }
        catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
        {
            return "Visual Studio discovery timed out.";
        }
    }

    internal bool TryEnqueue(ClientBuildJob job)
    {
        lock (sync)
        {
            foreach (var expired in jobs.Values.Where(value => value.IsFinished)
                                                 .OrderBy(value => value.Created)
                                                 .Take(Math.Max(0, jobs.Count - MaximumJobs + 1))
                                                 .ToArray())
            {
                jobs.Remove(expired.Id);
                expired.Cancellation.Dispose();
            }
            if (queue.Count == 4) return false;
            jobs.Add(job.Id, job);
            queue.Enqueue(job);
            pending.Release();
            return true;
        }
    }

    internal ClientBuildJob Get(Guid id)
    {
        lock (sync) return jobs.TryGetValue(id, out var job) ? job :
            throw new KeyNotFoundException("Build job has expired or does not exist.");
    }

    internal ClientBuildInstance[] GetInstances(string deploymentId)
    {
        lock (sync) return LoadInstances(deploymentId).ToArray();
    }

    private List<ClientBuildInstance> LoadInstances(string deploymentId)
    {
        if (instances.TryGetValue(deploymentId, out var values)) return values;
        var path = Path.Combine(root, "instances", deploymentId + ".json");
        values = File.Exists(path) ?
            JsonSerializer.Deserialize<List<ClientBuildInstance>>(File.ReadAllBytes(path), JsonSerializerOptions.Web) ??
                throw new InvalidOperationException("Client instance records are invalid.") : [];
        instances.Add(deploymentId, values);
        return values;
    }

    private void RecordInstance(ClientBuildJob job)
    {
        lock (sync)
        {
            var values = LoadInstances(job.DeploymentId);
            values.RemoveAll(value => value.InstanceId == job.InstanceId);
            values.Add(new(job.InstanceId, job.Group));
            if (values.Count > 1000) values.RemoveRange(0, values.Count - 1000);
            var directory = Path.Combine(root, "instances");
            Directory.CreateDirectory(directory);
            var path = Path.Combine(directory, job.DeploymentId + ".json");
            var temporary = path + ".tmp";
            File.WriteAllBytes(temporary, JsonSerializer.SerializeToUtf8Bytes(values, JsonSerializerOptions.Web));
            File.Move(temporary, path, true);
        }
    }

    internal bool Cancel(Guid id)
    {
        lock (sync)
        {
            var job = Get(id);
            if (!job.TryCancel()) return false;
            var waiting = queue.Where(value => value.Id != id).ToArray();
            queue.Clear();
            foreach (var value in waiting) queue.Enqueue(value);
            return true;
        }
    }

    private async Task ProcessQueueAsync()
    {
        try
        {
            while (true)
            {
                await pending.WaitAsync(stopping.Token);
                ClientBuildJob job;
                lock (sync)
                {
                    if (queue.Count == 0) continue;
                    job = queue.Dequeue();
                    job.Update("preparing", "Preparing build inputs.");
                }
                using var cancellation = CancellationTokenSource.CreateLinkedTokenSource(
                    stopping.Token, job.Cancellation.Token);
                cancellation.CancelAfter(TimeSpan.FromMinutes(20));
                try
                {
                    cancellation.Token.ThrowIfCancellationRequested();
                    await BuildAsync(job, cancellation.Token);
                }
                catch (OperationCanceledException)
                {
                    if (stopping.IsCancellationRequested || job.Cancellation.IsCancellationRequested)
                        job.Update("canceled", "Build canceled.");
                    else
                        job.Update("failed", "Build timed out.", "Build exceeded the 20 minute limit.");
                }
                catch (Exception exception) when (exception is IOException or Win32Exception or
                                                               InvalidOperationException or JsonException or
                                                               BadImageFormatException or TimeoutException or
                                                               UnauthorizedAccessException or XmlException)
                {
                    if (job.Cancellation.IsCancellationRequested || stopping.IsCancellationRequested)
                        job.Update("canceled", "Build canceled.");
                    else
                        job.Update("failed", "Build failed.", exception.Message);
                }
            }
        }
        catch (OperationCanceledException) when (stopping.IsCancellationRequested)
        {
        }
    }

    private async Task BuildAsync(ClientBuildJob job, CancellationToken cancellationToken)
    {
        if (source is null) throw new InvalidOperationException("Source solution is unavailable.");
        job.Update("preparing", "Resolving Visual Studio 2026 and reading source inputs.");
        var tools = await ResolveToolsAsync(cancellationToken);
        var files = EnumerateInputs(source).Order(StringComparer.Ordinal).ToArray();
        var dependencies = ReadDependencies(source, files);
        var packages = await GetPackageFingerprintsAsync(dependencies, cancellationToken);
        var macros = ClientBuildCatalog.GetMacros(job.Selection);
        var identity = $"ClientBuild.v1\n{tools.Identity}\n{string.Join('\n', macros)}\n" +
                       string.Join('\n', packages.Select(package => $"{package.Id}:{package.Sha256}"));
        var key = await HashInputsAsync(source, files, identity, cancellationToken);
        var cacheDirectory = Path.Combine(root, "cache", key);
        var cached = await ReadCacheAsync(cacheDirectory, cancellationToken);
        if (cached is not null)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (!job.Complete(Path.Combine(cacheDirectory, ExecutableName), cached.Size, true))
                throw new OperationCanceledException(job.Cancellation.Token);
            RecordInstance(job);
            PruneCache();
            return;
        }
        var workspace = Path.Combine(root, "work", job.Id.ToString("N"));
        var snapshot = Path.Combine(workspace, "Source");
        try
        {
            job.Update("preparing", "Copying an isolated source snapshot and native dependencies.");
            foreach (var relative in files)
            {
                cancellationToken.ThrowIfCancellationRequested();
                var destination = Path.Combine(snapshot, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                File.Copy(Path.Combine(source, relative), destination);
            }
            if (await HashInputsAsync(snapshot, files, identity, cancellationToken) != key)
            {
                throw new InvalidOperationException(
                    "Source changed while creating the build snapshot. Start a new build.");
            }
            foreach (var package in packages)
            {
                var packageSnapshot = Path.Combine(snapshot, "packages", package.Id);
                await CopyDirectoryAsync(Path.Combine(source, "packages", package.Id),
                                         packageSnapshot, cancellationToken);
                var packageFiles = EnumerateInputs(packageSnapshot, excludeBuildDirectories: false)
                                   .Order(StringComparer.Ordinal).ToArray();
                if (await HashInputsAsync(packageSnapshot, packageFiles, "NativePackage.v1", cancellationToken) !=
                    package.Sha256)
                    throw new InvalidOperationException(
                        $"Native dependency changed while copying: {package.Id}. Start a new build.");
            }
            job.Update("building", "Building the full solution: Release x64.");
            var start = new ProcessStartInfo(tools.MSBuild)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8,
                StandardErrorEncoding = Encoding.UTF8,
                WorkingDirectory = snapshot
            };
            foreach (var argument in new[]
            {
                Path.Combine(snapshot, "KNSoft.ZPigeon.slnx"), "/restore", "/t:Rebuild", "/m",
                "/p:Configuration=Release", "/p:Platform=x64", "/nr:false", "/nologo", "/v:minimal"
            })
            {
                start.ArgumentList.Add(argument);
            }
            start.Environment["CL"] = string.Join(' ', macros.Select(macro => $"/D{macro}"));
            start.Environment.Remove("_CL_");
            var exitCode = await RunAsync(start, job.AppendLog, cancellationToken);
            if (exitCode != 0)
                throw new InvalidOperationException($"MSBuild exited with code {exitCode}. See the build log.");
            var output = Path.Combine(snapshot, "OutDir", "x64", "Release");
            var executable = Path.Combine(output, ExecutableName);
            _ = ClientBuildWebApi.ReadTemplate(executable);
            Directory.CreateDirectory(cacheDirectory);
            File.Copy(executable, Path.Combine(cacheDirectory, ExecutableName), true);
            foreach (var name in job.RuntimeFiles)
            {
                var runtimePath = Path.Combine(output, name);
                if (!File.Exists(runtimePath))
                    throw new InvalidOperationException($"Required runtime file is missing: {name}.");
                File.Copy(runtimePath, Path.Combine(cacheDirectory, name), true);
            }
            var size = checked((int)new System.IO.FileInfo(executable).Length);
            await using var image = File.OpenRead(executable);
            var manifest = new ClientBuildCacheManifest(
                size, Convert.ToHexString(await SHA256.HashDataAsync(image, cancellationToken)), job.RuntimeFiles);
            await File.WriteAllBytesAsync(Path.Combine(cacheDirectory, "manifest.json"),
                JsonSerializer.SerializeToUtf8Bytes(manifest, JsonSerializerOptions.Web), cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();
            if (!job.Complete(Path.Combine(cacheDirectory, ExecutableName), size, false))
                throw new OperationCanceledException(job.Cancellation.Token);
            RecordInstance(job);
            PruneCache();
        }
        finally
        {
            DeleteWorkDirectory(workspace);
        }
    }

    private static async Task<ClientBuildCacheManifest?> ReadCacheAsync(
        string directory, CancellationToken cancellationToken)
    {
        var manifestPath = Path.Combine(directory, "manifest.json");
        if (!File.Exists(manifestPath)) return null;
        var manifest = JsonSerializer.Deserialize<ClientBuildCacheManifest>(
            await File.ReadAllBytesAsync(manifestPath, cancellationToken), JsonSerializerOptions.Web);
        var executable = Path.Combine(directory, ExecutableName);
        if (manifest is null || !File.Exists(executable) ||
            manifest.RuntimeFiles.Any(name => !File.Exists(Path.Combine(directory, name)))) return null;
        await using var image = File.OpenRead(executable);
        if (image.Length != manifest.Size ||
            Convert.ToHexString(await SHA256.HashDataAsync(image, cancellationToken)) != manifest.Sha256) return null;
        _ = ClientBuildWebApi.ReadTemplate(executable);
        return manifest;
    }

    private static async Task<string> HashInputsAsync(
        string directory, string[] files, string identity, CancellationToken cancellationToken)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        hash.AppendData(Encoding.UTF8.GetBytes(identity));
        foreach (var relative in files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            hash.AppendData(Encoding.UTF8.GetBytes($"\n{relative.Replace('\\', '/')}\0"));
            await using var input = File.OpenRead(Path.Combine(directory, relative));
            hash.AppendData(await SHA256.HashDataAsync(input, cancellationToken));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    private async Task<ClientBuildPackageFingerprint[]> GetPackageFingerprintsAsync(
        string[] dependencies, CancellationToken cancellationToken)
    {
        foreach (var expired in packageFingerprints.Keys
                                               .Where(id => !dependencies.Contains(id, StringComparer.Ordinal))
                                               .ToArray())
            packageFingerprints.Remove(expired);
        var results = new List<ClientBuildPackageFingerprint>(dependencies.Length);
        foreach (var dependency in dependencies)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (dependency.Any(character => !char.IsAsciiLetterOrDigit(character) &&
                                            character is not ('.' or '-' or '+')))
                throw new InvalidOperationException("Invalid native package identifier.");
            var directory = Path.Combine(source!, "packages", dependency);
            if (!Directory.Exists(directory))
                throw new InvalidOperationException(
                    $"Native dependency is missing: {dependency}. Restore the full solution first.");
            var files = EnumerateInputs(directory, excludeBuildDirectories: false)
                        .Order(StringComparer.Ordinal).ToArray();
            var inventory = HashPackageInventory(directory, files, cancellationToken);
            if (!packageFingerprints.TryGetValue(dependency, out var fingerprint) || fingerprint.Inventory != inventory)
            {
                var content = await HashInputsAsync(directory, files, "NativePackage.v1", cancellationToken);
                var currentFiles = EnumerateInputs(directory, excludeBuildDirectories: false)
                                   .Order(StringComparer.Ordinal).ToArray();
                if (HashPackageInventory(directory, currentFiles, cancellationToken) != inventory)
                    throw new InvalidOperationException(
                        $"Native dependency changed while hashing: {dependency}. Start a new build.");
                fingerprint = new(dependency, inventory, content);
                packageFingerprints[dependency] = fingerprint;
            }
            results.Add(fingerprint);
        }
        return results.ToArray();
    }

    private static string[] ReadDependencies(string directory, string[] files)
    {
        var dependencies = new List<string>();
        foreach (var relative in files.Where(path => Path.GetFileName(path) == "packages.config"))
        {
            var root = XDocument.Load(Path.Combine(directory, relative)).Root;
            if (root is null || root.Name != "packages")
                throw new InvalidOperationException($"Invalid native package manifest: {relative}.");
            foreach (var package in root.Elements())
            {
                var id = package.Attribute("id")?.Value;
                var version = package.Attribute("version")?.Value;
                if (package.Name != "package" || string.IsNullOrEmpty(id) || string.IsNullOrEmpty(version))
                    throw new InvalidOperationException($"Invalid native package manifest: {relative}.");
                dependencies.Add($"{id}.{version}");
            }
        }
        return dependencies.Distinct(StringComparer.Ordinal).Order(StringComparer.Ordinal).ToArray();
    }

    private static string HashPackageInventory(string directory, string[] files, CancellationToken cancellationToken)
    {
        using var hash = IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        foreach (var relative in files)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var file = new System.IO.FileInfo(Path.Combine(directory, relative));
            hash.AppendData(Encoding.UTF8.GetBytes(FormattableString.Invariant(
                $"{relative.Replace('\\', '/')}\0{file.Length}\0{file.LastWriteTimeUtc.Ticks}\n")));
        }
        return Convert.ToHexString(hash.GetHashAndReset());
    }

    private static IEnumerable<string> EnumerateInputs(
        string directory, string prefix = "", bool excludeBuildDirectories = true)
    {
        foreach (var entry in Directory.EnumerateFileSystemEntries(Path.Combine(directory, prefix)))
        {
            var name = Path.GetFileName(entry);
            var attributes = File.GetAttributes(entry);
            if ((attributes & FileAttributes.ReparsePoint) != 0) continue;
            if ((attributes & FileAttributes.Directory) != 0)
            {
                if (name == ".git" || excludeBuildDirectories &&
                    name is (".vs" or "OutDir" or "IntDir" or "obj" or "bin" or "packages")) continue;
                foreach (var relative in EnumerateInputs(
                    directory, Path.Combine(prefix, name), excludeBuildDirectories))
                    yield return relative;
            }
            else if (name != ".git" && !name.EndsWith(".user", StringComparison.OrdinalIgnoreCase) &&
                     !name.EndsWith(".aps", StringComparison.OrdinalIgnoreCase))
            {
                yield return Path.Combine(prefix, name);
            }
        }
    }

    private static async Task CopyDirectoryAsync(string source, string destination, CancellationToken cancellationToken)
    {
        foreach (var relative in EnumerateInputs(source, excludeBuildDirectories: false))
        {
            cancellationToken.ThrowIfCancellationRequested();
            var path = Path.Combine(destination, relative);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            await using var input = File.OpenRead(Path.Combine(source, relative));
            await using var output = File.Create(path);
            await input.CopyToAsync(output, cancellationToken);
        }
    }

    private void DeleteWorkDirectory(string path)
    {
        var parent = Path.GetFullPath(Path.Combine(root, "work")) + Path.DirectorySeparatorChar;
        var absolute = Path.GetFullPath(path);
        if (!absolute.StartsWith(parent, StringComparison.OrdinalIgnoreCase) ||
            !Guid.TryParseExact(Path.GetFileName(absolute), "N", out _))
            throw new InvalidOperationException("Invalid build workspace.");
        if (Directory.Exists(absolute)) Directory.Delete(absolute, true);
    }

    private void PruneCache()
    {
        var parent = Path.GetFullPath(Path.Combine(root, "cache")) + Path.DirectorySeparatorChar;
        string[] retained;
        lock (sync) retained = jobs.Values.Where(job => job.TemplatePath is not null)
                                  .Select(job => Path.GetDirectoryName(job.TemplatePath!)!).ToArray();
        var directories = Directory.EnumerateDirectories(parent)
                                   .OrderByDescending(Directory.GetLastWriteTimeUtc).ToArray();
        var remaining = directories.Length;
        foreach (var directory in directories.Reverse())
        {
            if (remaining <= MaximumJobs) break;
            var absolute = Path.GetFullPath(directory);
            var name = Path.GetFileName(absolute);
            if (!absolute.StartsWith(parent, StringComparison.OrdinalIgnoreCase) || name.Length != 64 ||
                name.Any(character => !Uri.IsHexDigit(character)) ||
                retained.Contains(absolute, StringComparer.OrdinalIgnoreCase)) continue;
            Directory.Delete(absolute, true);
            remaining--;
        }
    }

    private static string? FindSource()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory);
             directory is not null; directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, "KNSoft.ZPigeon.slnx"))) return directory.FullName;
        }
        return null;
    }

    private static async Task<ClientBuildTools> ResolveToolsAsync(CancellationToken cancellationToken)
    {
        var vswhere = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ProgramFilesX86),
                                   "Microsoft Visual Studio", "Installer", "vswhere.exe");
        if (!File.Exists(vswhere))
            throw new InvalidOperationException("Visual Studio Installer / vswhere is unavailable.");
        var output = new StringBuilder();
        var start = new ProcessStartInfo(vswhere)
        {
            UseShellExecute = false, CreateNoWindow = true, RedirectStandardOutput = true, RedirectStandardError = true,
            StandardOutputEncoding = Encoding.UTF8, StandardErrorEncoding = Encoding.UTF8
        };
        foreach (var argument in new[]
        {
            "-utf8", "-products", "*", "-version", "[18.0,19.0)", "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-latest", "-property", "installationPath"
        }) start.ArgumentList.Add(argument);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(10));
        var exitCode = await RunAsync(start, line =>
        {
            lock (output) { if (output.Length < 16384) output.AppendLine(line); }
        }, timeout.Token);
        var installation = output.ToString().Trim();
        if (exitCode != 0 || !Directory.Exists(installation))
            throw new InvalidOperationException("Visual Studio 2026 with the C++ x64 toolchain is unavailable.");
        var msbuild = Path.Combine(installation, "MSBuild", "Current", "Bin", "MSBuild.exe");
        var version = (await File.ReadAllTextAsync(
            Path.Combine(installation, "VC", "Auxiliary", "Build", "Microsoft.VCToolsVersion.default.txt"),
            cancellationToken)).Trim();
        if (version.Any(character => !char.IsAsciiDigit(character) && character != '.'))
            throw new InvalidOperationException("Invalid Visual Studio compiler version.");
        var compiler = Path.Combine(installation, "VC", "Tools", "MSVC", version, "bin", "Hostx64", "x64", "cl.exe");
        if (!File.Exists(msbuild) || !File.Exists(compiler))
            throw new InvalidOperationException("Visual Studio 2026 build tools are incomplete.");
        return new(msbuild, $"{FileVersionInfo.GetVersionInfo(msbuild).FileVersion}\n" +
                            FileVersionInfo.GetVersionInfo(compiler).FileVersion);
    }

    private static async Task<int> RunAsync(
        ProcessStartInfo start, Action<string> log, CancellationToken cancellationToken)
    {
        using var process = new Process { StartInfo = start };
        if (!process.Start()) throw new InvalidOperationException("Build process could not be started.");
        using var cancellation = cancellationToken.Register(() => Kill(process));
        async Task ReadAsync(StreamReader reader)
        {
            while (await reader.ReadLineAsync(cancellationToken) is { } line) log(line[..Math.Min(line.Length, 8192)]);
        }
        var output = ReadAsync(process.StandardOutput);
        var error = ReadAsync(process.StandardError);
        try
        {
            await process.WaitForExitAsync(cancellationToken);
            await Task.WhenAll(output, error).WaitAsync(TimeSpan.FromSeconds(5), cancellationToken);
            return process.ExitCode;
        }
        finally
        {
            if (!process.HasExited) Kill(process);
            try { await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(5)); }
            catch (TimeoutException) { }
        }
    }

    private static void Kill(Process process)
    {
        try { if (!process.HasExited) process.Kill(entireProcessTree: true); }
        catch (Exception exception) when (exception is InvalidOperationException or Win32Exception) { }
    }

    public async ValueTask DisposeAsync()
    {
        stopping.Cancel();
        await worker;
        lock (sync)
        {
            foreach (var job in jobs.Values) job.Cancellation.Dispose();
        }
        stopping.Dispose();
        pending.Dispose();
    }
}

internal sealed class ClientBuildJob(
    ClientBuildSelection selection, string group, string instanceId, string keyName,
    byte[] certificate, bool requireTcpTls13)
{
    private readonly Lock sync = new();
    private readonly StringBuilder log = new();
    private TaskCompletionSource changed = new(TaskCreationOptions.RunContinuationsAsynchronously);
    private long revision;
    internal Guid Id { get; } = Guid.NewGuid();
    internal DateTimeOffset Created { get; } = DateTimeOffset.UtcNow;
    internal CancellationTokenSource Cancellation { get; } = new();
    internal ClientBuildSelection Selection { get; } = selection;
    internal string Group { get; } = group;
    internal string InstanceId { get; } = instanceId;
    internal string KeyName { get; } = keyName;
    internal string DeploymentId { get; } = keyName.Split('.')[3];
    internal byte[] Certificate { get; } = certificate;
    internal bool RequireTcpTls13 { get; } = requireTcpTls13;
    internal string[] RuntimeFiles { get; } = ClientBuildCatalog.GetRuntimeFiles(selection);
    internal string State { get; private set; } = "queued";
    internal string Stage { get; private set; } = "Waiting for the build worker.";
    internal string? Error { get; private set; }
    internal string? TemplatePath { get; private set; }
    internal int ExecutableSize { get; private set; }
    internal bool CacheHit { get; private set; }
    internal bool IsFinished => State is "completed" or "failed" or "canceled";

    internal bool TryCancel()
    {
        lock (sync)
        {
            if (IsFinished) return false;
            Cancellation.Cancel();
            if (State == "queued") State = "canceled";
            Stage = State == "canceled" ? "Build canceled." : "Canceling the build process.";
            Signal();
            return true;
        }
    }

    internal void Update(string state, string stage, string? error = null)
    {
        lock (sync)
        {
            if (state == "failed" && Cancellation.IsCancellationRequested)
            {
                state = "canceled";
                stage = "Build canceled.";
                error = null;
            }
            State = state;
            Stage = stage;
            Error = error;
            Signal();
        }
    }

    internal bool Complete(string template, int size, bool cacheHit)
    {
        lock (sync)
        {
            if (Cancellation.IsCancellationRequested) return false;
            TemplatePath = template;
            ExecutableSize = size;
            CacheHit = cacheHit;
            State = "completed";
            Stage = cacheHit ? "Using the cached Release x64 template." : "Build completed.";
            Signal();
            return true;
        }
    }

    internal void AppendLog(string line)
    {
        lock (sync)
        {
            log.AppendLine(line.TrimEnd('\r'));
            if (log.Length > 262144) log.Remove(0, log.Length - 262144);
            Signal();
        }
    }

    private void Signal()
    {
        revision++;
        var previous = changed;
        changed = new(TaskCreationOptions.RunContinuationsAsynchronously);
        previous.TrySetResult();
    }

    internal ClientBuildJobView GetView()
    {
        lock (sync) return new(Id, State, Stage, revision, log.ToString(), Error, InstanceId, KeyName, Group,
                              Selection.Modules, Selection.Transports, Selection.ExecutionOptions,
                              RequireTcpTls13, CacheHit, ExecutableSize, RuntimeFiles);
    }

    internal Task WaitAsync(long previousRevision, CancellationToken cancellationToken)
    {
        lock (sync) return revision != previousRevision ?
            Task.CompletedTask : changed.Task.WaitAsync(cancellationToken);
    }
}

internal sealed record ClientBuildJobView(
    Guid Id, string State, string Stage, long Revision, string Log, string? Error,
    string InstanceId, string KeyName, string Group, int[] Modules, int[] Transports, string[] ExecutionOptions,
    bool RequireTcpTls13, bool CacheHit, int ExecutableSize, string[] RuntimeFiles);
internal sealed record ClientBuildCacheManifest(int Size, string Sha256, string[] RuntimeFiles);
internal sealed record ClientBuildPackageFingerprint(string Id, string Inventory, string Sha256);
internal sealed record ClientBuildTools(string MSBuild, string Identity);
