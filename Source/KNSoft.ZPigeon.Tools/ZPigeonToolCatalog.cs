using System.Globalization;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;
using KNSoft.ZPigeon.Application;
using KNSoft.ZPigeon.Server.Managed;
using Microsoft.Extensions.AI;

namespace KNSoft.ZPigeon.Tools;

[Flags]
public enum ToolAudience
{
    ExternalMcp = 1,
    BuiltInAgent = 2
}

[Flags]
public enum AdministrationToolKind
{
    None = 0,
    Enumerate = 1,
    Query = 2,
    Control = 4,
    Any = Enumerate | Query | Control
}

public sealed record ZPigeonTool(
    AIFunction Function,
    ToolAudience Audience,
    bool ReadOnly,
    bool Destructive,
    bool Idempotent,
    bool OpenWorld,
    bool Sensitive,
    int ModuleId,
    AdministrationToolKind Administration);

public sealed class ZPigeonToolCatalog
{
    private static readonly JsonSerializerOptions SerializerOptions = CreateSerializerOptions();
    private readonly ToolFunctions functions;
    private readonly ZPigeonApplication application;
    private readonly IReadOnlyList<ZPigeonTool> externalTools;

    public ZPigeonToolCatalog(ZPigeonApplication application)
    {
        this.application = application;
        functions = new(application);
        externalTools = Create(ToolAudience.ExternalMcp, null, null);
    }

    public IReadOnlyList<ZPigeonTool> CreateExternalTools() => externalTools;

    public IReadOnlyList<ZPigeonTool> CreateProfileTools() => Create(ToolAudience.BuiltInAgent, null, null);

    public IReadOnlyList<ZPigeonTool> CreateAvailableExternalTools()
    {
        var capabilities = new ClientCapabilities();
        foreach (var client in application.GetClients())
            capabilities = new(capabilities.Modules | client.Capabilities.Modules,
                               capabilities.Transports | client.Capabilities.Transports,
                               capabilities.Execution | client.Capabilities.Execution);
        var administration = GetAdministrationKinds(capabilities);
        return [.. externalTools.Where(tool =>
            (tool.ModuleId == 0 || capabilities.SupportsModule(tool.ModuleId)) &&
            (tool.Administration == AdministrationToolKind.None || (tool.Administration & administration) != 0))];
    }

    public IReadOnlyList<ZPigeonTool> CreateBuiltInTools(ulong clientId)
    {
        ArgumentOutOfRangeException.ThrowIfZero(clientId);
        return CreateBuiltInTools(clientId, application.GetClientCapabilities(clientId));
    }

    public IReadOnlyList<ZPigeonTool> CreateBuiltInTools(ulong clientId, ClientCapabilities capabilities)
    {
        ArgumentOutOfRangeException.ThrowIfZero(clientId);
        return Create(ToolAudience.BuiltInAgent, clientId, capabilities);
    }

    private IReadOnlyList<ZPigeonTool> Create(
        ToolAudience audience,
        ulong? clientId,
        ClientCapabilities? capabilities)
    {
        var result = new List<ZPigeonTool>();
        var administration = capabilities.HasValue ?
            GetAdministrationKinds(capabilities.Value) : AdministrationToolKind.Any;
        var methods = typeof(ToolFunctions).GetMethods(BindingFlags.Instance |
                                                       BindingFlags.Public |
                                                       BindingFlags.DeclaredOnly)
                                           .Select(method => (Method: method,
                                                              Metadata: method.GetCustomAttribute<
                                                                  ZPigeonToolAttribute>()))
                                           .Where(value => value.Metadata is not null &&
                                                           (value.Metadata.Audience & audience) != 0)
                                           .OrderBy(value => value.Metadata!.Name,
                                                    StringComparer.Ordinal);
        foreach (var (method, metadata) in methods)
        {
            if (capabilities.HasValue &&
                (metadata!.ModuleId != 0 && !capabilities.Value.SupportsModule(metadata.ModuleId) ||
                 metadata.Administration != AdministrationToolKind.None &&
                 (metadata.Administration & administration) == 0)) continue;
            var options = new AIFunctionFactoryOptions
            {
                Name = metadata!.Name,
                Description = metadata.Description,
                SerializerOptions = SerializerOptions
            };
            if (clientId.HasValue)
            {
                var value = clientId.Value.ToString(CultureInfo.InvariantCulture);
                options.ConfigureParameterBinding = parameter =>
                    StringComparer.Ordinal.Equals(parameter.Name, "clientId") ?
                        new AIFunctionFactoryOptions.ParameterBindingOptions
                        {
                            ExcludeFromSchema = true,
                            BindParameter = (_, _) => value
                        } :
                        default;
            }
            result.Add(new(AIFunctionFactory.Create(method, functions, options),
                           metadata.Audience,
                           metadata.ReadOnly,
                           metadata.Destructive,
                           metadata.ReadOnly || metadata.Idempotent,
                           metadata.OpenWorld,
                           metadata.Sensitive,
                           metadata.ModuleId,
                           metadata.Administration));
        }
        return result;
    }

    private static AdministrationToolKind GetAdministrationKinds(ClientCapabilities capabilities)
    {
        var operations = ZPigeonApplication.GetAdministrationCapabilities(capabilities);
        return (operations.Enumerations.Length != 0 ? AdministrationToolKind.Enumerate : AdministrationToolKind.None) |
               (operations.Queries.Length != 0 ? AdministrationToolKind.Query : AdministrationToolKind.None) |
               (operations.Controls.Length != 0 ? AdministrationToolKind.Control : AdministrationToolKind.None);
    }

    private static JsonSerializerOptions CreateSerializerOptions()
    {
        var options = new JsonSerializerOptions(JsonSerializerDefaults.Web)
        {
            TypeInfoResolver = new DefaultJsonTypeInfoResolver()
        };
        options.Converters.Add(new JsonStringEnumConverter());
        return options;
    }
}

[AttributeUsage(AttributeTargets.Method)]
internal sealed class ZPigeonToolAttribute(
    string name,
    string description,
    bool readOnly,
    bool destructive = false,
    bool idempotent = false,
    bool openWorld = false,
    ToolAudience audience = ToolAudience.ExternalMcp | ToolAudience.BuiltInAgent,
    bool sensitive = false,
    int moduleId = 0,
    AdministrationToolKind administration = AdministrationToolKind.None) : Attribute
{
    internal string Name { get; } = name;
    internal string Description { get; } = description;
    internal bool ReadOnly { get; } = readOnly;
    internal bool Destructive { get; } = destructive;
    internal bool Idempotent { get; } = idempotent;
    internal bool OpenWorld { get; } = openWorld;
    internal ToolAudience Audience { get; } = audience;
    internal bool Sensitive { get; } = sensitive;
    internal int ModuleId { get; } = moduleId;
    internal AdministrationToolKind Administration { get; } = administration;
}

public sealed record ToolOperationResult(bool Success);
public sealed record ToolAdministrationControlResult(bool Success, AdministrationControlResult Result);
