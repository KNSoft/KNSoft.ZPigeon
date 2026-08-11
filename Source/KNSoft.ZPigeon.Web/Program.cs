using KNSoft.ZPigeon.Server.Managed;
using KNSoft.ZPigeon.Web;

var builder = WebApplication.CreateBuilder(args);
builder.WebHost.UseUrls("http://127.0.0.1:5080");
var server = new NativeServer(AppContext.BaseDirectory);
builder.Services.AddSingleton(server);
var app = builder.Build();

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
