using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Application;

public sealed partial class ZPigeonApplication
{
    public Task<WmiRow[]> QueryWmiAsync(
        ulong clientId,
        string wmiNamespace,
        string query,
        uint limit,
        bool includeSystemProperties,
        CancellationToken cancellationToken = default)
    {
        ValidateRequiredText(wmiNamespace, 512, nameof(wmiNamespace));
        ValidateRequiredText(query, 8192, nameof(query));
        if (limit is 0 or > MaximumResultCount) throw new ArgumentOutOfRangeException(nameof(limit));
        return RunAsync(clientId,
                        () => server.QueryWmiAsync(wmiNamespace,
                                                   query,
                                                   limit,
                                                   includeSystemProperties),
                        cancellationToken);
    }

    public Task<RegistryPage<RegistryKeyRecord>> GetRegistryKeysAsync(
        ulong clientId,
        RegistryRoot root,
        string path,
        string? cursor,
        uint limit,
        CancellationToken cancellationToken = default)
    {
        ValidateRegistryRequest(root, path, cursor, limit);
        return RunAsync(clientId,
                        () => server.EnumerateRegistryKeysPageAsync(root, path, cursor, limit),
                        cancellationToken);
    }

    public Task<RegistryPage<RegistryValueRecord>> GetRegistryValuesAsync(
        ulong clientId,
        RegistryRoot root,
        string path,
        string? cursor,
        uint limit,
        CancellationToken cancellationToken = default)
    {
        ValidateRegistryRequest(root, path, cursor, limit);
        return RunAsync(clientId,
                        () => server.EnumerateRegistryValuesPageAsync(root, path, cursor, limit),
                        cancellationToken);
    }

    public Task<RegistryValue> GetRegistryValueAsync(
        ulong clientId,
        RegistryRoot root,
        string path,
        string name,
        CancellationToken cancellationToken = default)
    {
        ValidateRegistryRequest(root, path, null, 1);
        ValidateOptionalText(name, 16383, nameof(name));
        return RunAsync(clientId,
                        () => server.QueryRegistryValueAsync(root, path, name),
                        cancellationToken);
    }

    public Task<BrowserPage> GetBrowsersAsync(
        ulong clientId,
        CancellationToken cancellationToken = default) =>
        RunAsync(clientId, server.EnumerateBrowsersAsync, cancellationToken);

    public Task<BrowserPage> QueryBrowserAsync(
        ulong clientId,
        BrowserType browser,
        BrowserKind kind,
        string profile,
        string? userData,
        ulong cursor,
        uint limit,
        CancellationToken cancellationToken = default)
    {
        if (browser is not (BrowserType.Chrome or BrowserType.Edge))
        {
            throw new ArgumentOutOfRangeException(nameof(browser));
        }
        if (kind is not (BrowserKind.History or
                         BrowserKind.Download or
                         BrowserKind.Bookmark or
                         BrowserKind.Setting or
                         BrowserKind.Extension or
                         BrowserKind.Cookie or
                         BrowserKind.Password))
        {
            throw new ArgumentOutOfRangeException(nameof(kind));
        }
        ValidateRequiredText(profile, 32767, nameof(profile));
        ValidateOptionalText(userData, 32767, nameof(userData));
        if (limit is 0 or > MaximumResultCount) throw new ArgumentOutOfRangeException(nameof(limit));
        return RunAsync(clientId,
                        () => server.QueryBrowserAsync(browser,
                                                       kind,
                                                       profile,
                                                       userData,
                                                       cursor,
                                                       limit),
                        cancellationToken);
    }

    private static void ValidateRegistryRequest(
        RegistryRoot root,
        string path,
        string? cursor,
        uint limit)
    {
        if (root is not (RegistryRoot.ClassesRoot or
                         RegistryRoot.CurrentUser or
                         RegistryRoot.LocalMachine or
                         RegistryRoot.Users or
                         RegistryRoot.CurrentConfig))
        {
            throw new ArgumentOutOfRangeException(nameof(root));
        }
        ValidateOptionalText(path, 16383, nameof(path));
        ValidateOptionalText(cursor, 32767, nameof(cursor));
        if (limit is 0 or > MaximumResultCount) throw new ArgumentOutOfRangeException(nameof(limit));
    }
}
