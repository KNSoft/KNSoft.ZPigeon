namespace KNSoft.ZPigeon.Web;

internal static class SoftwareWebApi
{
    internal static void MapSoftwareApi(this WebApplication app, ClientServicesRegistry services)
    {
        app.MapPost("/api/packages/staging", async (SoftwareStagingRequest request) =>
        {
            var name = Path.GetFileName(request.Name);
            if (string.IsNullOrEmpty(name) || name.Length > 128 || name.Contains('\0')) return Results.BadRequest();
            return Results.Ok(new { Path = await services.Current.SoftwareDeployments.CreateStagingAsync(name) });
        });
        app.MapPost("/api/packages/staging/delete", async (SoftwareStagingDeleteRequest request) =>
        {
            if (string.IsNullOrWhiteSpace(request.Path) || request.Path.Length > 32767)
            {
                return Results.BadRequest();
            }
            await services.Current.SoftwareDeployments.DeleteStagingAsync(request.Path);
            return Results.NoContent();
        });
        app.MapPost("/api/packages/install", async (SoftwarePackageInstallRequest request) =>
        {
            if (string.IsNullOrWhiteSpace(request.Path) || request.Path.Length > 32767 || request.Path.Contains('\0') ||
                string.IsNullOrWhiteSpace(request.Name) || request.Name.Length > 260 || request.Name.Contains('\0') ||
                request.Dependencies is null or { Length: > 32 } ||
                request.Dependencies.Any(path => string.IsNullOrWhiteSpace(path) || path.Length > 32767 ||
                                                 path.Contains('\0')) ||
                request.Dependencies.Distinct(StringComparer.OrdinalIgnoreCase).Count() !=
                    request.Dependencies.Length ||
                request.Dependencies.Contains(request.Path, StringComparer.OrdinalIgnoreCase))
            {
                return Results.BadRequest();
            }
            return Results.Ok(await services.Current.SoftwareDeployments.InstallPackageAsync(
                request.Path,
                request.Name,
                request.Dependencies));
        });
        app.MapPost("/api/packages/install-existing", async (SoftwareExistingPackageInstallRequest request) =>
        {
            if (string.IsNullOrWhiteSpace(request.Path) || request.Path.Length > 32767 || request.Path.Contains('\0') ||
                string.IsNullOrWhiteSpace(request.Name) || request.Name.Length > 260 || request.Name.Contains('\0'))
            {
                return Results.BadRequest();
            }
            return Results.Ok(await services.Current.SoftwareDeployments.InstallExistingPackageAsync(
                request.Path,
                request.Name));
        });
        app.MapPost("/api/packages/providers", async () =>
            Results.Ok(await services.Current.SoftwareDeployments.EnumeratePackageProvidersAsync()));
        app.MapPost("/api/packages/list", async (PackageProviderRequest request) =>
        {
            if (!TryNormalizeProvider(request.Provider, out var provider)) return Results.BadRequest();
            return Results.Ok(await services.Current.SoftwareDeployments.EnumeratePackagesAsync(provider));
        });
        app.MapPost("/api/software/windows-app/uninstall", async (WindowsAppUninstallRequest request) =>
        {
            if (string.IsNullOrWhiteSpace(request.Identity) || request.Identity.Length > 32767 ||
                request.Identity.Contains('\0') || string.IsNullOrWhiteSpace(request.Name) ||
                request.Name.Length > 260 || request.Name.Contains('\0'))
            {
                return Results.BadRequest();
            }
            return Results.Ok(await services.Current.SoftwareDeployments.UninstallWindowsAppAsync(
                request.Identity,
                request.Name));
        });
        app.MapPost("/api/packages/control", async (PackageControlRequest request) =>
        {
            if (!TryNormalizeProvider(request.Provider, out var provider) ||
                request.Action is not (SoftwareDeploymentAction.Install or SoftwareDeploymentAction.Upgrade or
                    SoftwareDeploymentAction.Uninstall or SoftwareDeploymentAction.UpgradeAll))
            {
                return Results.BadRequest();
            }
            if (!provider.Equals("winget", StringComparison.Ordinal))
            {
                var all = request.Action == SoftwareDeploymentAction.UpgradeAll;
                if (request.Source is not null ||
                    (all && provider is not ("npm" or "chocolatey")) ||
                    !TryNormalizeExternalIdentity(provider,
                                                  request.Action,
                                                  request.Identity,
                                                  out var externalIdentity) ||
                    !TryNormalizeVersion(provider, request.Version, out var version) ||
                    (request.Action == SoftwareDeploymentAction.Uninstall && version is not null) ||
                    (all && version is not null) ||
                    (request.Scope is not null &&
                     (provider != "pip" || request.Scope != "user" ||
                      request.Action == SoftwareDeploymentAction.Uninstall)))
                {
                    return Results.BadRequest();
                }
                return Results.Ok(await services.Current.SoftwareDeployments.ControlPackageAsync(
                    provider,
                    request.Action,
                    externalIdentity,
                    version,
                    request.Scope));
            }
            if (request.Version is not null) return Results.BadRequest();
            var sourceRequired = request.Action is SoftwareDeploymentAction.Install or SoftwareDeploymentAction.Upgrade;
            string? source = null;
            var sourceValid = sourceRequired ? TryNormalizeSource(request.Source, out source) : request.Source is null;
            if (!sourceValid ||
                (sourceRequired ? request.Scope is not (null or "user" or "machine") : request.Scope is not null) ||
                source?.Equals("msstore", StringComparison.OrdinalIgnoreCase) == true && request.Scope is not null ||
                !TryNormalizeIdentity(request.Action, request.Identity, source, out var identity))
            {
                return Results.BadRequest();
            }
            return Results.Ok(await services.Current.SoftwareDeployments.ControlWinGetAsync(
                request.Action,
                identity,
                source,
                request.Scope));
        });
        app.MapPost("/api/packages/jobs", () => services.Current.SoftwareDeployments.EnumerateJobsAsync());
    }

    private static bool TryNormalizeProvider(string? value, out string provider)
    {
        provider = value?.Trim().ToLowerInvariant() ?? string.Empty;
        return provider is "winget" or "pip" or "npm" or "chocolatey" or "dotnet";
    }

    private static bool TryNormalizeExternalIdentity(
        string provider,
        SoftwareDeploymentAction action,
        string? value,
        out string? identity)
    {
        identity = null;
        if (action == SoftwareDeploymentAction.UpgradeAll) return string.IsNullOrEmpty(value);
        value = value?.Trim();
        if (string.IsNullOrEmpty(value) || value.Length > 260 || value[0] == '-') return false;
        var valid = provider == "npm" ? IsNpmPackageName(value) : value.All(IsPackageNameCharacter);
        if (!valid) return false;
        identity = value;
        return true;
    }

    private static bool TryNormalizeVersion(string provider, string? value, out string? version)
    {
        version = value?.Trim();
        if (string.IsNullOrEmpty(version))
        {
            version = null;
            return true;
        }
        return version.Length <= 128 && version[0] != '-' &&
               version.All(character => IsPackageNameCharacter(character) || character == '+' ||
                                        provider == "pip" && character == '!');
    }

    private static bool IsNpmPackageName(string value)
    {
        var slash = value.IndexOf('/');
        if (value[0] != '@') return slash < 0 && value.All(IsPackageNameCharacter);
        return slash > 1 && slash < value.Length - 1 && value.IndexOf('/', slash + 1) < 0 &&
               IsPackageNamePart(value.AsSpan(1, slash - 1)) && IsPackageNamePart(value.AsSpan(slash + 1));
    }

    private static bool IsPackageNamePart(ReadOnlySpan<char> value)
    {
        foreach (var character in value)
        {
            if (!IsPackageNameCharacter(character)) return false;
        }
        return true;
    }

    private static bool IsPackageNameCharacter(char value) =>
        char.IsAsciiLetterOrDigit(value) || value is '.' or '_' or '-';

    private static bool TryNormalizeIdentity(
        SoftwareDeploymentAction action,
        string? value,
        string? source,
        out string? identity)
    {
        identity = null;
        if (action == SoftwareDeploymentAction.UpgradeAll) return string.IsNullOrEmpty(value);
        value = value?.Trim();
        if (string.IsNullOrEmpty(value) || value.Length > 260 || value.Contains('\0')) return false;
        if (source?.Equals("msstore", StringComparison.OrdinalIgnoreCase) == true)
        {
            if (Uri.TryCreate(value, UriKind.Absolute, out var uri))
            {
                if (uri.Scheme.Equals("https", StringComparison.OrdinalIgnoreCase) &&
                    uri.Host.Equals("apps.microsoft.com", StringComparison.OrdinalIgnoreCase))
                {
                    value = uri.Segments.Select(segment => segment.Trim('/'))
                        .LastOrDefault(segment => segment.Length != 0);
                }
                else if (uri.Scheme.Equals("ms-windows-store", StringComparison.OrdinalIgnoreCase))
                {
                    value = uri.Query.TrimStart('?').Split('&', StringSplitOptions.RemoveEmptyEntries)
                        .Select(part => part.Split('=', 2))
                        .FirstOrDefault(part => part.Length == 2 &&
                                                part[0].Equals("ProductId", StringComparison.OrdinalIgnoreCase))
                        ?.ElementAtOrDefault(1);
                    if (value is not null)
                    {
                        try
                        {
                            value = Uri.UnescapeDataString(value);
                        }
                        catch (UriFormatException)
                        {
                            return false;
                        }
                    }
                }
                else
                {
                    return false;
                }
            }
            if (value is not { Length: 12 } || !value.All(char.IsAsciiLetterOrDigit)) return false;
            value = value.ToUpperInvariant();
        }
        else if (!value.All(character => char.IsAsciiLetterOrDigit(character) || character is '.' or '_' or '+' or '-'))
        {
            return false;
        }
        identity = value;
        return true;
    }

    private static bool TryNormalizeSource(string? value, out string? source)
    {
        source = value?.Trim() ?? string.Empty;
        return source.Equals("winget", StringComparison.OrdinalIgnoreCase) ||
               source.Equals("msstore", StringComparison.OrdinalIgnoreCase);
    }
}

internal sealed record SoftwareStagingRequest(string? Name);
internal sealed record SoftwareStagingDeleteRequest(string? Path);
internal sealed record SoftwarePackageInstallRequest(string? Path, string? Name, string[]? Dependencies);
internal sealed record SoftwareExistingPackageInstallRequest(string? Path, string? Name);
internal sealed record WindowsAppUninstallRequest(string? Identity, string? Name);
internal sealed record PackageProviderRequest(string? Provider);
internal sealed record PackageControlRequest(
    string? Provider,
    SoftwareDeploymentAction Action,
    string? Identity,
    string? Version,
    string? Source,
    string? Scope);
