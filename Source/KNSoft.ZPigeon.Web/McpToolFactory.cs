using KNSoft.ZPigeon.Tools;
using Microsoft.Extensions.DependencyInjection;
using ModelContextProtocol.Protocol;
using ModelContextProtocol.Server;
using System.Text.Json;

namespace KNSoft.ZPigeon.Web;

internal static class McpToolFactory
{
    internal static IMcpServerBuilder WithDynamicTools(
        this IMcpServerBuilder builder,
        ZPigeonToolCatalog catalog,
        McpPolicyStore policyStore)
    {
        var tools = Create(catalog.CreateTools()).ToDictionary(tool => tool.ProtocolTool.Name,
                                                                       StringComparer.Ordinal);
        return builder.WithListToolsHandler((_, _) =>
        {
            var policy = policyStore.Get();
            var available = policy.Enabled ?
                catalog.CreateAvailableTools().Where(tool => !policy.ReadOnlyOnly || tool.ReadOnly) :
                [];
            return ValueTask.FromResult(new ListToolsResult
            {
                Tools = [.. available.Select(tool => tools[tool.Function.Name].ProtocolTool)],
                TimeToLive = TimeSpan.Zero,
                CacheScope = CacheScope.Private
            });
        }).WithCallToolHandler((context, cancellationToken) =>
        {
            return InvokeAsync(context, cancellationToken, catalog, policyStore, tools);
        });
    }

    private static async ValueTask<CallToolResult> InvokeAsync(
        RequestContext<CallToolRequestParams> context,
        CancellationToken cancellationToken,
        ZPigeonToolCatalog catalog,
        McpPolicyStore policyStore,
        IReadOnlyDictionary<string, McpServerTool> tools)
    {
        var policy = policyStore.Get();
        if (!policy.Enabled) return Error("The MCP service is disabled.");
        var name = context.Params?.Name ?? string.Empty;
        if (!tools.TryGetValue(name, out var tool) ||
            !catalog.CreateAvailableTools().Any(available => available.Function.Name == name))
            return Error("No connected client supports the requested tool.");
        if (policy.ReadOnlyOnly && !catalog.CreateTools().Single(
                available => available.Function.Name == name).ReadOnly)
            return Error("The MCP service is configured to allow read-only tools only.");
        using var lease = policyStore.TryAcquire();
        if (lease is null) return Error("The MCP call limit has been reached.");
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(policy.CallTimeoutSeconds));
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken, timeout.Token);
        try
        {
            var result = await tool.InvokeAsync(context, linked.Token).ConfigureAwait(false);
            if (JsonSerializer.SerializeToUtf8Bytes(result).Length > policy.MaxResultSizeBytes)
                return Error("The MCP tool result exceeds the configured size limit.");
            return result;
        }
        catch (OperationCanceledException) when (timeout.IsCancellationRequested &&
                                                   !cancellationToken.IsCancellationRequested)
        {
            return Error("The MCP tool call timed out.");
        }
    }

    private static CallToolResult Error(string message) => new()
    {
        IsError = true,
        Content = [new TextContentBlock { Text = message }]
    };

    internal static McpServerTool[] Create(IEnumerable<ZPigeonTool> tools) =>
        [.. tools.Select(tool => McpServerTool.Create(tool.Function,
            new McpServerToolCreateOptions
            {
                ReadOnly = tool.ReadOnly,
                Idempotent = tool.Idempotent,
                UseStructuredContent = true
            }))];
}
