using KNSoft.ZPigeon.Agent;
using KNSoft.ZPigeon.Application;
using KNSoft.ZPigeon.Server.Managed;
using KNSoft.ZPigeon.Tools;
using KNSoft.ZPigeon.Web;
using Microsoft.AspNetCore.DataProtection;
using Microsoft.AspNetCore.HttpOverrides;
using ModelContextProtocol.AspNetCore;
using ModelContextProtocol.Protocol;
using System.Globalization;
using System.Net;
using System.Text.Json;

const uint StatusDeviceNotConnected = 0xC000009D;
var builder = WebApplication.CreateBuilder(args);
var webPort = builder.Configuration.GetValue("Web:Port", 9983);
var localOrigin = new UriBuilder(Uri.UriSchemeHttp, "127.0.0.1", webPort).Uri.GetLeftPart(UriPartial.Authority);
builder.WebHost.UseUrls(localOrigin);
builder.Logging.AddFilter("Microsoft.AspNetCore", LogLevel.Warning);
builder.Logging.AddFilter("ModelContextProtocol", LogLevel.Warning);
var server = new NativeServer(AppContext.BaseDirectory);
var zpigeonApplication = new ZPigeonApplication(server);
var toolCatalog = new ZPigeonToolCatalog(zpigeonApplication);
var clientServices = new ClientServicesRegistry(server);
var clientConnectionAvailable = new object();
var proxyUserHeader = builder.Configuration["ReverseProxy:UserHeader"] ?? "X-Forwarded-User";
var iceServers = builder.Configuration.GetSection("P2p:IceServers").Get<string[]>() ?? [];
var stun = new StunServer(builder.Configuration.GetValue("P2p:StunPort", (ushort)3478));
var applicationData = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                   "KNSoft",
                                   "ZPigeon");
builder.Services.AddDataProtection()
                .SetApplicationName("KNSoft.ZPigeon")
                .PersistKeysToFileSystem(new DirectoryInfo(Path.Combine(applicationData, "DataProtection")))
                .ProtectKeysWithDpapi();
builder.Services.AddMcpServer()
                .WithHttpTransport(options => options.SessionMode = HttpServerSessionMode.Stateless)
                .WithTools(McpToolFactory.Create(toolCatalog.CreateExternalTools()))
                .WithListToolsHandler((_, _) => ValueTask.FromResult(new ListToolsResult
                {
                    Tools = [],
                    TimeToLive = TimeSpan.FromHours(1),
                    CacheScope = CacheScope.Private
                }));
builder.Services.Configure<ForwardedHeadersOptions>(options =>
{
    options.ForwardedHeaders = ForwardedHeaders.XForwardedFor | ForwardedHeaders.XForwardedProto;
    options.ForwardLimit = 1;
    options.KnownProxies.Clear();
    options.KnownIPNetworks.Clear();
    options.KnownProxies.Add(IPAddress.Loopback);
    options.KnownProxies.Add(IPAddress.IPv6Loopback);
});
var app = builder.Build();
var secretProtector = new DataProtectionSecretProtector(
    app.Services.GetRequiredService<IDataProtectionProvider>());
var agentStore = new AgentStore(Path.Combine(applicationData, "agent.db"), secretProtector);
var agent = new ZPigeonAgent(zpigeonApplication, toolCatalog, agentStore);
var modelsDevCatalog = new ModelsDevCatalog(Path.Combine(AppContext.BaseDirectory,
                                                         "3rdParty",
                                                         "models.dev",
                                                         "api.json"));

app.UseForwardedHeaders();

// This boundary blocks browser cross-origin access; native loopback callers remain intentionally trusted.
app.Use(async (context, next) =>
{
    if (!StringComparer.Ordinal.Equals(context.Request.Host.Host, "127.0.0.1") ||
        (context.Request.Host.Port ?? 80) != webPort ||
        context.Request.Headers.Origin is { Count: > 0 } origin &&
        !StringComparer.Ordinal.Equals(origin.ToString(), localOrigin) ||
        StringComparer.OrdinalIgnoreCase.Equals(
            context.Request.Headers["Sec-Fetch-Site"].ToString(),
            "cross-site"))
    {
        context.Response.StatusCode = StatusCodes.Status403Forbidden;
        return;
    }
    await next();
});

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
#else
// browser data (cookies, passwords, history) must never be cached anywhere
app.Use(async (context, next) =>
{
    if (context.Request.Path.StartsWithSegments("/api/browser") ||
        context.Request.Path.StartsWithSegments("/api/agent") ||
        context.Request.Path.StartsWithSegments("/mcp"))
    {
        context.Response.OnStarting(() =>
        {
            context.Response.Headers.CacheControl = "no-store";
            return Task.CompletedTask;
        });
    }
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
        context.Response.StatusCode = IsDeviceNotConnected(exception) &&
                                      context.Items[clientConnectionAvailable] is false ?
                StatusCodes.Status503ServiceUnavailable :
                StatusCodes.Status500InternalServerError;
        await context.Response.WriteAsJsonAsync(new
        {
            exception.Message,
            Type = (ushort)exception.Status.Type,
            exception.Status.Code
        });
    }
    catch (ArgumentException exception) when (!context.Response.HasStarted)
    {
        context.Response.StatusCode = StatusCodes.Status400BadRequest;
        await context.Response.WriteAsJsonAsync(new { exception.Message });
    }
    catch (KeyNotFoundException exception) when (!context.Response.HasStarted)
    {
        context.Response.StatusCode = StatusCodes.Status404NotFound;
        await context.Response.WriteAsJsonAsync(new { exception.Message });
    }
    catch (InvalidOperationException exception) when (!context.Response.HasStarted)
    {
        context.Response.StatusCode = StatusCodes.Status409Conflict;
        await context.Response.WriteAsJsonAsync(new { exception.Message });
    }
});
var webSocketOptions = new WebSocketOptions();
webSocketOptions.AllowedOrigins.Add(localOrigin);
app.UseWebSockets(webSocketOptions);
app.Use(async (context, next) =>
{
    using (server.SelectCancellation(context.RequestAborted))
    {
        await next();
    }
});
app.Use(async (context, next) =>
{
    if (!context.Request.Path.StartsWithSegments("/api") ||
        context.Request.Path.StartsWithSegments("/api/clients") ||
        context.Request.Path.StartsWithSegments("/api/agent"))
    {
        await next();
        return;
    }
    if (!context.Request.Query.TryGetValue("client", out var value) ||
        value.Count != 1 ||
        !ulong.TryParse(value[0], out var clientId) ||
        clientId == 0)
    {
        context.Response.StatusCode = StatusCodes.Status400BadRequest;
        return;
    }
    try
    {
        using (server.SelectClient(clientId))
        {
            await next();
        }
    }
    catch (NativeException exception) when (IsDeviceNotConnected(exception))
    {
        var connected = NativeServer.IsClientConnected(clientId);
        context.Items[clientConnectionAvailable] = connected;
        if (!connected) clientServices.Remove(clientId);
        throw;
    }
    catch
    {
        if (!NativeServer.IsClientConnected(clientId))
        {
            context.Items[clientConnectionAvailable] = false;
            clientServices.Remove(clientId);
        }
        throw;
    }
});
app.UseDefaultFiles();
app.UseStaticFiles();
app.MapMcp("/mcp");
app.MapGet("/api/clients", () => GetConnectedClientViews(server));
app.MapGet("/api/clients/events", (HttpContext context) =>
    StreamClientChangesAsync(server, context));
app.MapGet("/api/status", () => GetStatus(server, clientServices.Current));
app.MapGet("/api/status/events", (HttpContext context) =>
    StreamStatusAsync(server, clientServices.Current, context));
app.MapGet("/api/zpigeon/connection", () => clientServices.Current.ConnectionPerformance.Current);
app.MapPost("/api/zpigeon/connection/probe", () =>
    clientServices.Current.ConnectionPerformance.ProbeAsync());
app.MapPost("/api/zpigeon/connection", (ConnectionPerformanceRequest request) =>
    request.SpeedClass < 5 && request.LatencyClass < 5 ?
        Results.Ok(clientServices.Current.ConnectionPerformance.Configure(request)) :
        Results.BadRequest());
app.MapPost("/api/system", (CancellationToken cancellationToken) =>
    zpigeonApplication.GetSystemInfoAsync(server.ClientId, cancellationToken));
app.MapPost("/api/eventlog/query", (EventLogQueryRequest request, HttpContext context) =>
    zpigeonApplication.QueryEventLogAsync(server.ClientId,
                                         request.ChannelPath,
                                         request.Query,
                                         request.Bookmark,
                                         request.MaxEvents,
                                         context.RequestAborted));
app.MapPost("/api/eventlog/channels", (CancellationToken cancellationToken) =>
    zpigeonApplication.GetEventLogChannelsAsync(server.ClientId, cancellationToken));
app.MapPost("/api/eventlog/channel/info", (EventLogRequest request) =>
    server.QueryEventLogChannelInfoAsync(request.ChannelPath));
app.MapPost("/api/eventlog/channel", (EventLogChannelRequest request) =>
    server.SetEventLogChannelEnabledAsync(request.ChannelPath,
                                          request.Enabled));
app.MapPost("/api/eventlog/clear", (EventLogRequest request) =>
    server.ClearEventLogAsync(request.ChannelPath));
app.MapPost("/api/eventlog/channel/configure", (EventLogChannelConfigureRequest request) =>
    server.ConfigureEventLogChannelAsync(request.ChannelPath,
                                          request.Enabled,
                                          request.RetentionMode,
                                          ulong.Parse(request.MaximumSize, CultureInfo.InvariantCulture)));
app.MapGet("/api/eventlog/stream/{id:guid}", async (
    HttpContext context,
    Guid id,
    string channelPath,
    string? query,
    string? name) =>
{
    if (channelPath.Length is 0 or > 512 || query?.Length > 8192 || name?.Length > 128)
    {
        context.Response.StatusCode = StatusCodes.Status400BadRequest;
        return;
    }
    await clientServices.Current.EventLogStreams.StreamAsync(context, id, channelPath, query, name);
});
app.MapPost("/api/eventlog/stream/{id:guid}/stop", (Guid id) =>
    clientServices.Current.EventLogStreams.Stop(id) ? Results.NoContent() : Results.NotFound());
app.MapGet("/api/terminal/shells", () => server.GetTerminalShellsAsync());
app.MapGet("/api/terminal/sessions", () => clientServices.Current.TerminalSessions.GetSessions());
app.MapPost("/api/terminal/session", async (TerminalCreateRequest request) =>
{
    if (request.Shell is not (TerminalShell.CommandPrompt or
                              TerminalShell.WindowsPowerShell or
                              TerminalShell.PowerShell) ||
        request.Columns == 0 || request.Rows == 0 ||
        request.WorkingDirectory is { Length: > 32767 } ||
        request.WorkingDirectory?.Contains('\0') == true)
    {
        return Results.BadRequest();
    }
    return Results.Ok(await clientServices.Current.TerminalSessions.CreateAsync(
        request.Shell,
        request.Columns,
        request.Rows,
        request.WorkingDirectory));
});
app.MapPost("/api/terminal/session/close", async (TerminalSessionRequest request) =>
    await clientServices.Current.TerminalSessions.CloseAsync(request.Id) ?
        Results.NoContent() :
        Results.NotFound());
app.MapPost("/api/terminal/session/rename", (TerminalRenameRequest request) =>
{
    var title = request.Title.Trim();
    if (title.Length is 0 or > 128)
    {
        return Results.BadRequest();
    }
    return clientServices.Current.TerminalSessions.Rename(request.Id, title) ?
               Results.NoContent() :
               Results.NotFound();
});
app.Map("/api/terminal", context =>
    TerminalWebSocket.RunAsync(context, clientServices.Current.TerminalSessions));
app.Map("/api/rtc", context => RtcWebSocket.RunAsync(context,
                                                      server,
                                                      stun,
                                                      iceServers,
                                                      proxyUserHeader));
app.MapPost("/api/serial/ports", () => server.EnumerateSerialPortsAsync());
app.Map("/api/serial", context => SerialWebSocket.RunAsync(context, server));
app.MapAgentApi(agent, agentStore, modelsDevCatalog, toolCatalog, server);
app.MapRegistryApi(clientServices);
app.MapManagementApi(clientServices);
app.MapSoftwareApi(clientServices);
app.MapRecordingApi(clientServices);
app.MapExecutionApi(clientServices);
app.MapRemoteAccessApi(clientServices,
                       proxyUserHeader);
app.Lifetime.ApplicationStopping.Register(server.Dispose);
app.Lifetime.ApplicationStopping.Register(clientServices.Dispose);
app.Lifetime.ApplicationStopping.Register(stun.Dispose);
app.Lifetime.ApplicationStopping.Register(agent.Dispose);
server.Start();
stun.Start();
app.Run();

static bool IsDeviceNotConnected(NativeException exception) =>
    exception.Status.Type == ZpStatusType.NtStatus &&
    exception.Status.Code == StatusDeviceNotConnected;

static ConnectedClientView[] GetConnectedClientViews(NativeServer server) =>
    server.GetClients()
          .Select(client => new ConnectedClientView(client.Id,
                                                     client.Fingerprint,
                                                     client.Address.ToString(),
                                                     client.Statistics.Transport))
          .ToArray();

static ServerStatusView GetStatus(NativeServer server, ClientServices services)
{
    services.ConnectionPerformance.ProbeIfDue();
    return new(NativeServer.State, server.ClientConnected, services.NetworkQuality.Current);
}

static async Task StreamStatusAsync(
    NativeServer server,
    ClientServices services,
    HttpContext context)
{
    context.Response.ContentType = "text/event-stream";
    context.Response.Headers.CacheControl = "no-store";
    try
    {
        while (!context.RequestAborted.IsCancellationRequested)
        {
            await context.Response.WriteAsync("data: ", context.RequestAborted);
            await JsonSerializer.SerializeAsync(context.Response.Body,
                                                GetStatus(server, services),
                                                JsonSerializerOptions.Web,
                                                context.RequestAborted);
            await context.Response.WriteAsync("\n\n", context.RequestAborted);
            await context.Response.Body.FlushAsync(context.RequestAborted);
            await Task.Delay(TimeSpan.FromSeconds(1), context.RequestAborted);
        }
    }
    catch (OperationCanceledException) when (context.RequestAborted.IsCancellationRequested)
    {
    }
}

static async Task StreamClientChangesAsync(NativeServer server, HttpContext context)
{
    using var changed = new SemaphoreSlim(0, 1);
    var gate = new object();
    var pending = false;
    var finished = false;
    void OnClientsChanged(object? _, EventArgs __)
    {
        lock (gate)
        {
            if (finished || pending) return;
            pending = true;
            changed.Release();
        }
    }

    server.ClientsChanged += OnClientsChanged;
    context.Response.ContentType = "text/event-stream";
    context.Response.Headers.CacheControl = "no-store";
    try
    {
        await context.Response.WriteAsync("event: clients\ndata: changed\n\n",
                                            context.RequestAborted);
        await context.Response.Body.FlushAsync(context.RequestAborted);
        while (!context.RequestAborted.IsCancellationRequested)
        {
            var hasChanged = await changed.WaitAsync(TimeSpan.FromSeconds(15),
                                                     context.RequestAborted);
            if (hasChanged)
            {
                lock (gate) pending = false;
            }
            await context.Response.WriteAsync(hasChanged ?
                                                    "event: clients\ndata: changed\n\n" :
                                                    ": keepalive\n\n",
                                                context.RequestAborted);
            await context.Response.Body.FlushAsync(context.RequestAborted);
        }
    }
    catch (OperationCanceledException) when (context.RequestAborted.IsCancellationRequested)
    {
    }
    finally
    {
        server.ClientsChanged -= OnClientsChanged;
        lock (gate) finished = true;
    }
}

internal sealed record TerminalCreateRequest(
    TerminalShell Shell,
    ushort Columns,
    ushort Rows,
    string? WorkingDirectory);
internal sealed record TerminalSessionRequest(uint Id);
internal sealed record TerminalRenameRequest(uint Id, string Title);
internal sealed record EventLogRequest(string ChannelPath);
internal sealed record EventLogChannelRequest(string ChannelPath, bool Enabled);
internal sealed record EventLogChannelConfigureRequest(
    string ChannelPath,
    bool Enabled,
    EventLogRetentionMode RetentionMode,
    string MaximumSize);
internal sealed record EventLogQueryRequest(
    string ChannelPath,
    string? Query,
    string? Bookmark,
    uint MaxEvents);
internal sealed record ConnectedClientView(
    ulong Id,
    string Fingerprint,
    string Address,
    int Transport);
internal sealed record ServerStatusView(
    int State,
    bool ClientConnected,
    NetworkQuality Quality);
