using System.Text.Json;

namespace KNSoft.ZPigeon.Web;

internal sealed class ConsoleStore
{
    private readonly Lock sync = new();
    private readonly string path;
    private readonly ConsoleConfiguration configuration;

    internal ConsoleStore(string path)
    {
        this.path = path;
        configuration = File.Exists(path) ?
            JsonSerializer.Deserialize<ConsoleConfiguration>(File.ReadAllBytes(path), JsonSerializerOptions.Web)! :
            new([], []);
    }

    internal string[] GetGroups()
    {
        lock (sync) return configuration.Groups.Order(StringComparer.Ordinal).ToArray();
    }

    internal string GetGroup(string fingerprint, string defaultGroup)
    {
        lock (sync) return configuration.ClientGroups.GetValueOrDefault(fingerprint.ToUpperInvariant(), defaultGroup);
    }

    internal void AddGroup(string group)
    {
        group = ValidateGroup(group);
        if (group.Length == 0) throw new ArgumentException("A group name is required.");
        lock (sync)
        {
            if (!configuration.Groups.Contains(group, StringComparer.Ordinal))
            {
                configuration.Groups.Add(group);
                Save();
            }
        }
    }

    internal void SetGroup(string fingerprint, string group)
    {
        if (fingerprint.Length != 64 || fingerprint.Any(character => !Uri.IsHexDigit(character)))
            throw new ArgumentException("Invalid Client fingerprint.");
        group = ValidateGroup(group);
        lock (sync)
        {
            if (group.Length != 0 && !configuration.Groups.Contains(group, StringComparer.Ordinal))
                configuration.Groups.Add(group);
            configuration.ClientGroups[fingerprint.ToUpperInvariant()] = group;
            Save();
        }
    }

    private void Save()
    {
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        var temporary = path + ".tmp";
        File.WriteAllBytes(temporary, JsonSerializer.SerializeToUtf8Bytes(configuration, JsonSerializerOptions.Web));
        File.Move(temporary, path, true);
    }

    private static string ValidateGroup(string group)
    {
        ArgumentNullException.ThrowIfNull(group);
        group = group.Trim();
        if (group.Length > 64 || group.Any(char.IsControl))
            throw new ArgumentException("Group names must contain at most 64 characters and no control characters.");
        return group;
    }
}

internal sealed record ConsoleConfiguration(List<string> Groups, Dictionary<string, string> ClientGroups);
internal sealed record ConsoleGroupRequest(string Group);
