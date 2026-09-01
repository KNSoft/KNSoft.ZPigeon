using System.Text.Json.Serialization;

namespace KNSoft.ZPigeon.Agent;

public enum ModelProtocol
{
    OpenAIResponses = 1,
    OpenAIChatCompletions,
    AnthropicMessages
}

public enum ModelAuthentication
{
    ApiKey = 1,
    BearerToken,
    None,
    OAuth
}

public enum ReasoningEffort
{
    None,
    Minimal,
    Low,
    Medium,
    High,
    XHigh,
    Max
}

public enum SessionItemKind
{
    User = 1,
    Assistant,
    ToolCall,
    ToolResult,
    Compaction,
    Error
}

public enum SessionItemState
{
    Completed = 1,
    Queued,
    Running,
    Canceled,
    Failed
}

public enum MessageDisposition
{
    Queue = 1,
    Steer
}

public sealed record ModelConfiguration(
    Guid Id,
    string Name,
    string Provider,
    ModelProtocol Protocol,
    Uri BaseUrl,
    ModelAuthentication Authentication,
    [property: JsonIgnore] string Credential,
    string ModelId,
    int ContextWindow,
    int MaximumOutputTokens,
    ReasoningEffort Reasoning,
    int RequestTimeoutSeconds,
    string AdvancedJson);

public sealed record AgentDocument(string Name, string Content);

public sealed record AgentConfiguration(
    Guid Id,
    string Name,
    Guid ModelId,
    string SystemPrompt,
    string[] ToolNames,
    string AgentsMd,
    string ToolsMd,
    string MemoryMd,
    AgentDocument[] Documents);

public sealed record AgentSessionSummary(
    Guid Id,
    Guid AgentId,
    string AgentName,
    string Title,
    DateTimeOffset CreatedAt,
    DateTimeOffset UpdatedAt);

public sealed record AgentSession(
    Guid Id,
    Guid AgentId,
    string ClientFingerprint,
    string Title,
    DateTimeOffset CreatedAt,
    DateTimeOffset UpdatedAt);

public sealed record SessionItem(
    Guid SessionId,
    long Sequence,
    Guid RunId,
    int Step,
    SessionItemKind Kind,
    SessionItemState State,
    string? Name,
    string? CallId,
    string Content,
    long? RelatedSequence,
    ModelProtocol? Protocol,
    string? ProtocolJson,
    long? InputTokens,
    long? CachedInputTokens,
    long? OutputTokens,
    long? ReasoningTokens,
    long? TotalTokens,
    string? RawUsage,
    DateTimeOffset CreatedAt);

public sealed record TokenUsage(
    long? Input,
    long? CachedInput,
    long? Output,
    long? Reasoning,
    long? Total,
    string? RawJson)
{
    public static TokenUsage Empty { get; } = new(null, null, null, null, null, null);
}

public sealed record SessionUsage(
    long Input,
    long CachedInput,
    long Output,
    long Reasoning,
    long Total,
    long? LatestInput,
    int ContextWindow);

public sealed record SessionRuntimeState(bool Running, int Queued);

public sealed class SessionChangedEventArgs(Guid sessionId) : EventArgs
{
    public Guid SessionId { get; } = sessionId;
}

public interface ISecretProtector
{
    string Protect(string value);
    string Unprotect(string value);
}
