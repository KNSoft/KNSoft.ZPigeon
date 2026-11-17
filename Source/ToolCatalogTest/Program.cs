using KNSoft.ZPigeon.Application;
using KNSoft.ZPigeon.Server.Managed;
using KNSoft.ZPigeon.Tools;

var server = new NativeServer(AppContext.BaseDirectory);
var catalog = new ZPigeonToolCatalog(new ZPigeonApplication(server));
var tools = catalog.CreateTools();
var toolNames = tools.Select(tool => tool.Function.Name).ToHashSet(StringComparer.Ordinal);

Assert(toolNames.Count == tools.Count, "MCP tool names must be unique.");
Assert(tools.Select(tool => tool.Function.Name)
            .SequenceEqual(toolNames.Order(StringComparer.Ordinal)),
       "The MCP catalog must be deterministic.");
foreach (var tool in tools)
{
    var hasClientId = tool.Function.JsonSchema.GetProperty("properties").TryGetProperty("clientId", out _);
    var targetless = tool.Function.Name is "list_clients";
    Assert(hasClientId == !targetless,
           $"MCP tool {tool.Function.Name} has an invalid clientId schema.");
    Assert(!tool.ReadOnly || tool.Idempotent,
           $"Read-only tool {tool.Function.Name} must be idempotent.");
}
Assert(tools.Single(tool => tool.Function.Name == "query_browser_secrets").ReadOnly,
       "Browser secret reads must remain read-only.");
Assert(catalog.CreateAvailableTools().Select(tool => tool.Function.Name).SequenceEqual(["list_clients"]),
       "With no connected clients, MCP must advertise only discovery.");
var userCapabilities = ZPigeonApplication.GetAdministrationCapabilities(new ClientCapabilities(1UL << 8, 4, 0));
Assert(userCapabilities.Enumerations.All(operation =>
           ((int)Enum.Parse<AdministrationOperation>(operation) >> 8) == 9) &&
       userCapabilities.Controls.All(control =>
           ((int)Enum.Parse<AdministrationOperation>(control.Operation) >> 8) == 9) &&
       userCapabilities.Queries.Length == 0,
       "Administration operation names must be limited to the selected client's compiled modules.");
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
RemoteMediaTests.Run();

Console.WriteLine($"MCP tool catalog: {tools.Count} tools.");

static void Assert(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}
