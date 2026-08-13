using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal static class RegistryWebApi
{
    internal static void MapRegistryApi(
        this WebApplication app,
        NativeServer server)
    {
        var registry = app.MapGroup("/api/registry");
        registry.MapPost("/keys", async (RegistryPageRequest request) =>
            await server.EnumerateRegistryKeysPageAsync(request.Root,
                                                        request.Path,
                                                        request.Cursor,
                                                        request.MaxEntries));
        registry.MapPost("/values", async (RegistryPageRequest request) =>
            await server.EnumerateRegistryValuesPageAsync(request.Root,
                                                          request.Path,
                                                          request.Cursor,
                                                          request.MaxEntries));
        registry.MapPost("/value/query", async (
            RegistryValueRequest request) =>
            await server.QueryRegistryValueAsync(request.Root,
                                                 request.Path,
                                                 request.Name));
        registry.MapPost("/value/set", async (
            RegistrySetValueRequest request) =>
            await server.SetRegistryValueAsync(request.Root,
                                               request.Path,
                                               request.Name,
                                               request.Type,
                                               request.Data));
        registry.MapPost("/value/delete", async (
            RegistryValueRequest request) =>
            await server.DeleteRegistryValueAsync(request.Root,
                                                  request.Path,
                                                  request.Name));
        registry.MapPost("/key/create", async (
            RegistryKeyRequest request) =>
            await server.CreateRegistryKeyAsync(request.Root,
                                                request.Path));
        registry.MapPost("/key/delete", async (
            RegistryKeyRequest request) =>
            await server.DeleteRegistryKeyAsync(request.Root,
                                                request.Path));
        registry.MapPost("/key/rename", async (
            RegistryRenameRequest request) =>
            await server.RenameRegistryKeyAsync(request.Root,
                                                request.Path,
                                                request.Name,
                                                request.NewName));
        registry.MapPost("/value/rename", async (
            RegistryRenameRequest request) =>
            await server.RenameRegistryValueAsync(request.Root,
                                                  request.Path,
                                                  request.Name,
                                                  request.NewName));
    }
}

internal sealed record RegistryPageRequest(
    RegistryRoot Root,
    string Path,
    string? Cursor,
    uint MaxEntries);
internal sealed record RegistryKeyRequest(
    RegistryRoot Root,
    string Path);
internal sealed record RegistryValueRequest(
    RegistryRoot Root,
    string Path,
    string Name);
internal sealed record RegistrySetValueRequest(
    RegistryRoot Root,
    string Path,
    string Name,
    uint Type,
    byte[] Data);
internal sealed record RegistryRenameRequest(
    RegistryRoot Root,
    string Path,
    string Name,
    string NewName);
