using KNSoft.ZPigeon.Server.Managed;

namespace KNSoft.ZPigeon.Web;

internal static class RecordingWebApi
{
    internal static void MapRecordingApi(this WebApplication app, NativeServer server)
    {
        app.MapPost("/api/recording/capabilities", async () => new
        {
            Codecs = await server.QueryRecordingCapabilitiesAsync()
        });
        app.MapPost("/api/recording/start", async (RecordingStartRequest request) =>
        {
            if (!ulong.TryParse(request.WindowHandle, out var windowHandle)) return Results.BadRequest();
            if (!Enum.IsDefined(request.Source) || !Enum.IsDefined(request.Codec) ||
                !Enum.IsDefined(request.AudioSource) ||
                request.FrameRate is > 120 || request.MaxDimension is > 7680 ||
                request.VideoBitRate is > 100_000_000 || request.AudioBitRate is > 1_000_000 ||
                request.SourceId?.Length > 1024 || request.AudioDeviceId?.Length > 1024)
            {
                return Results.BadRequest();
            }
            if ((request.Flags & ~RecordingFlags.CaptureCursor) != 0)
            {
                return Results.BadRequest();
            }
            var result = await server.StartRecordingAsync(new RecordingOptions(
                request.Source,
                request.Codec,
                request.FrameRate,
                request.AudioSource,
                request.Flags,
                request.MaxDimension,
                request.VideoBitRate,
                request.AudioBitRate,
                windowHandle,
                request.SourceId,
                request.AudioDeviceId));
            return Results.Ok(ToWebRecord(result));
        });
        app.MapPost("/api/recording/list", async () =>
            (await server.EnumerateRecordingsAsync()).Select(ToWebRecord));
        app.MapPost("/api/recording/stop", async (RecordingIdRequest request) =>
            await server.StopRecordingAsync(request.RecordingId));
        app.MapPost("/api/recording/delete", async (RecordingIdRequest request) =>
            await server.DeleteRecordingAsync(request.RecordingId));
    }

    private static object ToWebRecord(RecordingRecord record) => new
    {
        record.RecordingId,
        record.Source,
        record.Codec,
        record.State,
        record.Status,
        StartTime = record.StartTime.ToString(),
        Duration = record.Duration.ToString(),
        FileSize = record.FileSize.ToString(),
        record.Path
    };
}

internal sealed record RecordingStartRequest(
    RecordingSource Source,
    RecordingCodec Codec,
    ushort FrameRate,
    RecordingAudioSource AudioSource,
    RecordingFlags Flags,
    uint MaxDimension,
    uint VideoBitRate,
    uint AudioBitRate,
    string WindowHandle,
    string? SourceId,
    string? AudioDeviceId);

internal sealed record RecordingIdRequest(uint RecordingId);
