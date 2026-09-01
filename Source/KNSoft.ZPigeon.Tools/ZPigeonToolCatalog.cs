using System.Globalization;
using System.Reflection;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.Json.Serialization.Metadata;
using KNSoft.ZPigeon.Application;
using Microsoft.Extensions.AI;

namespace KNSoft.ZPigeon.Tools;

[Flags]
public enum ToolAudience
{
    ExternalMcp = 1,
    BuiltInAgent = 2
}

public sealed record ZPigeonTool(
    AIFunction Function,
    ToolAudience Audience,
    bool ReadOnly,
    bool Destructive,
    bool Idempotent,
    bool OpenWorld,
    bool Sensitive);

public sealed class ZPigeonToolCatalog
{
    private static readonly JsonSerializerOptions SerializerOptions = CreateSerializerOptions();
    private readonly ToolFunctions functions;

    public ZPigeonToolCatalog(ZPigeonApplication application) => functions = new(application);

    public IReadOnlyList<ZPigeonTool> CreateExternalTools() => Create(ToolAudience.ExternalMcp, null);

    public IReadOnlyList<ZPigeonTool> CreateBuiltInTools(ulong clientId)
    {
        ArgumentOutOfRangeException.ThrowIfZero(clientId);
        return Create(ToolAudience.BuiltInAgent, clientId);
    }

    private IReadOnlyList<ZPigeonTool> Create(ToolAudience audience, ulong? clientId)
    {
        var result = new List<ZPigeonTool>();
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
                           metadata.Sensitive));
        }
        return result;
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
    bool sensitive = false) : Attribute
{
    internal string Name { get; } = name;
    internal string Description { get; } = description;
    internal bool ReadOnly { get; } = readOnly;
    internal bool Destructive { get; } = destructive;
    internal bool Idempotent { get; } = idempotent;
    internal bool OpenWorld { get; } = openWorld;
    internal ToolAudience Audience { get; } = audience;
    internal bool Sensitive { get; } = sensitive;
}

public sealed record ToolOperationResult(bool Success);
