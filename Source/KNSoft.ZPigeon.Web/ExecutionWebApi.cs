using System.Text;
using System.Xml;
using System.Globalization;
using System.Security.Principal;
using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal static class ExecutionWebApi
{
    internal static void MapExecutionApi(
        this WebApplication app,
        ClientServicesRegistry services)
    {
        var server = services.Server;
        app.MapPost("/api/execution/sessions", () =>
            server.EnumerateExecutionSessionsAsync());
        app.MapPost("/api/execution/environment", () =>
            server.QueryExecutionEnvironmentAsync());
        app.MapPost("/api/execution/image", (ExecutionImageRequest request) =>
            string.IsNullOrWhiteSpace(request.Path) || request.Path.Length > 32767 || request.Path.Contains('\0') ?
                Task.FromResult<IResult>(Results.BadRequest()) :
                QueryImageAsync(server, request.Path));
        app.MapPost("/api/execution/jobs", async () =>
        {
            var jobs = await server.EnumerateExecutionJobsAsync();
            foreach (var job in jobs)
            {
                if (job.State == ExecutionJobState.Exited &&
                    services.Current.ExecutionCleanupPaths.TryRemove(job.JobId, out var path))
                {
                    try
                    {
                        await server.DeleteFileAsync(path);
                    }
                    catch (NativeException)
                    {
                    }
                }
            }
            return jobs;
        });
        app.MapPost("/api/execution/start", async (ExecutionStartRequest request) =>
        {
            if (!Enum.IsDefined(request.Engine) || !Enum.IsDefined(request.Identity))
            {
                return Results.BadRequest();
            }
            ExecutionStart start;
            try
            {
                start = request.ToExecutionStart();
            }
            catch (ArgumentException)
            {
                return Results.BadRequest();
            }
            var job = await server.StartExecutionAsync(start);
            if (!string.IsNullOrEmpty(request.CleanupPath))
            {
                services.Current.ExecutionCleanupPaths[job.JobId] = request.CleanupPath;
            }
            return Results.Ok(job);
        });
        app.MapPost("/api/terminal/run", async (TerminalExecutionRequest request) =>
        {
            if (request.Columns == 0 || request.Rows == 0)
            {
                return Results.BadRequest();
            }
            ExecutionStart start;
            try
            {
                start = request.Start.ToExecutionStart();
            }
            catch (ArgumentException)
            {
                return Results.BadRequest();
            }
            if (start.Engine != ExecutionEngine.CreateProcess)
            {
                return Results.BadRequest();
            }
            return Results.Ok(await services.Current.TerminalSessions.CreateExecutionAsync(
                start,
                request.Columns,
                request.Rows,
                Path.GetFileName(start.FileName),
                request.Start.CleanupPath));
        });
        app.MapPost("/api/execution/terminate", async (ExecutionJobRequest request) =>
        {
            if (!uint.TryParse(request.JobId, out var jobId))
            {
                return Results.BadRequest();
            }
            await server.TerminateExecutionAsync(jobId);
            return Results.NoContent();
        });
        app.MapPost("/api/execution/staging", async (ExecutionStagingRequest request) =>
        {
            var name = Path.GetFileName(request.Name);
            return name.Length is 0 or > 260 ?
                Results.BadRequest() :
                Results.Ok(new { Path = await server.CreateExecutionStagingAsync(name) });
        });
        app.MapPost("/api/sandbox/wsb/start", async (WindowsSandboxRequest request) =>
        {
            if (request.MemoryMb is > 0 and < 1024 || request.MemoryMb > 0x100000 ||
                request.LogonCommand is { Length: > 32767 } || request.LogonCommand?.Contains('\0') == true ||
                request.MappedFolders is null or { Length: > 16 } ||
                request.MappedFolders.Any(folder =>
                    string.IsNullOrWhiteSpace(folder.HostFolder) || folder.HostFolder.Length > 32767 ||
                    folder.HostFolder.Contains('\0') || !Path.IsPathFullyQualified(folder.HostFolder) ||
                    folder.SandboxFolder is { Length: > 32767 } || folder.SandboxFolder?.Contains('\0') == true))
            {
                return Results.BadRequest();
            }
            var settings = new XmlWriterSettings
            {
                Encoding = new UTF8Encoding(false),
                Indent = true,
                OmitXmlDeclaration = true
            };
            using var stream = new MemoryStream();
            using (var xml = XmlWriter.Create(stream, settings))
            {
                xml.WriteStartElement("Configuration");
                WriteSandboxSetting(xml, "vGPU", request.Vgpu);
                WriteSandboxSetting(xml, "Networking", request.Networking);
                WriteSandboxSetting(xml, "ClipboardRedirection", request.Clipboard);
                WriteSandboxSetting(xml, "AudioInput", request.AudioInput);
                WriteSandboxSetting(xml, "VideoInput", request.VideoInput);
                WriteSandboxSetting(xml, "ProtectedClient", request.ProtectedClient);
                WriteSandboxSetting(xml, "PrinterRedirection", request.Printers);
                if (request.MemoryMb != 0)
                    xml.WriteElementString("MemoryInMB", request.MemoryMb.ToString(CultureInfo.InvariantCulture));
                if (request.MappedFolders.Length != 0)
                {
                    xml.WriteStartElement("MappedFolders");
                    foreach (var folder in request.MappedFolders)
                    {
                        xml.WriteStartElement("MappedFolder");
                        xml.WriteElementString("HostFolder", folder.HostFolder);
                        if (!string.IsNullOrEmpty(folder.SandboxFolder))
                            xml.WriteElementString("SandboxFolder", folder.SandboxFolder);
                        xml.WriteElementString("ReadOnly", folder.ReadOnly ? "true" : "false");
                        xml.WriteEndElement();
                    }
                    xml.WriteEndElement();
                }
                if (!string.IsNullOrWhiteSpace(request.LogonCommand))
                {
                    xml.WriteStartElement("LogonCommand");
                    xml.WriteElementString("Command", request.LogonCommand);
                    xml.WriteEndElement();
                }
                xml.WriteEndElement();
            }
            var path = await server.CreateExecutionStagingAsync("Sandbox.wsb");
            try
            {
                await UploadAsync(server, path, stream.ToArray());
                return Results.Ok(await server.StartExecutionAsync(new ExecutionStart(
                    ExecutionEngine.ShellExecute,
                    ExecutionIdentity.Current,
                    uint.MaxValue,
                    ExecutionFlags.DeleteFile,
                    path,
                    null,
                    null,
                    "open",
                    null,
                    null,
                    null)));
            }
            catch
            {
                try
                {
                    await server.DeleteFileAsync(path);
                }
                catch (NativeException)
                {
                }
                throw;
            }
        });
        app.MapPost("/api/terminal/script", async (TerminalScriptRequest request) =>
        {
            if (!Enum.IsDefined(request.Shell) || request.Columns == 0 || request.Rows == 0 ||
                request.Script.Length is 0 or > 0x00400000)
            {
                return Results.BadRequest();
            }
            var extension = request.Extension.ToLowerInvariant();
            var valid = request.Shell switch
            {
                TerminalShell.CommandPrompt => extension is ".cmd" or ".bat",
                TerminalShell.WindowsPowerShell or TerminalShell.PowerShell => extension == ".ps1",
                TerminalShell.ConsoleScriptHost or TerminalShell.WindowsScriptHost =>
                    extension is ".vbs" or ".js" or ".wsf",
                TerminalShell.HtmlApplication => extension == ".hta",
                _ => false
            };
            if (!valid)
            {
                return Results.BadRequest();
            }
            var path = await server.CreateExecutionStagingAsync("Script" + extension);
            var encoding = request.Shell switch
            {
                TerminalShell.WindowsPowerShell or TerminalShell.HtmlApplication => new UTF8Encoding(true),
                TerminalShell.ConsoleScriptHost or TerminalShell.WindowsScriptHost => Encoding.Unicode,
                _ => new UTF8Encoding(false)
            };
            try
            {
                await UploadAsync(server, path, EncodeScript(encoding, request.Script));
                return Results.Ok(await services.Current.TerminalSessions.CreateScriptAsync(
                    request.Shell,
                    path,
                    request.Columns,
                    request.Rows));
            }
            catch
            {
                try
                {
                    await server.DeleteFileAsync(path);
                }
                catch (NativeException)
                {
                }
                throw;
            }
        });
    }

    private static void WriteSandboxSetting(XmlWriter xml, string name, bool value) =>
        xml.WriteElementString(name, value ? "Enable" : "Disable");

    private static async Task<IResult> QueryImageAsync(NativeServer server, string path) =>
        Results.Ok(await server.QueryExecutionImageAsync(path));

    private static byte[] EncodeScript(Encoding encoding, string script)
    {
        var preamble = encoding.Preamble;
        var data = GC.AllocateUninitializedArray<byte>(preamble.Length + encoding.GetByteCount(script));
        preamble.CopyTo(data);
        encoding.GetBytes(script.AsSpan(), data.AsSpan(preamble.Length));
        return data;
    }

    private static async Task UploadAsync(NativeServer server, string path, byte[] data)
    {
        await using var transfer = await server.OpenFileWriteAsync(path, (ulong)data.Length, false);
        await transfer.WriteAsync(data);
        var status = (await transfer.Completion).Status;
        if (!status.IsSuccess)
        {
            throw new NativeException(status);
        }
    }
}

internal sealed record ExecutionStartRequest(
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
    ExecutionCustomTokenRequest? CustomToken,
    string? CleanupPath)
{
    internal ExecutionStart ToExecutionStart()
    {
        if (!Enum.IsDefined(Engine) || !Enum.IsDefined(Identity) ||
            (Flags & ~(ExecutionFlags.Hidden | ExecutionFlags.DeleteFile | ExecutionFlags.JobObject)) != 0 ||
            (Flags.HasFlag(ExecutionFlags.JobObject) && Engine != ExecutionEngine.CreateProcess) ||
            string.IsNullOrWhiteSpace(FileName) || FileName.Length > 32767 || FileName.Contains('\0') ||
            Arguments is { Length: > 32767 } || Arguments?.Contains('\0') == true ||
            WorkingDirectory is { Length: > 32767 } || WorkingDirectory?.Contains('\0') == true ||
            Verb is { Length: > 64 } || Verb?.Contains('\0') == true ||
            UserName is { Length: > 256 } || UserName?.Contains('\0') == true ||
            Password is { Length: > 256 } || Password?.Contains('\0') == true ||
            AppContainerSid is { Length: > 184 } || AppContainerSid?.Contains('\0') == true ||
            (Identity == ExecutionIdentity.OtherUser ?
                string.IsNullOrWhiteSpace(UserName) || string.IsNullOrEmpty(Password) :
                !string.IsNullOrEmpty(UserName) || !string.IsNullOrEmpty(Password)) ||
            (Identity == ExecutionIdentity.AppContainer ?
                Engine != ExecutionEngine.CreateProcess || SessionId != uint.MaxValue ||
                string.IsNullOrWhiteSpace(AppContainerSid) || !string.IsNullOrEmpty(Verb) :
                !string.IsNullOrEmpty(AppContainerSid)) ||
            (Identity == ExecutionIdentity.CustomToken ?
                Engine != ExecutionEngine.CreateProcess || CustomToken is null || !string.IsNullOrEmpty(Verb) :
                CustomToken is not null) ||
            (Engine == ExecutionEngine.ShellExecute &&
                (Identity != ExecutionIdentity.Current || SessionId != uint.MaxValue)))
        {
            throw new ArgumentException(nameof(ExecutionStartRequest));
        }
        return new(Engine,
                   Identity,
                   SessionId,
                   Flags,
                   FileName,
                   Arguments,
                   WorkingDirectory,
                   Verb,
                   UserName,
                   Password,
                   AppContainerSid,
                   CustomToken?.Encode());
    }
}

internal sealed record ExecutionJobRequest(string JobId);
internal sealed record ExecutionStagingRequest(string Name);
internal sealed record ExecutionImageRequest(string Path);
internal sealed record TerminalExecutionRequest(
    ExecutionStartRequest Start,
    ushort Columns,
    ushort Rows);
internal readonly record struct ExecutionTokenGroupRequest(string Sid, uint Attributes);
internal readonly record struct ExecutionTokenPrivilegeRequest(ulong Luid, uint Attributes);
internal sealed record ExecutionCustomTokenRequest(
    ulong AuthenticationId,
    uint IntegrityRid,
    string UserSid,
    string OwnerSid,
    string PrimaryGroupSid,
    bool UiAccess,
    bool AddLogonSid,
    ExecutionTokenGroupRequest[] Groups,
    ExecutionTokenPrivilegeRequest[] Privileges)
{
    internal byte[] Encode()
    {
        if (IntegrityRid is < 0x1000 or > 0x5000 || Groups is null or { Length: > 256 } ||
            Privileges is null or { Length: > 256 })
        {
            throw new ArgumentException(nameof(ExecutionCustomTokenRequest));
        }
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, true);
        writer.Write((byte)((UiAccess ? 1 : 0) | (AddLogonSid ? 2 : 0)));
        writer.Write(AuthenticationId);
        writer.Write(IntegrityRid);
        WriteSid(writer, UserSid);
        WriteSid(writer, OwnerSid);
        WriteSid(writer, PrimaryGroupSid);
        writer.Write((ushort)Groups.Length);
        foreach (var group in Groups)
        {
            writer.Write(group.Attributes);
            WriteSid(writer, group.Sid);
        }
        writer.Write((ushort)Privileges.Length);
        foreach (var privilege in Privileges)
        {
            writer.Write(privilege.Luid);
            writer.Write(privilege.Attributes);
        }
        return stream.ToArray();
    }

    private static void WriteSid(BinaryWriter writer, string value)
    {
        var sid = new SecurityIdentifier(value);
        var bytes = GC.AllocateUninitializedArray<byte>(sid.BinaryLength);
        sid.GetBinaryForm(bytes, 0);
        writer.Write((byte)bytes.Length);
        writer.Write(bytes);
    }
}
internal sealed record WindowsSandboxFolder(string HostFolder, string? SandboxFolder, bool ReadOnly);
internal sealed record WindowsSandboxRequest(
    bool Vgpu,
    bool Networking,
    bool Clipboard,
    bool AudioInput,
    bool VideoInput,
    bool ProtectedClient,
    bool Printers,
    uint MemoryMb,
    string? LogonCommand,
    WindowsSandboxFolder[] MappedFolders);
internal sealed record TerminalScriptRequest(
    TerminalShell Shell,
    string Extension,
    string Script,
    ushort Columns,
    ushort Rows);
