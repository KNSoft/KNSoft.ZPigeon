using KNSoft.ZPigeon.Agent;
using KNSoft.ZPigeon.Server.Managed;
using KNSoft.ZPigeon.Tools;
using System.Globalization;
using System.Text.Json;

namespace KNSoft.ZPigeon.Web;

internal static class AgentWebApi
{
    private static readonly JsonSerializerOptions ExportJsonOptions = new(JsonSerializerDefaults.Web)
    {
        WriteIndented = true
    };

    internal static void MapAgentApi(
        this WebApplication app,
        ZPigeonAgent agent,
        AgentStore store,
        ModelsDevCatalog catalog,
        ZPigeonToolCatalog tools,
        NativeServer server)
    {
        app.MapGet("/api/agent/catalog/providers", catalog.GetProviders);
        app.MapGet("/api/agent/catalog/providers/{provider}/models", catalog.GetModels);
        app.MapGet("/api/agent/tools", () => tools.CreateBuiltInTools(1)
            .Select(tool => new ToolView(tool.Function.Name,
                                         tool.Function.Description ?? string.Empty,
                                         tool.ReadOnly,
                                         tool.Destructive,
                                         tool.Sensitive))
            .OrderBy(tool => tool.Name, StringComparer.Ordinal)
            .ToArray());

        app.MapGet("/api/agent/models", () => store.GetModels().Select(ToModelView).ToArray());
        app.MapGet("/api/agent/models/{id:guid}", (Guid id) => ToModelDetail(store.GetModel(id, true)));
        app.MapPost("/api/agent/models", (ModelConfigurationRequest request) =>
        {
            var value = CreateModel(Guid.NewGuid(), request, null);
            return Results.Created($"/api/agent/models/{value.Id:D}", ToModelDetail(store.SaveModel(value, true)));
        });
        app.MapPut("/api/agent/models/{id:guid}", (Guid id, ModelConfigurationRequest request) =>
        {
            var existing = store.GetModel(id, true);
            var value = CreateModel(id, request, existing.Credential);
            return Results.Ok(ToModelDetail(store.SaveModel(value, false)));
        });
        app.MapDelete("/api/agent/models/{id:guid}", (Guid id) =>
        {
            store.DeleteModel(id);
            return Results.NoContent();
        });
        app.MapPost("/api/agent/models/test", async (ModelConfigurationRequest request, HttpContext context) =>
        {
            try
            {
                var existingCredential = request.Id.HasValue ?
                    store.GetModel(request.Id.Value, true).Credential :
                    null;
                var value = CreateModel(request.Id ?? Guid.NewGuid(), request, existingCredential);
                var reply = await agent.TestModelAsync(value, context.RequestAborted);
                return Results.Ok(new ModelTestResult(true, reply));
            }
            catch (OperationCanceledException) when (context.RequestAborted.IsCancellationRequested)
            {
                throw;
            }
            catch (Exception exception)
            {
                return Results.Ok(new ModelTestResult(false, exception.Message));
            }
        });

        app.MapGet("/api/agent/agents", () => store.GetAgents()
            .Select(value => new AgentConfigurationView(value.Id, value.Name, value.ModelId))
            .ToArray());
        app.MapGet("/api/agent/agents/{id:guid}", store.GetAgent);
        app.MapPost("/api/agent/agents", (AgentConfigurationRequest request) =>
        {
            var value = CreateAgent(Guid.NewGuid(), request, tools);
            return Results.Created($"/api/agent/agents/{value.Id:D}", store.SaveAgent(value, true));
        });
        app.MapPut("/api/agent/agents/{id:guid}", (Guid id, AgentConfigurationRequest request) =>
            Results.Ok(store.SaveAgent(CreateAgent(id, request, tools), false)));
        app.MapDelete("/api/agent/agents/{id:guid}", (Guid id) =>
        {
            store.DeleteAgent(id);
            return Results.NoContent();
        });

        app.MapGet("/api/agent/sessions", (HttpContext context, string? query) =>
            store.GetSessions(GetClientFingerprint(context, server), query));
        app.MapPost("/api/agent/sessions", (HttpContext context, SessionCreateRequest request) =>
        {
            var title = AgentValidation.ValidateTitle(request.Title);
            var value = store.CreateSession(request.AgentId, GetClientFingerprint(context, server), title);
            return Results.Created($"/api/agent/sessions/{value.Id:D}", CreateSessionView(value, agent, store));
        });
        app.MapGet("/api/agent/sessions/{id:guid}", (HttpContext context, Guid id) =>
        {
            var session = GetSession(context, server, store, id);
            return CreateSessionView(session, agent, store);
        });
        app.MapPut("/api/agent/sessions/{id:guid}", (HttpContext context, Guid id, SessionRenameRequest request) =>
        {
            _ = GetSession(context, server, store, id);
            return Results.Ok(CreateSessionView(store.RenameSession(id, AgentValidation.ValidateTitle(request.Title)),
                                                agent,
                                                store));
        });
        app.MapDelete("/api/agent/sessions/{id:guid}", (HttpContext context, Guid id) =>
        {
            _ = GetSession(context, server, store, id);
            if (agent.GetState(id).Running) return Results.Conflict(new { Message = "Stop the session first." });
            store.DeleteSession(id);
            return Results.NoContent();
        });
        app.MapPost("/api/agent/sessions/{id:guid}/messages", (
            HttpContext context,
            Guid id,
            SessionMessageRequest request) =>
        {
            _ = GetSession(context, server, store, id);
            if (!Enum.IsDefined(request.Disposition)) return Results.BadRequest();
            return Results.Accepted(value: agent.QueueMessage(id, request.Content, request.Disposition));
        });
        app.MapPost("/api/agent/sessions/{id:guid}/stop", (HttpContext context, Guid id) =>
        {
            _ = GetSession(context, server, store, id);
            agent.Stop(id);
            return Results.Accepted();
        });
        app.MapPost("/api/agent/sessions/{id:guid}/compact", (HttpContext context, Guid id) =>
        {
            _ = GetSession(context, server, store, id);
            agent.RequestCompaction(id);
            return Results.Accepted();
        });
        app.MapPost("/api/agent/sessions/{id:guid}/fork", (
            HttpContext context,
            Guid id,
            SessionForkRequest request) =>
        {
            _ = GetSession(context, server, store, id);
            var value = store.ForkSession(id, request.ThroughSequence);
            return Results.Created($"/api/agent/sessions/{value.Id:D}", CreateSessionView(value, agent, store));
        });
        app.MapGet("/api/agent/sessions/{id:guid}/export", (HttpContext context, Guid id) =>
        {
            var session = GetSession(context, server, store, id);
            var value = CreateSessionView(session, agent, store);
            return Results.File(JsonSerializer.SerializeToUtf8Bytes(value, ExportJsonOptions),
                                "application/json",
                                $"zpigeon-session-{id:D}.json");
        });
        app.MapGet("/api/agent/sessions/{id:guid}/events", async (HttpContext context, Guid id) =>
        {
            _ = GetSession(context, server, store, id);
            await StreamSessionChangesAsync(agent, id, context);
        });
    }

    private static ModelConfiguration CreateModel(
        Guid id,
        ModelConfigurationRequest request,
        string? existingCredential)
    {
        ArgumentNullException.ThrowIfNull(request);
        if (!Uri.TryCreate(request.BaseUrl?.Trim(), UriKind.Absolute, out var baseUrl))
        {
            throw new ArgumentException("The model Base URL is invalid.", nameof(request));
        }
        var authentication = request.Authentication;
        var credential = authentication == ModelAuthentication.None ? string.Empty :
            string.IsNullOrEmpty(request.Credential) ? existingCredential ?? string.Empty : request.Credential;
        var result = new ModelConfiguration(id,
                                            request.Name?.Trim() ?? string.Empty,
                                            request.Provider?.Trim() ?? string.Empty,
                                            request.Protocol,
                                            baseUrl,
                                            authentication,
                                            credential,
                                            request.ModelId?.Trim() ?? string.Empty,
                                            request.ContextWindow,
                                            request.MaximumOutputTokens,
                                            request.Reasoning,
                                            request.RequestTimeoutSeconds,
                                            string.IsNullOrWhiteSpace(request.AdvancedJson) ?
                                                "{}" :
                                                request.AdvancedJson);
        AgentValidation.ValidateModel(result);
        return result;
    }

    private static AgentConfiguration CreateAgent(
        Guid id,
        AgentConfigurationRequest request,
        ZPigeonToolCatalog tools)
    {
        ArgumentNullException.ThrowIfNull(request);
        var toolNames = request.ToolNames ?? [];
        var knownTools = tools.CreateBuiltInTools(1)
                              .Select(tool => tool.Function.Name)
                              .ToHashSet(StringComparer.Ordinal);
        var result = new AgentConfiguration(id,
                                            request.Name?.Trim() ?? string.Empty,
                                            request.ModelId,
                                            request.SystemPrompt ?? string.Empty,
                                            toolNames,
                                            request.AgentsMd ?? string.Empty,
                                            request.ToolsMd ?? string.Empty,
                                            request.MemoryMd ?? string.Empty,
                                            request.Documents ?? []);
        AgentValidation.ValidateAgent(result, knownTools);
        return result;
    }

    private static object CreateSessionView(AgentSession session, ZPigeonAgent agent, AgentStore store)
    {
        var configuration = store.GetAgent(session.AgentId);
        var model = store.GetModel(configuration.ModelId, false);
        return new
        {
            Session = session,
            Agent = new { configuration.Id, configuration.Name },
            Model = new { model.Id, model.Name, model.Provider, model.ModelId, model.ContextWindow },
            Items = store.GetItems(session.Id).Select(ToItemView).ToArray(),
            State = agent.GetState(session.Id),
            Usage = store.GetUsage(session.Id, model.ContextWindow)
        };
    }

    private static object ToItemView(SessionItem item) => new
    {
        item.SessionId,
        item.Sequence,
        item.RunId,
        item.Step,
        item.Kind,
        item.State,
        item.Name,
        item.CallId,
        item.Content,
        item.RelatedSequence,
        item.Protocol,
        item.InputTokens,
        item.CachedInputTokens,
        item.OutputTokens,
        item.ReasoningTokens,
        item.TotalTokens,
        item.RawUsage,
        item.CreatedAt
    };

    private static ModelConfigurationView ToModelView(ModelConfiguration value) =>
        new(value.Id,
            value.Name,
            value.Provider,
            value.Protocol,
            value.BaseUrl.AbsoluteUri,
            value.Authentication,
            value.ModelId,
            value.ContextWindow,
            value.MaximumOutputTokens,
            value.Reasoning,
            value.RequestTimeoutSeconds,
            value.AdvancedJson);

    private static ModelConfigurationDetail ToModelDetail(ModelConfiguration value) =>
        new(value.Id,
            value.Name,
            value.Provider,
            value.Protocol,
            value.BaseUrl.AbsoluteUri,
            value.Authentication,
            value.Credential,
            value.ModelId,
            value.ContextWindow,
            value.MaximumOutputTokens,
            value.Reasoning,
            value.RequestTimeoutSeconds,
            value.AdvancedJson);

    private static AgentSession GetSession(
        HttpContext context,
        NativeServer server,
        AgentStore store,
        Guid id)
    {
        var session = store.GetSession(id);
        if (!StringComparer.Ordinal.Equals(session.ClientFingerprint, GetClientFingerprint(context, server)))
        {
            throw new KeyNotFoundException("The session does not exist for this client.");
        }
        return session;
    }

    private static string GetClientFingerprint(HttpContext context, NativeServer server)
    {
        if (!context.Request.Query.TryGetValue("client", out var value) ||
            value.Count != 1 ||
            !ulong.TryParse(value[0], NumberStyles.None, CultureInfo.InvariantCulture, out var clientId) ||
            clientId == 0)
        {
            throw new ArgumentException("The client identifier is invalid.");
        }
        return server.GetClients().SingleOrDefault(client => client.Id == clientId)?.Fingerprint ??
               throw new KeyNotFoundException("The client is not connected.");
    }

    private static async Task StreamSessionChangesAsync(
        ZPigeonAgent agent,
        Guid sessionId,
        HttpContext context)
    {
        using var changed = new SemaphoreSlim(0, 1);
        var sync = new object();
        var pending = false;
        var finished = false;
        void OnChanged(object? _, SessionChangedEventArgs eventArgs)
        {
            if (eventArgs.SessionId != sessionId) return;
            lock (sync)
            {
                if (finished || pending) return;
                pending = true;
                changed.Release();
            }
        }
        agent.SessionChanged += OnChanged;
        context.Response.ContentType = "text/event-stream";
        context.Response.Headers.CacheControl = "no-store";
        try
        {
            await context.Response.WriteAsync("event: changed\ndata: changed\n\n", context.RequestAborted);
            await context.Response.Body.FlushAsync(context.RequestAborted);
            while (!context.RequestAborted.IsCancellationRequested)
            {
                var hasChanged = await changed.WaitAsync(TimeSpan.FromSeconds(15), context.RequestAborted);
                if (hasChanged)
                {
                    lock (sync) pending = false;
                }
                await context.Response.WriteAsync(hasChanged ?
                                                        "event: changed\ndata: changed\n\n" :
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
            agent.SessionChanged -= OnChanged;
            lock (sync) finished = true;
        }
    }
}

internal sealed record ModelConfigurationRequest(
    Guid? Id,
    string Name,
    string Provider,
    ModelProtocol Protocol,
    string BaseUrl,
    ModelAuthentication Authentication,
    string? Credential,
    string ModelId,
    int ContextWindow,
    int MaximumOutputTokens,
    ReasoningEffort Reasoning,
    int RequestTimeoutSeconds,
    string AdvancedJson);

internal sealed record ModelConfigurationView(
    Guid Id,
    string Name,
    string Provider,
    ModelProtocol Protocol,
    string BaseUrl,
    ModelAuthentication Authentication,
    string ModelId,
    int ContextWindow,
    int MaximumOutputTokens,
    ReasoningEffort Reasoning,
    int RequestTimeoutSeconds,
    string AdvancedJson);

internal sealed record ModelConfigurationDetail(
    Guid Id,
    string Name,
    string Provider,
    ModelProtocol Protocol,
    string BaseUrl,
    ModelAuthentication Authentication,
    string Credential,
    string ModelId,
    int ContextWindow,
    int MaximumOutputTokens,
    ReasoningEffort Reasoning,
    int RequestTimeoutSeconds,
    string AdvancedJson);

internal sealed record AgentConfigurationRequest(
    string Name,
    Guid ModelId,
    string? SystemPrompt,
    string[]? ToolNames,
    string? AgentsMd,
    string? ToolsMd,
    string? MemoryMd,
    AgentDocument[]? Documents);

internal sealed record AgentConfigurationView(Guid Id, string Name, Guid ModelId);
internal sealed record SessionCreateRequest(Guid AgentId, string Title);
internal sealed record SessionRenameRequest(string Title);
internal sealed record SessionMessageRequest(string Content, MessageDisposition Disposition);
internal sealed record SessionForkRequest(long? ThroughSequence);
internal sealed record ModelTestResult(bool Success, string Message);
internal sealed record ToolView(string Name, string Description, bool ReadOnly, bool Destructive, bool Sensitive);
