using KNSoft.ZPigeon.Tools;
using ModelContextProtocol.Server;
using System.Text.Json.Nodes;

namespace KNSoft.ZPigeon.Web;

internal static class McpToolFactory
{
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
