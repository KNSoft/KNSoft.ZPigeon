using KNSoft.ZPigeon.Server.Managed;
using System.Net;

namespace KNSoft.ZPigeon.Web;

internal static class RemoteAccessWebApi
{
    internal static void MapRemoteAccessApi(
        this WebApplication app,
        ClientServicesRegistry services,
        string proxyUserHeader)
    {
        var server = services.Server;
        var rdpPatches = new RdpPatchManager(
            server,
            Path.Combine(AppContext.BaseDirectory, "3rdParty", "rdpwrap.ini", "rdpwrap.ini"));
        app.Lifetime.ApplicationStopped.Register(rdpPatches.Dispose);
        app.MapPost("/api/remote/rdp", (HttpContext context, RdpForwardRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader) ||
                context.Connection.RemoteIpAddress is not IPAddress sourceAddress || request.Port == 0)
            {
                return request.Port == 0 ? Results.BadRequest() : Results.Unauthorized();
            }
            return Results.Ok(services.Current.RdpForwards.Create(sourceAddress, request.Port));
        });
        app.MapPost("/api/remote/rdp/status", async (HttpContext context) =>
            IsAuthenticated(context, proxyUserHeader) ?
                Results.Ok(await rdpPatches.GetStatusAsync()) :
                Results.Unauthorized());
        app.MapPost("/api/remote/rdp/settings", async (HttpContext context, RdpSettingsRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            if (request.Port == 0) return Results.BadRequest();
            await rdpPatches.ConfigureAsync(request.Enabled,
                                            request.Port,
                                            request.Nla,
                                            request.SameUserMultipleSessions);
            return Results.NoContent();
        });
        app.MapPost("/api/remote/rdp/patch", async (HttpContext context, RdpPatchRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            try
            {
                await rdpPatches.SetEnabledAsync(request.Enabled);
                return Results.NoContent();
            }
            catch (NotSupportedException exception)
            {
                return Results.BadRequest(new { exception.Message });
            }
        });
        app.MapPost("/api/remote/desktop/image", async (HttpContext context, DesktopCaptureRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            return Results.File(
                await server.CaptureWindowAsync(0,
                                                0,
                                                0,
                                                new(request.CaptureCursor,
                                                    request.MaxDimension,
                                                    request.FrameRate,
                                                    request.ImageQuality,
                                                    true,
                                                    request.MonitorIndex)),
                "image/jpeg");
        });
        app.MapPost("/api/remote/desktop/monitors", async (HttpContext context) =>
            IsAuthenticated(context, proxyUserHeader) ?
                Results.Ok(await server.EnumerateMonitorsAsync()) :
                Results.Unauthorized());
        app.Map("/api/remote/desktop", async context =>
        {
            if (!IsAuthenticated(context, proxyUserHeader))
            {
                context.Response.StatusCode = StatusCodes.Status401Unauthorized;
                return;
            }
            await WindowCaptureWebSocket.RunAsync(
                context,
                server,
                services.Current.ConnectionPerformance);
        });
        app.MapPost("/api/remote/forward/{id:guid}", (
            HttpContext context,
            Guid id) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader) ||
                context.Connection.RemoteIpAddress is not IPAddress sourceAddress)
            {
                return Results.Unauthorized();
            }
            var info = services.Current.RdpForwards.Get(id, sourceAddress) ??
                       services.Current.TcpForwards.Get(id, sourceAddress) ??
                       services.Current.UdpForwards.Get(id, sourceAddress);
            return info is null ? Results.NotFound() : Results.Ok(info);
        });
        app.MapPost("/api/remote/forwards", (HttpContext context) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader) ||
                context.Connection.RemoteIpAddress is not IPAddress sourceAddress)
            {
                return Results.Unauthorized();
            }
            return Results.Ok(new
            {
                SourceAddress = sourceAddress.ToString(),
                Rules = services.Current.RdpForwards.GetAll(sourceAddress)
                    .Concat(services.Current.TcpForwards.GetAll(sourceAddress)
                        .Where(rule => rule.Kind != "RDP"))
                    .Concat(services.Current.UdpForwards.GetAll(sourceAddress)
                        .Where(rule => rule.Kind != "RDP"))
                    .OrderBy(rule => rule.Port)
            });
        });
        app.MapPost("/api/remote/forward", (
            HttpContext context,
            PortForwardRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader) ||
                context.Connection.RemoteIpAddress is not IPAddress sourceAddress)
            {
                return Results.Unauthorized();
            }
            var host = request.Host?.Trim();
            var kind = request.Protocol == "tcp" ? request.Kind switch
            {
                "tcp" => "TCP",
                "rdp" => "RDP",
                "cdp" => "CDP",
                "windbg-process" => "WinDbgProcess",
                "windbg-server" => "WinDbgServer",
                _ => null
            } : request.Protocol == "udp" && request.Kind == "tcp" ? "UDP" : null;
            if (host is null || host.Length is 0 or > 255 || host.Contains('\0') ||
                kind is null || request.Port == 0 ||
                !IPAddress.TryParse(request.SourceAddress, out var allowedSourceAddress) ||
                request.IdleTimeoutSeconds is < 1 or > 86400)
            {
                return Results.BadRequest();
            }
            var timeout = TimeSpan.FromSeconds(request.IdleTimeoutSeconds);
            return Results.Ok(request.Protocol == "tcp" ?
                services.Current.TcpForwards.Create(sourceAddress,
                                   allowedSourceAddress,
                                   kind,
                                   host,
                                   request.Port,
                                   timeout) :
                services.Current.UdpForwards.Create(sourceAddress,
                                   allowedSourceAddress,
                                   kind,
                                   host,
                                   request.Port,
                                   timeout));
        });
        app.MapPost("/api/remote/forward/{id:guid}/close", async (
            HttpContext context,
            Guid id) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader) ||
                context.Connection.RemoteIpAddress is not IPAddress sourceAddress)
            {
                return Results.Unauthorized();
            }
            var info = services.Current.RdpForwards.Get(id, sourceAddress) ??
                       services.Current.TcpForwards.Get(id, sourceAddress) ??
                       services.Current.UdpForwards.Get(id, sourceAddress);
            if (info is null)
            {
                return Results.NotFound();
            }
            if (info.Kind == "CDP" &&
                await services.Current.CdpSessions.CloseForwardAsync(id))
            {
                return Results.NoContent();
            }
            return services.Current.RdpForwards.Close(id, sourceAddress) ||
                   services.Current.TcpForwards.Close(id, sourceAddress) ||
                   services.Current.UdpForwards.Close(id, sourceAddress) ?
                Results.NoContent() : Results.NotFound();
        });
        app.MapPost("/api/remote/cdp/browsers", async (HttpContext context) =>
            IsAuthenticated(context, proxyUserHeader) ?
                Results.Ok(await services.Current.CdpSessions.DiscoverAsync()) :
                Results.Unauthorized());
        app.MapPost("/api/remote/cdp/sessions", async (HttpContext context) =>
            IsAuthenticated(context, proxyUserHeader) ?
                Results.Ok(await services.Current.CdpSessions.GetSessionsAsync()) :
                Results.Unauthorized());
        app.MapPost("/api/remote/cdp/profile/inspect", async (
            HttpContext context,
            CdpSourceProfileRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            if (request.Browser is not { Length: > 0 and <= 32 } ||
                request.Kind is not ("source" or "managed") ||
                request.Profile is not { Length: > 0 and <= 260 })
            {
                return Results.BadRequest();
            }
            try
            {
                return Results.Ok(await services.Current.CdpSessions.InspectProfileAsync(
                    request.Browser,
                    request.Kind,
                    request.Profile));
            }
            catch (ArgumentException exception)
            {
                return Results.BadRequest(new { exception.Message });
            }
        });
        app.MapPost("/api/remote/cdp/profile/clone", async (
            HttpContext context,
            CdpCloneProfileRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            if (request.Browser is not { Length: > 0 and <= 32 } ||
                request.Kind is not ("source" or "managed") ||
                request.Profile is not { Length: > 0 and <= 260 } ||
                request.Name is not { Length: > 0 and <= 64 })
            {
                return Results.BadRequest();
            }
            try
            {
                return Results.Ok(await services.Current.CdpSessions.CloneProfileAsync(
                    request.Browser,
                    request.Kind,
                    request.Profile,
                    request.Name));
            }
            catch (ArgumentException exception)
            {
                return Results.BadRequest(new { exception.Message });
            }
        });
        app.MapPost("/api/remote/cdp/profile/delete", async (
            HttpContext context,
            CdpSourceProfileRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            if (request.Browser is not { Length: > 0 and <= 32 } || request.Kind != "managed" ||
                request.Profile is not { Length: > 0 and <= 64 })
            {
                return Results.BadRequest();
            }
            try
            {
                await services.Current.CdpSessions.DeleteProfileAsync(request.Browser, request.Profile);
                return Results.NoContent();
            }
            catch (ArgumentException exception)
            {
                return Results.BadRequest(new { exception.Message });
            }
        });
        app.MapPost("/api/remote/cdp/profile/create", async (
            HttpContext context,
            CdpCreateProfileRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            if (request.Browser is not { Length: > 0 and <= 32 } ||
                request.Name is not { Length: > 0 and <= 64 })
            {
                return Results.BadRequest();
            }
            try
            {
                return Results.Ok(await services.Current.CdpSessions.CreateProfileAsync(
                    request.Browser,
                    request.Name));
            }
            catch (ArgumentException exception)
            {
                return Results.BadRequest(new { exception.Message });
            }
        });
        app.MapPost("/api/remote/cdp/start", async (
            HttpContext context,
            CdpStartRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader) ||
                context.Connection.RemoteIpAddress is not IPAddress sourceAddress)
            {
                return Results.Unauthorized();
            }
            if (request.Browser is not { Length: > 0 and <= 32 } ||
                request.Profile is not { Length: > 0 and <= 64 })
            {
                return Results.BadRequest();
            }
            try
            {
                return Results.Ok(await services.Current.CdpSessions.StartAsync(
                    sourceAddress,
                    request.Browser,
                    request.Profile));
            }
            catch (ArgumentException exception)
            {
                return Results.BadRequest(new { exception.Message });
            }
        });
        app.MapPost("/api/remote/cdp/targets", async (
            HttpContext context,
            CdpSessionRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            try
            {
                return Results.Ok(await services.Current.CdpSessions.GetTargetsAsync(
                    request.Id,
                    context.RequestAborted));
            }
            catch (KeyNotFoundException)
            {
                return Results.NotFound();
            }
        });
        app.MapPost("/api/remote/cdp/target", async (
            HttpContext context,
            CdpCreateTargetRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            try
            {
                return Results.Ok(await services.Current.CdpSessions.CreateTargetAsync(
                    request.Id,
                    request.Url,
                    context.RequestAborted));
            }
            catch (ArgumentException exception)
            {
                return Results.BadRequest(new { exception.Message });
            }
            catch (KeyNotFoundException)
            {
                return Results.NotFound();
            }
        });
        app.MapPost("/api/remote/cdp/target/close", async (
            HttpContext context,
            CdpTargetRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader)) return Results.Unauthorized();
            try
            {
                return await services.Current.CdpSessions.CloseTargetAsync(
                           request.Id,
                           request.Target,
                           context.RequestAborted) ?
                           Results.NoContent() :
                           Results.NotFound();
            }
            catch (ArgumentException exception)
            {
                return Results.BadRequest(new { exception.Message });
            }
            catch (KeyNotFoundException)
            {
                return Results.NotFound();
            }
        });
        app.Map("/api/remote/cdp/control", async context =>
        {
            if (!IsAuthenticated(context, proxyUserHeader))
            {
                context.Response.StatusCode = StatusCodes.Status401Unauthorized;
                return;
            }
            if (!Guid.TryParse(context.Request.Query["id"], out var id) ||
                context.Request.Query["target"] is not { Count: 1 } targets ||
                context.Request.Query["width"] is not { Count: 1 } widths ||
                context.Request.Query["height"] is not { Count: 1 } heights ||
                context.Request.Query["quality"] is not { Count: 1 } qualities ||
                !int.TryParse(widths[0], out var width) ||
                !int.TryParse(heights[0], out var height) ||
                !int.TryParse(qualities[0], out var quality))
            {
                context.Response.StatusCode = StatusCodes.Status400BadRequest;
                return;
            }
            await CdpControlWebSocket.RunAsync(context,
                services.Current.CdpSessions,
                id,
                targets[0]!,
                width,
                height,
                quality);
        });
        app.MapPost("/api/remote/cdp/close", async (
            HttpContext context,
            CdpSessionRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader))
            {
                return Results.Unauthorized();
            }
            return await services.Current.CdpSessions.CloseAsync(request.Id) ?
                       Results.NoContent() :
                       Results.NotFound();
        });
    }

    internal static bool IsAuthenticated(HttpContext context, string proxyUserHeader)
    {
        if (context.User.Identity?.IsAuthenticated == true ||
            context.Connection.RemoteIpAddress is IPAddress address && IPAddress.IsLoopback(address))
        {
            return true;
        }
        var values = context.Request.Headers[proxyUserHeader];
        return values.Count == 1 && values[0]?.Trim().Length is > 0 and <= 128;
    }
}

internal sealed record CdpStartRequest(string Browser, string Profile);
internal sealed record CdpSessionRequest(Guid Id);
internal sealed record CdpSourceProfileRequest(string Browser, string Kind, string Profile);
internal sealed record CdpCloneProfileRequest(string Browser, string Kind, string Profile, string Name);
internal sealed record CdpCreateProfileRequest(string Browser, string Name);
internal sealed record CdpCreateTargetRequest(Guid Id, string Url);
internal sealed record CdpTargetRequest(Guid Id, string Target);
internal sealed record RdpForwardRequest(ushort Port);
internal sealed record RdpSettingsRequest(bool Enabled, ushort Port, bool Nla, bool SameUserMultipleSessions);
internal sealed record RdpPatchRequest(bool Enabled);
internal sealed record DesktopCaptureRequest(
    bool CaptureCursor,
    uint MaxDimension,
    byte FrameRate,
    byte ImageQuality,
    uint MonitorIndex);
internal sealed record PortForwardRequest(
    string? Kind,
    string? Protocol,
    string? SourceAddress,
    string? Host,
    ushort Port,
    uint IdleTimeoutSeconds);
