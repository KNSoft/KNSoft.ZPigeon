using KNSoft.ZPigeon.Server.Managed;
using KNSoft.ZPigeon.Web;

const uint StatusDeviceNotConnected = 0xC000009D;
var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls("http://127.0.0.1:5080");
var server = new NativeServer(AppContext.BaseDirectory);
var terminalSessions = new TerminalWebSessionManager(server);
builder.Services.AddSingleton(server);
builder.Services.AddSingleton(terminalSessions);
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
app.MapGet("/api/terminal/sessions", () => terminalSessions.GetSessions());
app.MapPost("/api/terminal/session", async (TerminalCreateRequest request) =>
{
    if (!Enum.IsDefined(request.Shell) ||
        request.Columns == 0 || request.Rows == 0)
    {
        return Results.BadRequest();
    }
    return Results.Ok(await terminalSessions.CreateAsync(
        request.Shell,
        request.Columns,
        request.Rows));
});
app.MapPost("/api/terminal/session/close", async (TerminalSessionRequest request) =>
    await terminalSessions.CloseAsync(request.Id) ?
        Results.NoContent() :
        Results.NotFound());
app.MapPost("/api/terminal/session/rename", (TerminalRenameRequest request) =>
{
    var title = request.Title.Trim();
    if (title.Length is 0 or > 128)
    {
        return Results.BadRequest();
    }
    return terminalSessions.Rename(request.Id, title) ?
               Results.NoContent() :
               Results.NotFound();
});
app.Map("/api/terminal", context =>
    TerminalWebSocket.RunAsync(context, terminalSessions));
app.MapRegistryApi(server);
app.MapManagementApi(server);
app.Lifetime.ApplicationStopping.Register(server.Dispose);
app.Lifetime.ApplicationStopping.Register(() =>
    terminalSessions.DisposeAsync().AsTask().GetAwaiter().GetResult());
server.Start();
app.Run();

internal sealed record TerminalCreateRequest(
    TerminalShell Shell,
    ushort Columns,
    ushort Rows);
internal sealed record TerminalSessionRequest(uint Id);
internal sealed record TerminalRenameRequest(uint Id, string Title);
internal sealed record EventLogRequest(string ChannelPath);
internal sealed record EventLogChannelRequest(string ChannelPath, bool Enabled);
internal sealed record EventLogQueryRequest(
    string ChannelPath,
    string? Query,
    string? Bookmark,
    uint MaxEvents);
