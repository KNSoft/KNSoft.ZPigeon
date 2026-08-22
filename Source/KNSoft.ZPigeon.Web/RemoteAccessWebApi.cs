using KNSoft.ZPigeon.Server.Managed;
using System.Net;

namespace KNSoft.ZPigeon.Web;

internal static class RemoteAccessWebApi
{
    internal static void MapRemoteAccessApi(
        this WebApplication app,
        TcpForwardManager tcpForwards,
        UdpForwardManager udpForwards,
        RdpForwardManager rdpForwards,
        CdpSessionManager cdp,
        NativeServer server,
        string proxyUserHeader)
    {
        app.MapPost("/api/remote/rdp", (HttpContext context, RdpForwardRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader) ||
                context.Connection.RemoteIpAddress is not IPAddress sourceAddress || request.Port == 0)
            {
                return request.Port == 0 ? Results.BadRequest() : Results.Unauthorized();
            }
            return Results.Ok(rdpForwards.Create(sourceAddress, request.Port));
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
            await WindowCaptureWebSocket.RunAsync(context, server);
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
            var info = rdpForwards.Get(id, sourceAddress) ?? tcpForwards.Get(id, sourceAddress) ??
                       udpForwards.Get(id, sourceAddress);
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
                Rules = rdpForwards.GetAll(sourceAddress)
                    .Concat(tcpForwards.GetAll(sourceAddress).Where(rule => rule.Kind != "RDP"))
                    .Concat(udpForwards.GetAll(sourceAddress).Where(rule => rule.Kind != "RDP"))
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
                tcpForwards.Create(sourceAddress,
                                   allowedSourceAddress,
                                   kind,
                                   host,
                                   request.Port,
                                   timeout) :
                udpForwards.Create(sourceAddress,
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
            var info = rdpForwards.Get(id, sourceAddress) ?? tcpForwards.Get(id, sourceAddress) ??
                       udpForwards.Get(id, sourceAddress);
            if (info is null)
            {
                return Results.NotFound();
            }
            if (info.Kind == "CDP" && await cdp.CloseForwardAsync(id))
            {
                return Results.NoContent();
            }
            return rdpForwards.Close(id, sourceAddress) || tcpForwards.Close(id, sourceAddress) ||
                   udpForwards.Close(id, sourceAddress) ?
                Results.NoContent() : Results.NotFound();
        });
        app.MapPost("/api/remote/cdp/browsers", async (HttpContext context) =>
            IsAuthenticated(context, proxyUserHeader) ?
                Results.Ok(await cdp.DiscoverAsync()) :
                Results.Unauthorized());
        app.MapPost("/api/remote/cdp/sessions", (HttpContext context) =>
            IsAuthenticated(context, proxyUserHeader) ?
                Results.Ok(cdp.GetSessions()) :
                Results.Unauthorized());
        app.MapPost("/api/remote/cdp/start", async (
            HttpContext context,
            CdpStartRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader) ||
                context.Connection.RemoteIpAddress is not IPAddress sourceAddress)
            {
                return Results.Unauthorized();
            }
            if (request.Browser.Length is 0 or > 32 ||
                request.Mode is not ("fresh" or "incognito" or "profile") ||
                request.Profile?.Length > 64)
            {
                return Results.BadRequest();
            }
            try
            {
                return Results.Ok(await cdp.StartAsync(
                    sourceAddress,
                    request.Browser,
                    request.Mode,
                    request.Profile));
            }
            catch (ArgumentException exception)
            {
                return Results.BadRequest(new { exception.Message });
            }
        });
        app.MapPost("/api/remote/cdp/close", async (
            HttpContext context,
            CdpSessionRequest request) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader))
            {
                return Results.Unauthorized();
            }
            return await cdp.CloseAsync(request.Id) ? Results.NoContent() : Results.NotFound();
        });
    }

    private static bool IsAuthenticated(HttpContext context, string proxyUserHeader)
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

internal sealed record CdpStartRequest(string Browser, string Mode, string? Profile);
internal sealed record CdpSessionRequest(Guid Id);
internal sealed record RdpForwardRequest(ushort Port);
internal sealed record DesktopCaptureRequest(
    bool CaptureCursor,
    uint MaxDimension,
    ushort FrameRate,
    ushort ImageQuality,
    uint MonitorIndex);
internal sealed record PortForwardRequest(
    string? Kind,
    string? Protocol,
    string? SourceAddress,
    string? Host,
    ushort Port,
    uint IdleTimeoutSeconds);
