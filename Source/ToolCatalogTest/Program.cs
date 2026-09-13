using KNSoft.ZPigeon.Application;
using KNSoft.ZPigeon.Agent;
using KNSoft.ZPigeon.Server.Managed;
using KNSoft.ZPigeon.Tools;

var server = new NativeServer(AppContext.BaseDirectory);
var catalog = new ZPigeonToolCatalog(new ZPigeonApplication(server));
var external = catalog.CreateExternalTools();
var builtIn = catalog.CreateBuiltInTools(1);
var externalNames = external.Select(tool => tool.Function.Name).ToHashSet(StringComparer.Ordinal);
var builtInNames = builtIn.Select(tool => tool.Function.Name).ToHashSet(StringComparer.Ordinal);

Assert(externalNames.Count == external.Count, "External tool names must be unique.");
Assert(builtInNames.Count == builtIn.Count, "Built-in tool names must be unique.");
Assert(externalNames.SetEquals(builtInNames.Append("list_clients")),
       "The two audiences must share one catalog except for list_clients.");
Assert(external.Select(tool => tool.Function.Name)
               .SequenceEqual(externalNames.Order(StringComparer.Ordinal)),
       "The external catalog must be deterministic.");
foreach (var tool in external)
{
    var hasClientId = tool.Function.JsonSchema.GetProperty("properties").TryGetProperty("clientId", out _);
    var targetless = tool.Function.Name is "list_clients" or "list_administration_operations";
    Assert(hasClientId == !targetless,
           $"External tool {tool.Function.Name} has an invalid clientId schema.");
    Assert(tool.ReadOnly || tool.Destructive,
           $"Mutating tool {tool.Function.Name} must conservatively declare destructive behavior.");
    Assert(!tool.ReadOnly || tool.Idempotent,
           $"Read-only tool {tool.Function.Name} must be idempotent.");
}
foreach (var tool in builtIn)
{
    Assert(!tool.Function.JsonSchema.GetProperty("properties").TryGetProperty("clientId", out _),
           $"Built-in tool {tool.Function.Name} exposes clientId.");
}
Assert(external.Where(tool => tool.Sensitive)
               .Select(tool => tool.Function.Name)
               .SequenceEqual(["query_browser_secrets"]),
       "Sensitive tools must be explicit.");

var administration = ZPigeonApplication.GetAdministrationCapabilities();
Assert(!administration.Queries.Contains(nameof(AdministrationOperation.QueryCredential)),
       "Credential reads must not be exposed through the generic administration tool.");
Assert(!administration.Controls.Any(control =>
           control.Operation == nameof(AdministrationOperation.ControlSoftware)),
       "Binary or structured-data controls must not be exposed through the string control tool.");
Assert(!administration.Controls.Any(control =>
           control.Operation == nameof(AdministrationOperation.ControlPublishedShareSecurity)),
       "Controls requiring flags or binary data must not be exposed through the string control tool.");
Assert(administration.Controls.All(control => control.Actions.Length > 0),
       "Every administration control must declare its valid actions.");
var updates = administration.Controls.Single(control =>
    control.Operation == nameof(AdministrationOperation.ControlUpdate));
Assert(updates.Actions.SequenceEqual([nameof(AdministrationAction.Refresh), nameof(AdministrationAction.Check)]),
       "Administration action combinations must be explicit and deterministic.");
TestAgentStore();

Console.WriteLine($"Tool catalog: {external.Count} external, {builtIn.Count} built-in.");

static void Assert(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static void TestAgentStore()
{
    var directory = Path.Combine(Path.GetTempPath(), $"KNSoft.ZPigeon.AgentTest-{Guid.NewGuid():N}");
    try
    {
        var profile = new AgentProfile("Answer concisely.",
                                         [],
                                         "Follow the session instructions.",
                                         string.Empty,
                                         "Remember the selected task.",
                                         [new("NOTES.md", "Session context")]);
        AgentValidation.ValidateProfile(profile, new HashSet<string>());
        var store = new AgentStore(Path.Combine(directory, "agent.db"), new TestProtector(), profile);
        Assert(store.GetProfile().Documents.Single() == profile.Documents.Single(),
               "The global profile must exist before any sessions are created.");
        var model = new ModelConfiguration(Guid.NewGuid(),
                                           "Test model",
                                           "openai",
                                           ModelProtocol.OpenAIResponses,
                                           new("https://api.openai.com/v1"),
                                           ModelAuthentication.ApiKey,
                                           "secret",
                                           "test-model",
                                           128000,
                                           4096,
                                           ReasoningEffort.None,
                                           60,
                                           "{}");
        store.SaveModel(model, true);
        Assert(store.GetModels().Single().Credential.Length == 0,
               "Model lists must not expose credentials.");
        Assert(store.GetModel(model.Id, true).Credential == model.Credential,
               "Stored credentials must round-trip through the protector.");
        Assert(store.GetModel(model.Id, false).Credential.Length == 0,
               "Model metadata reads must not decrypt credentials.");

        var session = store.CreateSession(model.Id, "test-fingerprint", "New session");
        var stored = store.GetSession(session.Id);
        Assert(stored.ModelId == model.Id,
               "Sessions must persist their model without an agent configuration.");
        Assert(store.GetSessions("test-fingerprint", null).Single().ModelName == model.Name,
               "Session lists must display the selected model.");
        var secondModel = model with { Id = Guid.NewGuid(), Name = "Second model" };
        store.SaveModel(secondModel, true);
        var secondSession = store.CreateSession(secondModel.Id, "other-client", "Other session");
        Assert(store.GetModels().Length == 2 &&
               store.GetSessions("test-fingerprint", null).Single().Id == session.Id &&
               store.GetSession(secondSession.Id).ModelId == secondModel.Id,
               "Multiple models must remain selectable and sessions isolated by client.");
        try
        {
            store.DeleteModel(model.Id);
            throw new InvalidOperationException("A model used by a session must not be deleted.");
        }
        catch (InvalidOperationException exception) when (exception.InnerException is Microsoft.Data.Sqlite.SqliteException)
        {
        }
        store.DeleteSession(secondSession.Id);
        store.DeleteModel(secondModel.Id);
        var queued = store.AddUserMessage(session.Id, "queued message", MessageDisposition.Queue);
        store.SetInitialTitle(session.Id, queued.Content);
        var steer = store.AddUserMessage(session.Id, "steer message", MessageDisposition.Steer);
        Assert(store.GetQueuedCount(session.Id) == 2,
               "Queued message counts must be read without loading session history.");
        Assert(store.GetNextQueued(session.Id)?.Sequence == steer.Sequence,
               "Steer messages must run before the normal queue.");
        Assert(store.GetSessions("test-fingerprint", "queued").Single().Id == session.Id,
               "Session search must include message content.");

        store.SetItemState(session.Id, queued.Sequence, SessionItemState.Running);
        store.AddItem(session.Id,
                      queued.RunId,
                      1,
                      SessionItemKind.Assistant,
                      SessionItemState.Completed,
                      null,
                      null,
                      "reply",
                      null,
                      model.Protocol,
                      "[]",
                      new(10, 2, 3, 1, 13, "{\"total_tokens\":13}"));
        var usage = store.GetUsage(session.Id, model.ContextWindow);
        Assert(usage.Input == 10 && usage.CachedInput == 2 && usage.Output == 3 &&
               usage.Reasoning == 1 && usage.Total == 13,
               "Normalized token usage must be persisted.");

        var fork = store.ForkSession(session.Id, null);
        Assert(fork.ModelId == model.Id, "Forks must preserve the selected model.");
        store.SaveProfile(profile with { SystemPrompt = "Updated instructions" });
        var reopened = new AgentStore(Path.Combine(directory, "agent.db"), new TestProtector(), profile);
        Assert(reopened.GetProfile().SystemPrompt == "Updated instructions" &&
               reopened.GetProfile().Documents.Single() == profile.Documents.Single(),
               "Restarting must preserve the global profile instead of restoring its defaults.");
        reopened.SaveProfile(profile with { AgentsMd = "Shared updated instructions" });
        var laterSession = store.CreateSession(model.Id, "other-client", "Later session");
        var laterFork = store.ForkSession(session.Id, null);
        Assert(store.GetProfile().AgentsMd == "Shared updated instructions",
               "All store instances must read the same profile; new sessions and forks must not replace it.");
        using (var connection = new Microsoft.Data.Sqlite.SqliteConnection($"Data Source={directory}/agent.db"))
        {
            connection.Open();
            using var command = connection.CreateCommand();
            command.CommandText = "SELECT COUNT(*) FROM Profile";
            Assert((long)command.ExecuteScalar()! == 1, "There must be exactly one stored profile.");
            command.CommandText = "SELECT COUNT(*) FROM pragma_table_info('Session') WHERE name = 'Profile'";
            Assert((long)command.ExecuteScalar()! == 0, "Sessions must not store profile copies.");
        }
        store.DeleteSession(laterSession.Id);
        store.DeleteSession(laterFork.Id);
        Assert(store.GetItems(fork.Id).Where(item => item.Kind == SessionItemKind.User)
                    .All(item => item.State == SessionItemState.Canceled),
               "Forks must not resume queued or running work.");
        store.DeleteSession(fork.Id);
        store.DeleteSession(session.Id);
        store.DeleteModel(model.Id);
        Assert(store.GetProfile().AgentsMd == "Shared updated instructions",
               "Deleting every session and model must preserve the global profile.");
    }
    finally
    {
        Microsoft.Data.Sqlite.SqliteConnection.ClearAllPools();
        if (Directory.Exists(directory)) Directory.Delete(directory, true);
    }
}

sealed class TestProtector : ISecretProtector
{
    public string Protect(string value) => "protected:" + value;

    public string Unprotect(string value) => value[10..];
}
