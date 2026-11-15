using KNSoft.ZPigeon.Tools;
using Microsoft.Extensions.DependencyInjection;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;
using System.Text.Json.Nodes;

namespace KNSoft.ZPigeon.Web;

internal static class McpToolFactory
{
    internal static IMcpServerBuilder WithDynamicTools(this IMcpServerBuilder builder, ZPigeonToolCatalog catalog)
    {
        var tools = Create(catalog.CreateExternalTools()).ToDictionary(tool => tool.ProtocolTool.Name,
                                                                      StringComparer.Ordinal);
        return builder.WithListToolsHandler((_, _) => ValueTask.FromResult(new ListToolsResult
        {
            Tools = [.. catalog.CreateAvailableExternalTools().Select(tool => tools[tool.Function.Name].ProtocolTool)],
            TimeToLive = TimeSpan.Zero,
            CacheScope = CacheScope.Private
        })).WithCallToolHandler((context, cancellationToken) =>
        {
            var name = context.Params?.Name ?? string.Empty;
            if (!tools.TryGetValue(name, out var tool) ||
                !catalog.CreateAvailableExternalTools().Any(available => available.Function.Name == name))
                return ValueTask.FromResult(new CallToolResult
                {
                    IsError = true,
                    Content = [new TextContentBlock { Text = "No connected client supports the requested tool." }]
                });
            return tool.InvokeAsync(context, cancellationToken);
        });
    }

    internal static McpServerTool[] Create(IEnumerable<ZPigeonTool> tools) =>
        [.. tools.Select(tool => McpServerTool.Create(tool.Function,
            new McpServerToolCreateOptions
            {
                ReadOnly = tool.ReadOnly,
                Destructive = tool.Destructive,
                Idempotent = tool.Idempotent,
                OpenWorld = tool.OpenWorld,
                Meta = tool.Sensitive ?
                    new JsonObject { ["org.knsoft.zpigeon/sensitive"] = true } :
                    null,
                UseStructuredContent = true
            }))];
}
