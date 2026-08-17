using System.Net;

namespace KNSoft.ZPigeon.Web;

internal static class RemoteAccessWebApi
{
    internal static void MapRemoteAccessApi(
        this WebApplication app,
        TcpForwardManager forwards,
        CdpSessionManager cdp,
        string proxyUserHeader)
    {
        app.MapPost("/api/remote/rdp", (HttpContext context) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader) ||
                context.Connection.RemoteIpAddress is not IPAddress sourceAddress)
            {
                return Results.Unauthorized();
            }
            return Results.Ok(forwards.Create(sourceAddress, 3389));
        });
        app.MapPost("/api/remote/forward/{id:guid}", (
            HttpContext context,
            Guid id) =>
        {
            if (!IsAuthenticated(context, proxyUserHeader))
            {
                return Results.Unauthorized();
            }
            var info = forwards.Get(id);
            return info is null ? Results.NotFound() : Results.Ok(info);
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
        if (context.User.Identity?.IsAuthenticated == true)
        {
            return true;
        }
        var values = context.Request.Headers[proxyUserHeader];
        return values.Count == 1 && values[0]?.Trim().Length is > 0 and <= 128;
    }
}

internal sealed record CdpStartRequest(string Browser, string Mode, string? Profile);
internal sealed record CdpSessionRequest(Guid Id);
