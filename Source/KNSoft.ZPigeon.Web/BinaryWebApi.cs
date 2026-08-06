using System.Text.Json;

namespace KNSoft.ZPigeon.Web;

internal static class BinaryWebApi
{
    private const int MaximumMetadataLength = 0x10000;
    private static readonly JsonSerializerOptions JsonOptions = new(JsonSerializerDefaults.Web);

    internal static async Task<(T Metadata, byte[] Data)> ReadAsync<T>(
        HttpRequest request,
        int maximumDataLength)
    {
        if (!request.HasFormContentType)
        {
            throw new BadHttpRequestException("Expected multipart form data.");
        }
        var form = await request.ReadFormAsync(request.HttpContext.RequestAborted);
        var metadataValue = form["metadata"];
        if (metadataValue.Count != 1 || metadataValue[0] is not { Length: > 0 } metadataText ||
            metadataText.Length > MaximumMetadataLength || form.Files.Count != 1)
        {
            throw new BadHttpRequestException("Invalid binary request.");
        }
        var file = form.Files[0];
        if (file.Name != "data" || file.Length < 0 || file.Length > maximumDataLength)
        {
            throw new BadHttpRequestException("Invalid binary request data.");
        }
        var metadata = JsonSerializer.Deserialize<T>(metadataText, JsonOptions) ??
                       throw new BadHttpRequestException("Invalid binary request metadata.");
        var data = GC.AllocateUninitializedArray<byte>(checked((int)file.Length));
        await using var stream = file.OpenReadStream();
        await stream.ReadExactlyAsync(data, request.HttpContext.RequestAborted);
        return (metadata, data);
    }
}
