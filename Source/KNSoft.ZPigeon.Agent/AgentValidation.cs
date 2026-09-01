namespace KNSoft.ZPigeon.Agent;

public static class AgentValidation
{
    public static void ValidateModel(ModelConfiguration value)
    {
        ArgumentNullException.ThrowIfNull(value);
        ValidateId(value.Id, nameof(value));
        ValidateText(value.Name, 128, nameof(value.Name), true);
        ValidateText(value.Provider, 128, nameof(value.Provider), true);
        ValidateText(value.ModelId, 256, nameof(value.ModelId), true);
        if (!Enum.IsDefined(value.Protocol) || !Enum.IsDefined(value.Authentication) ||
            !Enum.IsDefined(value.Reasoning) ||
            !value.BaseUrl.IsAbsoluteUri ||
            value.BaseUrl.AbsoluteUri.Length > 2048 ||
            value.BaseUrl.Scheme != Uri.UriSchemeHttp && value.BaseUrl.Scheme != Uri.UriSchemeHttps ||
            !string.IsNullOrEmpty(value.BaseUrl.UserInfo) ||
            value.ContextWindow is < 1 or > 100_000_000 ||
            value.MaximumOutputTokens is < 1 or > 10_000_000 ||
            value.MaximumOutputTokens > value.ContextWindow ||
            value.RequestTimeoutSeconds is < 1 or > 600 ||
            value.AdvancedJson.Length > 65536)
        {
            throw new ArgumentException("The model configuration is invalid.", nameof(value));
        }
        if (value.Authentication == ModelAuthentication.None)
        {
            if (value.Credential.Length != 0)
            {
                throw new ArgumentException("An unauthenticated model cannot have a credential.", nameof(value));
            }
        }
        else
        {
            ValidateText(value.Credential, 32768, nameof(value.Credential), true);
        }
        ModelProtocolClient.ValidateAdvanced(value.Protocol, value.AdvancedJson);
    }

    public static void ValidateAgent(AgentConfiguration value, IReadOnlySet<string> knownTools)
    {
        ArgumentNullException.ThrowIfNull(value);
        ArgumentNullException.ThrowIfNull(knownTools);
        ValidateId(value.Id, nameof(value));
        ValidateId(value.ModelId, nameof(value.ModelId));
        ValidateText(value.Name, 128, nameof(value.Name), true);
        ValidateText(value.SystemPrompt, 65536, nameof(value.SystemPrompt), false);
        ValidateText(value.AgentsMd, 262144, nameof(value.AgentsMd), false);
        ValidateText(value.ToolsMd, 262144, nameof(value.ToolsMd), false);
        ValidateText(value.MemoryMd, 262144, nameof(value.MemoryMd), false);
        if (value.ToolNames.Length > knownTools.Count ||
            value.ToolNames.Distinct(StringComparer.Ordinal).Count() != value.ToolNames.Length ||
            value.ToolNames.Any(name => !knownTools.Contains(name)) ||
            value.Documents.Length > 16)
        {
            throw new ArgumentException("The agent configuration is invalid.", nameof(value));
        }
        var names = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var document in value.Documents)
        {
            if (document is null)
            {
                throw new ArgumentException("The agent configuration is invalid.", nameof(value));
            }
            ValidateText(document.Name, 128, nameof(value.Documents), true);
            ValidateText(document.Content, 262144, nameof(value.Documents), false);
            if (!names.Add(document.Name))
            {
                throw new ArgumentException("Custom document names must be unique.", nameof(value));
            }
        }
    }

    public static string ValidateTitle(string value)
    {
        value = value?.Trim() ?? throw new ArgumentNullException(nameof(value));
        ValidateText(value, 128, nameof(value), true);
        return value;
    }

    public static string ValidateMessage(string value)
    {
        value = value?.Trim() ?? throw new ArgumentNullException(nameof(value));
        ValidateText(value, 65536, nameof(value), true);
        return value;
    }

    private static void ValidateText(string value, int maximumLength, string name, bool required)
    {
        ArgumentNullException.ThrowIfNull(value, name);
        if (value.Length > maximumLength || value.Contains('\0') || required && string.IsNullOrWhiteSpace(value))
        {
            throw new ArgumentException(null, name);
        }
    }

    private static void ValidateId(Guid value, string name)
    {
        if (value == Guid.Empty) throw new ArgumentOutOfRangeException(name);
    }
}
