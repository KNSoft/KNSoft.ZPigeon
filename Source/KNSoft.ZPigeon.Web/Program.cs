using KNSoft.ZPigeon.Server.Managed;
using KNSoft.ZPigeon.Web;

const uint StatusDeviceNotConnected = 0xC000009D;
var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls("http://127.0.0.1:5080");
var server = new NativeServer(AppContext.BaseDirectory);
builder.Services.AddSingleton(server);
var app = builder.Build();

#if DEBUG
app.Use(async (context, next) =>
{
    context.Response.OnStarting(() =>
    {
        context.Response.Headers.CacheControl = "no-store, no-cache";
        context.Response.Headers.Pragma = "no-cache";
        context.Response.Headers.Expires = "0";
        return Task.CompletedTask;
    });
    await next();
});
#endif
app.Use(async (context, next) =>
{
    try
    {
        await next();
    }
    catch (NativeException exception) when (!context.Response.HasStarted)
    {
        context.Response.StatusCode =
            exception.Status.Type == ZpStatusType.NtStatus &&
            exception.Status.Code == StatusDeviceNotConnected ?
                StatusCodes.Status503ServiceUnavailable :
                StatusCodes.Status500InternalServerError;
        await context.Response.WriteAsJsonAsync(new
        {
            exception.Message,
            Type = (ushort)exception.Status.Type,
            exception.Status.Code
        });
    }
});
app.UseWebSockets();
app.UseDefaultFiles();
app.UseStaticFiles();
app.MapGet("/api/status", () => new
{
    server.State,
    server.ClientConnected
});
app.MapPost("/api/system", async () => await server.GetSystemInfoAsync());
app.MapPost("/api/process/terminate", async (ProcessRequest request) =>
    await server.TerminateProcessAsync(request.ProcessId));
app.MapPost("/api/eventlog/query", async (EventLogQueryRequest request) =>
    await server.QueryEventLogPageAsync(request.ChannelPath,
                                        request.Query,
                                        request.Bookmark,
                                        request.MaxEvents));
app.MapPost("/api/eventlog/channel", async (EventLogChannelRequest request) =>
    await server.SetEventLogChannelEnabledAsync(request.ChannelPath,
                                                request.Enabled));
app.MapPost("/api/eventlog/clear", async (EventLogRequest request) =>
    await server.ClearEventLogAsync(request.ChannelPath));
app.MapGet("/api/terminal/shells", async () =>
    await server.GetTerminalShellsAsync());
app.Map("/api/terminal", context =>
    TerminalWebSocket.RunAsync(context, server));
app.MapRegistryApi(server);
app.Lifetime.ApplicationStopping.Register(server.Dispose);
server.Start();
app.Run();

internal sealed record ProcessRequest(uint ProcessId);
internal sealed record EventLogRequest(string ChannelPath);
internal sealed record EventLogChannelRequest(string ChannelPath, bool Enabled);
internal sealed record EventLogQueryRequest(
    string ChannelPath,
    string? Query,
    string? Bookmark,
    uint MaxEvents);
