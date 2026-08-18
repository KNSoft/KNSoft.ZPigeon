using KNSoft.ZPigeon.Server.Managed;
using System.Text;

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
        registry.MapPost("/value/range", async (RegistryRangeRequest request) =>
        {
            if (!uint.TryParse(request.Offset, out var offset) || request.Length is 0 or > 0x10000)
            {
                return Results.BadRequest();
            }
            var range = await server.QueryRegistryValueRangeAsync(
                request.Root,
                request.Path,
                request.Name,
                offset,
                request.Length);
            return Results.Ok(new
            {
                Size = range.TotalLength.ToString(),
                Offset = request.Offset,
                range.Data
            });
        });
        registry.MapPost("/value/range/write", async (RegistryRangeWriteRequest request) =>
        {
            if (!uint.TryParse(request.Offset, out var offset) || request.Data.Length is 0 or > 0x10000)
            {
                return Results.BadRequest();
            }
            await server.WriteRegistryValueRangeAsync(
                request.Root,
                request.Path,
                request.Name,
                offset,
                request.Data);
            return Results.NoContent();
        });
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
        registry.MapPost("/key/security", async (RegistryKeyRequest request) =>
        {
            var value = await server.QueryRegistrySecurityAsync(request.Root, request.Path);
            if (value.Type != 1 || value.Data.Length % sizeof(char) != 0)
            {
                throw new InvalidDataException("The remote registry security descriptor is invalid.");
            }
            return new { Sddl = Encoding.Unicode.GetString(value.Data) };
        });
        registry.MapPost("/key/security/set", async (RegistrySecurityRequest request) =>
            await server.SetRegistrySecurityAsync(request.Root, request.Path, request.Sddl));
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
internal sealed record RegistryRangeRequest(
    RegistryRoot Root,
    string Path,
    string Name,
    string Offset,
    uint Length);
internal sealed record RegistryRangeWriteRequest(
    RegistryRoot Root,
    string Path,
    string Name,
    string Offset,
    byte[] Data);
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
internal sealed record RegistrySecurityRequest(
    RegistryRoot Root,
    string Path,
    string Sddl);
