using KNSoft.ZPigeon.Server.Managed;
using System.Buffers.Binary;
using System.Globalization;
using System.Text;
using System.Text.Json;

namespace KNSoft.ZPigeon.Web;

internal static class RegistryWebApi
{
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    internal static void MapRegistryApi(
        this WebApplication app,
        ClientServicesRegistry services)
    {
        var server = services.Server;
        var registry = app.MapGroup("/api/registry");
        registry.MapPost("/keys", (RegistryPageRequest request) =>
            server.EnumerateRegistryKeysPageAsync(request.Root,
                                                  request.Path,
                                                  request.Cursor,
                                                  request.MaxEntries));
        registry.MapPost("/values", async (HttpContext context, RegistryPageRequest request) =>
        {
            var page = await server.EnumerateRegistryValuesPageAsync(request.Root,
                                                                     request.Path,
                                                                     request.Cursor,
                                                                     request.MaxEntries);
            var metadata = JsonSerializer.SerializeToUtf8Bytes(new
            {
                page.HasMore,
                page.NextCursor,
                Records = page.Records.Select(value => new
                {
                    value.Name,
                    value.Type,
                    value.DataLength,
                    PreviewLength = value.Preview.Length
                })
            }, JsonOptions);
            var length = 4L + metadata.Length + page.Records.Sum(value => (long)value.Preview.Length);
            var header = new byte[4];
            BinaryPrimitives.WriteInt32LittleEndian(header, metadata.Length);
            context.Response.ContentLength = length;
            context.Response.ContentType = "application/vnd.zpigeon.records";
            await context.Response.Body.WriteAsync(header, context.RequestAborted);
            await context.Response.Body.WriteAsync(metadata, context.RequestAborted);
            foreach (var value in page.Records)
                await context.Response.Body.WriteAsync(value.Preview, context.RequestAborted);
        });
        registry.MapPost("/value/query", async (HttpContext context, RegistryValueRequest request) =>
        {
            var value = await server.QueryRegistryValueAsync(request.Root, request.Path, request.Name);
            context.Response.Headers["X-ZPigeon-Type"] = value.Type.ToString(CultureInfo.InvariantCulture);
            context.Response.Headers["X-ZPigeon-Size"] = value.Data.Length.ToString(CultureInfo.InvariantCulture);
            return Results.Bytes(value.Data, "application/octet-stream");
        });
        registry.MapPost("/value/range", async (HttpContext context, RegistryRangeRequest request) =>
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
            context.Response.Headers["X-ZPigeon-Size"] = range.TotalLength.ToString(CultureInfo.InvariantCulture);
            return Results.Bytes(range.Data, "application/octet-stream");
        });
        registry.MapPost("/value/range/write", async (HttpRequest request) =>
        {
            var (metadata, data) = await BinaryWebApi.ReadAsync<RegistryRangeWriteRequest>(request, 0x10000);
            if (!uint.TryParse(metadata.Offset, out var offset) || data.Length == 0)
            {
                return Results.BadRequest();
            }
            await server.WriteRegistryValueRangeAsync(
                metadata.Root,
                metadata.Path,
                metadata.Name,
                offset,
                data);
            return Results.NoContent();
        });
        registry.MapPost("/value/set", async (HttpRequest request) =>
        {
            var (metadata, data) = await BinaryWebApi.ReadAsync<RegistrySetValueRequest>(request, 0x1000000);
            await server.SetRegistryValueAsync(metadata.Root,
                                               metadata.Path,
                                               metadata.Name,
                                               metadata.Type,
                                               data);
            return Results.NoContent();
        });
        registry.MapPost("/value/delete", (
            RegistryValueRequest request) =>
            server.DeleteRegistryValueAsync(request.Root,
                                            request.Path,
                                            request.Name));
        registry.MapPost("/key/create", (
            RegistryKeyRequest request) =>
            server.CreateRegistryKeyAsync(request.Root,
                                          request.Path));
        registry.MapPost("/key/delete", (
            RegistryKeyRequest request) =>
            server.DeleteRegistryKeyAsync(request.Root,
                                          request.Path));
        registry.MapPost("/key/rename", (
            RegistryRenameRequest request) =>
            server.RenameRegistryKeyAsync(request.Root,
                                          request.Path,
                                          request.Name,
                                          request.NewName));
        registry.MapPost("/value/rename", (
            RegistryRenameRequest request) =>
            server.RenameRegistryValueAsync(request.Root,
                                            request.Path,
                                            request.Name,
                                            request.NewName));
        registry.MapPost("/key/security", (RegistryKeyRequest request) =>
            server.QueryRegistrySecurityAsync(request.Root, request.Path));
        registry.MapPost("/key/security/set", (RegistrySecurityRequest request) =>
            server.SetRegistrySecurityAsync(request.Root,
                                            request.Path,
                                            request.Sddl,
                                            request.DaclProtected));
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
    string Offset);
internal sealed record RegistrySetValueRequest(
    RegistryRoot Root,
    string Path,
    string Name,
    uint Type);
internal sealed record RegistryRenameRequest(
    RegistryRoot Root,
    string Path,
    string Name,
    string NewName);
internal sealed record RegistrySecurityRequest(
    RegistryRoot Root,
    string Path,
    string Sddl,
    bool DaclProtected);
