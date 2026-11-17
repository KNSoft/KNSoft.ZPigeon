using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;
using KNSoft.ZPigeon.Application;
using KNSoft.ZPigeon.Server.Managed;
using Microsoft.Extensions.AI;

namespace KNSoft.ZPigeon.Tools;

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
    bool ReadOnly,
    bool Idempotent,
    int ModuleId,
    AdministrationToolKind Administration);

public sealed class ZPigeonToolCatalog
{
    private static readonly JsonSerializerOptions SerializerOptions = CreateSerializerOptions();
    private readonly ToolFunctions functions;
    private readonly ZPigeonApplication application;
    private readonly IReadOnlyList<ZPigeonTool> tools;

    public ZPigeonToolCatalog(ZPigeonApplication application)
    {
        this.application = application;
        functions = new(application);
        tools = Create();
    }

    public IReadOnlyList<ZPigeonTool> CreateTools() => tools;

    public IReadOnlyList<ZPigeonTool> CreateAvailableTools()
    {
        var capabilities = new ClientCapabilities();
        foreach (var client in application.GetClients())
            capabilities = new(capabilities.Modules | client.Capabilities.Modules,
                               capabilities.Transports | client.Capabilities.Transports,
                               capabilities.Execution | client.Capabilities.Execution);
        var administration = GetAdministrationKinds(capabilities);
        return [.. tools.Where(tool =>
            (tool.ModuleId == 0 || capabilities.SupportsModule(tool.ModuleId)) &&
            (tool.Administration == AdministrationToolKind.None || (tool.Administration & administration) != 0))];
    }

    private IReadOnlyList<ZPigeonTool> Create()
    {
        var result = new List<ZPigeonTool>();
        var methods = typeof(ToolFunctions).GetMethods(BindingFlags.Instance |
                                                       BindingFlags.Public |
                                                       BindingFlags.DeclaredOnly)
                                            .Select(method => (Method: method,
                                                               Metadata: method.GetCustomAttribute<
                                                                   ZPigeonToolAttribute>()))
                                            .Where(value => value.Metadata is not null)
                                            .OrderBy(value => value.Metadata!.Name,
                                                    StringComparer.Ordinal);
        foreach (var (method, metadata) in methods)
        {
            var options = new AIFunctionFactoryOptions
            {
                Name = metadata!.Name,
                Description = metadata.Description,
                SerializerOptions = SerializerOptions
            };
            result.Add(new(AIFunctionFactory.Create(method, functions, options),
                           metadata.ReadOnly,
                           metadata.ReadOnly || metadata.Idempotent,
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
    bool idempotent = false,
    int moduleId = 0,
    AdministrationToolKind administration = AdministrationToolKind.None) : Attribute
{
    internal string Name { get; } = name;
    internal string Description { get; } = description;
    internal bool ReadOnly { get; } = readOnly;
    internal bool Idempotent { get; } = idempotent;
    internal int ModuleId { get; } = moduleId;
    internal AdministrationToolKind Administration { get; } = administration;
}

public sealed record ToolOperationResult(bool Success);
public sealed record ToolAdministrationControlResult(bool Success, AdministrationControlResult Result);
