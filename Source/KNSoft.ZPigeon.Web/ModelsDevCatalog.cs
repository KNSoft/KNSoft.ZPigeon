using KNSoft.ZPigeon.Agent;
using System.Text.Json;

namespace KNSoft.ZPigeon.Web;

internal sealed class ModelsDevCatalog
{
    private readonly Dictionary<string, CatalogProviderData> providers = new(StringComparer.Ordinal);

    internal ModelsDevCatalog(string path)
    {
        using var document = JsonDocument.Parse(File.ReadAllBytes(path));
        foreach (var property in document.RootElement.EnumerateObject())
        {
            var value = property.Value;
            var models = value.GetProperty("models")
                              .EnumerateObject()
                              .Select(model => ReadModel(model.Value))
                              .ToArray();
            var npm = value.TryGetProperty("npm", out var npmValue) ? npmValue.GetString() : null;
            var api = value.TryGetProperty("api", out var apiValue) && apiValue.ValueKind == JsonValueKind.String ?
                apiValue.GetString() :
                null;
            providers.Add(property.Name,
                          new(new(property.Name,
                                  value.GetProperty("name").GetString() ?? property.Name,
                                  api ?? GetKnownApi(property.Name),
                                  GetProtocol(npm),
                                  models.Length),
                              models));
        }
    }

    internal CatalogProvider[] GetProviders() =>
        [.. providers.Values.Select(value => value.Provider)
                          .OrderBy(value => value.Name, StringComparer.OrdinalIgnoreCase)
                          .ThenBy(value => value.Id, StringComparer.Ordinal)];

    internal CatalogModel[] GetModels(string provider)
    {
        if (!providers.TryGetValue(provider, out var value))
        {
            throw new KeyNotFoundException("The catalog provider does not exist.");
        }
        return value.Models;
    }

    private static CatalogModel ReadModel(JsonElement value)
    {
        var limit = value.GetProperty("limit");
        var modalities = value.GetProperty("modalities");
        return new(value.GetProperty("id").GetString() ?? throw new InvalidDataException("A model has no id."),
                   value.GetProperty("name").GetString() ?? throw new InvalidDataException("A model has no name."),
                   value.TryGetProperty("description", out var description) ? description.GetString() : null,
                   limit.GetProperty("context").GetInt32(),
                   limit.GetProperty("output").GetInt32(),
                   ReadStrings(modalities.GetProperty("input")),
                   ReadStrings(modalities.GetProperty("output")),
                   value.TryGetProperty("reasoning", out var reasoning) && reasoning.GetBoolean(),
                   value.TryGetProperty("tool_call", out var toolCall) && toolCall.GetBoolean());
    }

    private static string[] ReadStrings(JsonElement value) =>
        [.. value.EnumerateArray().Select(item => item.GetString() ?? string.Empty)];

    private static ModelProtocol? GetProtocol(string? npm) => npm switch
    {
        "@ai-sdk/openai" => ModelProtocol.OpenAIResponses,
        "@ai-sdk/openai-compatible" => ModelProtocol.OpenAIChatCompletions,
        "@ai-sdk/anthropic" => ModelProtocol.AnthropicMessages,
        _ => null
    };

    private static string? GetKnownApi(string provider) => provider switch
    {
        "openai" => "https://api.openai.com/v1",
        "anthropic" => "https://api.anthropic.com/v1",
        _ => null
    };

    private sealed record CatalogProviderData(CatalogProvider Provider, CatalogModel[] Models);
}

internal sealed record CatalogProvider(
    string Id,
    string Name,
    string? Api,
    ModelProtocol? Protocol,
    int ModelCount);

internal sealed record CatalogModel(
    string Id,
    string Name,
    string? Description,
    int ContextWindow,
    int MaximumOutputTokens,
    string[] InputModalities,
    string[] OutputModalities,
    bool Reasoning,
    bool ToolCall);
