using KNSoft.ZPigeon.Tools;
using Microsoft.Extensions.AI;
using System.IO.Compression;
using System.Net;
using System.Net.Http.Headers;
using System.Net.Http.Json;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace KNSoft.ZPigeon.Agent;

internal sealed record ModelCall(
    string SystemPrompt,
    IReadOnlyList<SessionItem> Items,
    IReadOnlyList<ZPigeonTool> Tools,
    int? MaximumOutputTokens = null);

internal sealed record ModelToolCall(string Id, string Name, string Arguments);

internal sealed record ModelResult(
    string Text,
    ModelToolCall[] ToolCalls,
    string? FinishReason,
    string ProtocolJson,
    TokenUsage Usage);

internal sealed class ModelProtocolClient
{
    private const long MaximumResponseBytes = 16 * 1024 * 1024;
    private static readonly HttpClient HttpClient = new(new SocketsHttpHandler
    {
        AutomaticDecompression = DecompressionMethods.All,
        PooledConnectionLifetime = TimeSpan.FromMinutes(10)
    })
    {
        Timeout = Timeout.InfiniteTimeSpan
    };

    internal async Task<ModelResult> CompleteAsync(
        ModelConfiguration model,
        ModelCall call,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(call);
        var requestBody = model.Protocol switch
        {
            ModelProtocol.OpenAIResponses => CreateResponsesRequest(model, call),
            ModelProtocol.OpenAIChatCompletions => CreateChatRequest(model, call),
            ModelProtocol.AnthropicMessages => CreateAnthropicRequest(model, call),
            _ => throw new ArgumentOutOfRangeException(nameof(model))
        };
        using var request = new HttpRequestMessage(HttpMethod.Post, GetEndpoint(model))
        {
            Content = JsonContent.Create(requestBody)
        };
        request.Headers.UserAgent.ParseAdd("KNSoft.ZPigeon/1.0");
        AddAuthentication(request, model);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(model.RequestTimeoutSeconds));
        using var response = await HttpClient.SendAsync(request,
                                                       HttpCompletionOption.ResponseHeadersRead,
                                                       timeout.Token).ConfigureAwait(false);
        await response.Content.LoadIntoBufferAsync(MaximumResponseBytes, timeout.Token).ConfigureAwait(false);
        var text = await response.Content.ReadAsStringAsync(timeout.Token).ConfigureAwait(false);
        JsonObject json;
        try
        {
            json = JsonNode.Parse(text) as JsonObject ??
                   throw new JsonException("The response root is not an object.");
        }
        catch (JsonException exception)
        {
            if (!response.IsSuccessStatusCode)
            {
                throw new HttpRequestException($"HTTP {(int)response.StatusCode}: {Truncate(text)}",
                                               exception,
                                               response.StatusCode);
            }
            throw new InvalidDataException("The model returned invalid JSON.", exception);
        }
        if (!response.IsSuccessStatusCode)
        {
            var message = json["error"]?["message"]?.GetValue<string>() ??
                          json["message"]?.GetValue<string>() ??
                          text;
            throw new HttpRequestException($"HTTP {(int)response.StatusCode}: {Truncate(message)}",
                                           null,
                                           response.StatusCode);
        }
        return model.Protocol switch
        {
            ModelProtocol.OpenAIResponses => ParseResponses(json),
            ModelProtocol.OpenAIChatCompletions => ParseChat(json),
            ModelProtocol.AnthropicMessages => ParseAnthropic(json),
            _ => throw new ArgumentOutOfRangeException(nameof(model))
        };
    }

    internal static void ValidateAdvanced(ModelProtocol protocol, string value)
    {
        ArgumentNullException.ThrowIfNull(value);
        JsonObject advanced;
        try
        {
            advanced = JsonNode.Parse(value) as JsonObject ??
                       throw new JsonException("The advanced value must be an object.");
        }
        catch (JsonException exception)
        {
            throw new ArgumentException("Advanced parameters must be a JSON object.", nameof(value), exception);
        }
        var reserved = protocol switch
        {
            ModelProtocol.OpenAIResponses =>
                new[]
                {
                    "model", "input", "instructions", "tools", "stream", "store", "include",
                    "previous_response_id"
                },
            ModelProtocol.OpenAIChatCompletions =>
                ["model", "messages", "tools", "stream"],
            ModelProtocol.AnthropicMessages => ["model", "messages", "system", "tools", "stream"],
            _ => throw new ArgumentOutOfRangeException(nameof(protocol))
        };
        var key = reserved.FirstOrDefault(advanced.ContainsKey);
        if (key is not null)
        {
            throw new ArgumentException($"Advanced parameters cannot set the structural field '{key}'.",
                                        nameof(value));
        }
    }

    private static JsonObject CreateResponsesRequest(ModelConfiguration model, ModelCall call)
    {
        var input = new JsonArray();
        foreach (var item in call.Items)
        {
            switch (item.Kind)
            {
                case SessionItemKind.User when item.State != SessionItemState.Queued:
                    input.Add(new JsonObject { ["role"] = "user", ["content"] = item.Content });
                    break;
                case SessionItemKind.Assistant:
                    if (item.Protocol == ModelProtocol.OpenAIResponses &&
                        ParseNode(item.ProtocolJson) is JsonArray output)
                    {
                        foreach (var value in output) input.Add(value?.DeepClone());
                    }
                    else if (item.Content.Length != 0)
                    {
                        input.Add(new JsonObject { ["role"] = "assistant", ["content"] = item.Content });
                    }
                    break;
                case SessionItemKind.ToolCall when !HasProtocolAssistant(call.Items, item):
                    input.Add(new JsonObject
                    {
                        ["type"] = "function_call",
                        ["call_id"] = item.CallId,
                        ["name"] = item.Name,
                        ["arguments"] = item.Content
                    });
                    break;
                case SessionItemKind.ToolResult:
                    input.Add(new JsonObject
                    {
                        ["type"] = "function_call_output",
                        ["call_id"] = item.CallId,
                        ["output"] = item.Content
                    });
                    break;
            }
        }
        var result = new JsonObject
        {
            ["model"] = model.ModelId,
            ["instructions"] = call.SystemPrompt,
            ["input"] = input,
            ["max_output_tokens"] = call.MaximumOutputTokens ?? model.MaximumOutputTokens,
            ["store"] = false,
            ["include"] = new JsonArray("reasoning.encrypted_content")
        };
        if (call.Tools.Count != 0) result["tools"] = CreateResponsesTools(call.Tools);
        AddOpenAIReasoning(result, model.Reasoning, true);
        MergeAdvanced(result, model.AdvancedJson);
        return result;
    }

    private static JsonObject CreateChatRequest(ModelConfiguration model, ModelCall call)
    {
        var messages = new JsonArray(new JsonObject { ["role"] = "system", ["content"] = call.SystemPrompt });
        foreach (var item in call.Items)
        {
            switch (item.Kind)
            {
                case SessionItemKind.User when item.State != SessionItemState.Queued:
                    messages.Add(new JsonObject { ["role"] = "user", ["content"] = item.Content });
                    break;
                case SessionItemKind.Assistant:
                    if (item.Protocol == ModelProtocol.OpenAIChatCompletions &&
                        ParseNode(item.ProtocolJson) is JsonObject message)
                    {
                        messages.Add(message.DeepClone());
                    }
                    else
                    {
                        var assistant = new JsonObject { ["role"] = "assistant", ["content"] = item.Content };
                        var calls = CreateChatHistoryCalls(call.Items, item);
                        if (calls.Count != 0) assistant["tool_calls"] = calls;
                        messages.Add(assistant);
                    }
                    break;
                case SessionItemKind.ToolResult:
                    messages.Add(new JsonObject
                    {
                        ["role"] = "tool",
                        ["tool_call_id"] = item.CallId,
                        ["content"] = item.Content
                    });
                    break;
            }
        }
        var result = new JsonObject
        {
            ["model"] = model.ModelId,
            ["messages"] = messages,
            ["max_tokens"] = call.MaximumOutputTokens ?? model.MaximumOutputTokens
        };
        if (call.Tools.Count != 0) result["tools"] = CreateChatTools(call.Tools);
        AddOpenAIReasoning(result, model.Reasoning, false);
        MergeAdvanced(result, model.AdvancedJson);
        return result;
    }

    private static JsonObject CreateAnthropicRequest(ModelConfiguration model, ModelCall call)
    {
        var messages = new JsonArray();
        foreach (var item in call.Items)
        {
            switch (item.Kind)
            {
                case SessionItemKind.User when item.State != SessionItemState.Queued:
                    AddAnthropicMessage(messages,
                                        "user",
                                        new JsonObject { ["type"] = "text", ["text"] = item.Content });
                    break;
                case SessionItemKind.Assistant:
                    if (item.Protocol == ModelProtocol.AnthropicMessages &&
                        ParseNode(item.ProtocolJson) is JsonArray content)
                    {
                        AddAnthropicMessage(messages, "assistant", content);
                    }
                    else
                    {
                        var blocks = new JsonArray();
                        if (item.Content.Length != 0)
                        {
                            blocks.Add(new JsonObject { ["type"] = "text", ["text"] = item.Content });
                        }
                        foreach (var toolCall in GetStepToolCalls(call.Items, item))
                        {
                            blocks.Add(new JsonObject
                            {
                                ["type"] = "tool_use",
                                ["id"] = toolCall.CallId,
                                ["name"] = toolCall.Name,
                                ["input"] = ParseNode(toolCall.Content) ?? new JsonObject()
                            });
                        }
                        if (blocks.Count != 0) AddAnthropicMessage(messages, "assistant", blocks);
                    }
                    break;
                case SessionItemKind.ToolResult:
                    AddAnthropicMessage(messages,
                                        "user",
                                        new JsonObject
                                        {
                                            ["type"] = "tool_result",
                                            ["tool_use_id"] = item.CallId,
                                            ["content"] = item.Content
                                        });
                    break;
            }
        }
        var result = new JsonObject
        {
            ["model"] = model.ModelId,
            ["system"] = call.SystemPrompt,
            ["messages"] = messages,
            ["max_tokens"] = call.MaximumOutputTokens ?? model.MaximumOutputTokens
        };
        if (call.Tools.Count != 0) result["tools"] = CreateAnthropicTools(call.Tools);
        AddAnthropicReasoning(result, model.Reasoning);
        MergeAdvanced(result, model.AdvancedJson);
        return result;
    }

    private static ModelResult ParseResponses(JsonObject json)
    {
        var output = json["output"] as JsonArray ?? throw new InvalidDataException("The response has no output.");
        var text = new List<string>();
        var calls = new List<ModelToolCall>();
        foreach (var item in output.OfType<JsonObject>())
        {
            switch (item["type"]?.GetValue<string>())
            {
                case "message":
                    if (item["content"] is not JsonArray content) break;
                    text.AddRange(content.OfType<JsonObject>()
                                         .Where(value => value["type"]?.GetValue<string>() is
                                             "output_text" or "text")
                                         .Select(value => value["text"]?.GetValue<string>() ?? string.Empty));
                    break;
                case "function_call":
                    calls.Add(new(item["call_id"]?.GetValue<string>() ??
                                      throw new InvalidDataException("A tool call has no call id."),
                                  item["name"]?.GetValue<string>() ??
                                      throw new InvalidDataException("A tool call has no name."),
                                  item["arguments"]?.GetValue<string>() ?? "{}"));
                    break;
            }
        }
        return new(string.Concat(text),
                   [.. calls],
                   json["status"]?.GetValue<string>(),
                   output.ToJsonString(),
                   ParseResponsesUsage(json["usage"] as JsonObject));
    }

    private static ModelResult ParseChat(JsonObject json)
    {
        var choice = (json["choices"] as JsonArray)?.OfType<JsonObject>().FirstOrDefault() ??
                     throw new InvalidDataException("The response has no choice.");
        var message = choice["message"] as JsonObject ??
                      throw new InvalidDataException("The response has no message.");
        var calls = (message["tool_calls"] as JsonArray)?.OfType<JsonObject>().Select(value =>
        {
            var function = value["function"] as JsonObject ??
                           throw new InvalidDataException("A tool call has no function.");
            return new ModelToolCall(value["id"]?.GetValue<string>() ??
                                         throw new InvalidDataException("A tool call has no id."),
                                     function["name"]?.GetValue<string>() ??
                                         throw new InvalidDataException("A tool call has no name."),
                                     function["arguments"]?.GetValue<string>() ?? "{}");
        }).ToArray() ?? [];
        return new(ReadChatContent(message["content"]),
                   calls,
                   choice["finish_reason"]?.GetValue<string>(),
                   message.ToJsonString(),
                   ParseChatUsage(json["usage"] as JsonObject));
    }

    private static ModelResult ParseAnthropic(JsonObject json)
    {
        var content = json["content"] as JsonArray ??
                      throw new InvalidDataException("The response has no content.");
        var text = new List<string>();
        var calls = new List<ModelToolCall>();
        foreach (var item in content.OfType<JsonObject>())
        {
            switch (item["type"]?.GetValue<string>())
            {
                case "text":
                    text.Add(item["text"]?.GetValue<string>() ?? string.Empty);
                    break;
                case "tool_use":
                    calls.Add(new(item["id"]?.GetValue<string>() ??
                                      throw new InvalidDataException("A tool call has no id."),
                                  item["name"]?.GetValue<string>() ??
                                      throw new InvalidDataException("A tool call has no name."),
                                  (item["input"] ?? new JsonObject()).ToJsonString()));
                    break;
            }
        }
        return new(string.Concat(text),
                   [.. calls],
                   json["stop_reason"]?.GetValue<string>(),
                   content.ToJsonString(),
                   ParseAnthropicUsage(json["usage"] as JsonObject));
    }

    private static JsonArray CreateResponsesTools(IReadOnlyList<ZPigeonTool> tools) =>
        new([.. tools.Select(tool => (JsonNode)new JsonObject
        {
            ["type"] = "function",
            ["name"] = tool.Function.Name,
            ["description"] = tool.Function.Description,
            ["parameters"] = ParseSchema(tool.Function)
        })]);

    private static JsonArray CreateChatTools(IReadOnlyList<ZPigeonTool> tools) =>
        new([.. tools.Select(tool => (JsonNode)new JsonObject
        {
            ["type"] = "function",
            ["function"] = new JsonObject
            {
                ["name"] = tool.Function.Name,
                ["description"] = tool.Function.Description,
                ["parameters"] = ParseSchema(tool.Function)
            }
        })]);

    private static JsonArray CreateAnthropicTools(IReadOnlyList<ZPigeonTool> tools) =>
        new([.. tools.Select(tool => (JsonNode)new JsonObject
        {
            ["name"] = tool.Function.Name,
            ["description"] = tool.Function.Description,
            ["input_schema"] = ParseSchema(tool.Function)
        })]);

    private static JsonNode ParseSchema(AIFunction function) =>
        JsonNode.Parse(function.JsonSchema.GetRawText()) ?? new JsonObject();

    private static JsonArray CreateChatHistoryCalls(IReadOnlyList<SessionItem> items, SessionItem assistant) =>
        new([.. GetStepToolCalls(items, assistant).Select(item => (JsonNode)new JsonObject
        {
            ["id"] = item.CallId,
            ["type"] = "function",
            ["function"] = new JsonObject { ["name"] = item.Name, ["arguments"] = item.Content }
        })]);

    private static IEnumerable<SessionItem> GetStepToolCalls(
        IReadOnlyList<SessionItem> items,
        SessionItem assistant) =>
        items.Where(item => item.Kind == SessionItemKind.ToolCall &&
                            item.RunId == assistant.RunId &&
                            item.Step == assistant.Step);

    private static bool HasProtocolAssistant(IReadOnlyList<SessionItem> items, SessionItem toolCall) =>
        items.Any(item => item.Kind == SessionItemKind.Assistant &&
                          item.RunId == toolCall.RunId &&
                          item.Step == toolCall.Step &&
                          item.Protocol == ModelProtocol.OpenAIResponses &&
                          item.ProtocolJson is not null);

    private static void AddAnthropicMessage(JsonArray messages, string role, JsonNode content)
    {
        var blocks = content as JsonArray ?? new JsonArray(content);
        if (messages.LastOrDefault() is JsonObject previous &&
            previous["role"]?.GetValue<string>() == role &&
            previous["content"] is JsonArray previousContent)
        {
            foreach (var block in blocks) previousContent.Add(block?.DeepClone());
            return;
        }
        messages.Add(new JsonObject { ["role"] = role, ["content"] = blocks.DeepClone() });
    }

    private static void AddOpenAIReasoning(JsonObject request, ReasoningEffort effort, bool responses)
    {
        if (effort == ReasoningEffort.None) return;
        var value = ToWireEffort(effort);
        if (responses) request["reasoning"] = new JsonObject { ["effort"] = value };
        else request["reasoning_effort"] = value;
    }

    private static void AddAnthropicReasoning(JsonObject request, ReasoningEffort effort)
    {
        if (effort == ReasoningEffort.None) return;
        request["thinking"] = new JsonObject { ["type"] = "adaptive" };
        request["output_config"] = new JsonObject
        {
            ["effort"] = effort switch
            {
                ReasoningEffort.Minimal => "low",
                ReasoningEffort.XHigh or ReasoningEffort.Max => "max",
                _ => ToWireEffort(effort)
            }
        };
    }

    private static string ToWireEffort(ReasoningEffort value) => value switch
    {
        ReasoningEffort.Minimal => "minimal",
        ReasoningEffort.Low => "low",
        ReasoningEffort.Medium => "medium",
        ReasoningEffort.High => "high",
        ReasoningEffort.XHigh => "xhigh",
        ReasoningEffort.Max => "max",
        _ => throw new ArgumentOutOfRangeException(nameof(value))
    };

    private static void MergeAdvanced(JsonObject target, string value)
    {
        if (JsonNode.Parse(value) is not JsonObject advanced) return;
        foreach (var property in advanced) target[property.Key] = property.Value?.DeepClone();
    }

    private static Uri GetEndpoint(ModelConfiguration model)
    {
        var path = model.Protocol switch
        {
            ModelProtocol.OpenAIResponses => "responses",
            ModelProtocol.OpenAIChatCompletions => "chat/completions",
            ModelProtocol.AnthropicMessages => "messages",
            _ => throw new ArgumentOutOfRangeException(nameof(model))
        };
        var absolute = model.BaseUrl.AbsoluteUri.TrimEnd('/');
        return absolute.EndsWith('/' + path, StringComparison.OrdinalIgnoreCase) ?
            model.BaseUrl :
            new Uri(absolute + '/' + path, UriKind.Absolute);
    }

    private static void AddAuthentication(HttpRequestMessage request, ModelConfiguration model)
    {
        switch (model.Authentication)
        {
            case ModelAuthentication.ApiKey when model.Protocol == ModelProtocol.AnthropicMessages:
                request.Headers.TryAddWithoutValidation("x-api-key", model.Credential);
                break;
            case ModelAuthentication.ApiKey:
            case ModelAuthentication.BearerToken:
            case ModelAuthentication.OAuth:
                request.Headers.Authorization = new AuthenticationHeaderValue("Bearer", model.Credential);
                break;
            case ModelAuthentication.None:
                break;
            default:
                throw new ArgumentOutOfRangeException(nameof(model));
        }
        if (model.Protocol == ModelProtocol.AnthropicMessages)
        {
            request.Headers.TryAddWithoutValidation("anthropic-version", "2023-06-01");
        }
    }

    private static TokenUsage ParseResponsesUsage(JsonObject? usage)
    {
        if (usage is null) return TokenUsage.Empty;
        return new(ReadInt64(usage, "input_tokens"),
                   ReadInt64(usage["input_tokens_details"] as JsonObject, "cached_tokens"),
                   ReadInt64(usage, "output_tokens"),
                   ReadInt64(usage["output_tokens_details"] as JsonObject, "reasoning_tokens"),
                   ReadInt64(usage, "total_tokens"),
                   usage.ToJsonString());
    }

    private static TokenUsage ParseChatUsage(JsonObject? usage)
    {
        if (usage is null) return TokenUsage.Empty;
        return new(ReadInt64(usage, "prompt_tokens"),
                   ReadInt64(usage["prompt_tokens_details"] as JsonObject, "cached_tokens"),
                   ReadInt64(usage, "completion_tokens"),
                   ReadInt64(usage["completion_tokens_details"] as JsonObject, "reasoning_tokens"),
                   ReadInt64(usage, "total_tokens"),
                   usage.ToJsonString());
    }

    private static TokenUsage ParseAnthropicUsage(JsonObject? usage)
    {
        if (usage is null) return TokenUsage.Empty;
        var uncached = ReadInt64(usage, "input_tokens");
        var created = ReadInt64(usage, "cache_creation_input_tokens");
        var cached = ReadInt64(usage, "cache_read_input_tokens");
        long? input = uncached.HasValue || created.HasValue || cached.HasValue ?
            uncached.GetValueOrDefault() + created.GetValueOrDefault() + cached.GetValueOrDefault() :
            null;
        var output = ReadInt64(usage, "output_tokens");
        return new(input,
                   cached,
                   output,
                   null,
                   input.HasValue || output.HasValue ? input.GetValueOrDefault() + output.GetValueOrDefault() : null,
                   usage.ToJsonString());
    }

    private static long? ReadInt64(JsonObject? value, string name) =>
        value?[name] is JsonValue item && item.TryGetValue<long>(out var result) ? result : null;

    private static JsonNode? ParseNode(string? value)
    {
        if (string.IsNullOrEmpty(value)) return null;
        try
        {
            return JsonNode.Parse(value);
        }
        catch (JsonException)
        {
            return null;
        }
    }

    private static string ReadChatContent(JsonNode? content)
    {
        if (content is JsonValue value && value.TryGetValue<string>(out var text)) return text;
        if (content is not JsonArray blocks) return string.Empty;
        return string.Concat(blocks.OfType<JsonObject>()
                                   .Where(block => block["type"]?.GetValue<string>() is "text" or "output_text")
                                   .Select(block => block["text"]?.GetValue<string>() ?? string.Empty));
    }

    private static string Truncate(string value) => value.Length <= 8192 ? value : value[..8192];
}
