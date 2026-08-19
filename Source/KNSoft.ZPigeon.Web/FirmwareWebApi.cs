using KNSoft.FirmwareSpec;
using KNSoft.ZPigeon.Server.Managed;
using System.Buffers.Binary;
using System.Globalization;
using System.Text;

namespace KNSoft.ZPigeon.Web;

internal static class FirmwareWebApi
{
    private const uint MaximumRangeLength = 0x10000;
    private const int MaximumVariableLength = 0x100000;

    internal static void MapFirmwareApi(this WebApplication app, NativeServer server)
    {
        app.MapPost("/api/firmware/variables", async () =>
            await server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateFirmwareVariables));
        app.MapPost("/api/firmware/cpuid", async () =>
        {
            var records = await server.QueryAdministrationAsync(AdministrationOperation.QueryFirmware, "cpuid");
            if (!TryGetSingleData(records, AdministrationKind.CpuidSnapshot, out var data))
            {
                return InvalidData("CPUID 响应格式无效");
            }
            var status = CpuidParser.TryParse(data, out var snapshot);
            if (status != FirmwareDecodeStatus.Success || snapshot is null)
            {
                return InvalidData($"CPUID 解析失败：{status}");
            }
            return Results.Ok(new CpuidWebRecord(
                snapshot.VendorId,
                GetProcessorName(snapshot),
                snapshot.Records.ToArray(),
                CpuidFeatures.All.Where(snapshot.IsSupported).Select(feature => feature.Name).ToArray()));
        });
        app.MapPost("/api/firmware/smbios", async () =>
        {
            var records = await server.QueryAdministrationAsync(AdministrationOperation.QueryFirmware, "smbios");
            if (!TryGetSingleData(records, AdministrationKind.SmbiosTable, out var data))
            {
                return InvalidData("SMBIOS 响应格式无效");
            }
            var status = SmbiosParser.TryParseWindowsRaw(data, out var table);
            if (status != FirmwareDecodeStatus.Success || table is null)
            {
                return InvalidData($"SMBIOS 解析失败：{status}");
            }
            return Results.Ok(new SmbiosWebRecord(
                table.Version.Major,
                table.Version.Minor,
                table.Version.DmiRevision,
                table.Structures.Select(structure => new SmbiosStructureWebRecord(
                    structure.Type,
                    structure.TypeName,
                    structure.Handle,
                    structure.Length,
                    structure.Data.Length,
                    structure.Offset,
                    structure.Fields.Select(field =>
                        new FirmwareFieldWebRecord(field.Name, FormatSmbiosField(structure, field))).ToArray()))
                    .ToArray()));
        });
        app.MapPost("/api/firmware/acpi", async () =>
        {
            var records = await server.QueryAdministrationAsync(AdministrationOperation.QueryFirmware, "acpi");
            if (records.Any(record => record.Kind != AdministrationKind.AcpiTable))
            {
                return InvalidData("ACPI 响应格式无效");
            }
            var result = new AcpiTableWebRecord[records.Length];
            for (var index = 0; index < records.Length; index++)
            {
                var record = records[index];
                if (!TryDecodeData(record, out var data))
                {
                    result[index] = InvalidAcpi(record.Identity, FirmwareDecodeStatus.InvalidArgument);
                    continue;
                }
                var status = AcpiParser.TryParse(data, out var table);
                if (status != FirmwareDecodeStatus.Success || table is null)
                {
                    result[index] = InvalidAcpi(record.Identity, status);
                    continue;
                }
                var header = table.Header;
                result[index] = new AcpiTableWebRecord(
                    record.Identity,
                    header.Signature,
                    table.Description,
                    header.OemId,
                    header.OemTableId,
                    header.Revision,
                    header.Length,
                    null,
                    table.Fields.Select(field => new FirmwareFieldWebRecord(
                        field.Name,
                        table.TryReadUnsigned(field.Offset, field.Size, out var value) ? FormatUnsigned(value) : "—"))
                        .ToArray());
            }
            return Results.Ok(result);
        });
        app.MapPost("/api/firmware/data", async (HttpContext context, FirmwareDataRequest request) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            if (!ulong.TryParse(request.Offset, out var offset) || request.Length is 0 or > MaximumRangeLength)
            {
                return Results.BadRequest();
            }
            var records = await server.QueryAdministrationAsync(
                AdministrationOperation.QueryFirmware,
                request.Identity);
            if (records.Length != 1 || !TryDecodeData(records[0], out var data) || offset > (ulong)data.Length)
            {
                return Results.BadRequest();
            }
            var length = Math.Min(request.Length, (uint)(data.Length - (int)offset));
            return Results.Ok(new
            {
                Size = data.Length.ToString(CultureInfo.InvariantCulture),
                Data = data.AsSpan((int)offset, (int)length).ToArray()
            });
        });
        app.MapPost("/api/firmware/variable/write", async (FirmwareVariableWriteRequest request) =>
        {
            if (request.Action is not (AdministrationAction.Create or AdministrationAction.Configure or
                    AdministrationAction.Delete) || request.Data.Length > MaximumVariableLength)
            {
                return Results.BadRequest();
            }
            string identity;
            if (request.Action == AdministrationAction.Create)
            {
                if (string.IsNullOrEmpty(request.Name) || request.Name.Contains('\0') ||
                    !Guid.TryParse(request.VendorGuid, out var vendorGuid))
                {
                    return Results.BadRequest();
                }
                identity = $"uefi:{{{vendorGuid:D}}}:{request.Name}";
            }
            else
            {
                if (string.IsNullOrEmpty(request.Identity) ||
                    !request.Identity.StartsWith("uefi:", StringComparison.Ordinal))
                {
                    return Results.BadRequest();
                }
                identity = request.Identity;
            }
            await server.ControlAdministrationAsync(
                AdministrationOperation.ControlFirmware,
                request.Action,
                identity,
                request.Attributes.ToString(CultureInfo.InvariantCulture),
                Convert.ToBase64String(request.Data));
            return Results.NoContent();
        });
        app.MapPost("/api/firmware/variable/range/write", async (FirmwareVariableRangeWriteRequest request) =>
        {
            if (!request.Identity.StartsWith("uefi:", StringComparison.Ordinal) ||
                !ulong.TryParse(request.Offset, out var offset) ||
                request.Data.Length is 0 || (uint)request.Data.Length > MaximumRangeLength)
            {
                return Results.BadRequest();
            }
            var records = await server.QueryAdministrationAsync(
                AdministrationOperation.QueryFirmware,
                request.Identity);
            if (records.Length != 1 || !TryDecodeData(records[0], out var data) ||
                offset > (ulong)data.Length || request.Data.Length > data.Length - (int)offset)
            {
                return Results.BadRequest();
            }
            request.Data.CopyTo(data.AsSpan((int)offset));
            await server.ControlAdministrationAsync(
                AdministrationOperation.ControlFirmware,
                AdministrationAction.Configure,
                request.Identity,
                request.Attributes.ToString(CultureInfo.InvariantCulture),
                Convert.ToBase64String(data));
            return Results.NoContent();
        });
    }

    private static bool TryGetSingleData(
        AdministrationRecord[] records,
        AdministrationKind kind,
        out byte[] data)
    {
        if (records.Length == 1 && records[0].Kind == kind)
        {
            return TryDecodeData(records[0], out data);
        }
        data = [];
        return false;
    }

    private static bool TryDecodeData(AdministrationRecord record, out byte[] data)
    {
        try
        {
            data = Convert.FromBase64String(record.Detail);
            return data.Length.ToString(CultureInfo.InvariantCulture) == record.Value;
        }
        catch (FormatException)
        {
            data = [];
            return false;
        }
    }

    private static IResult InvalidData(string message) => Results.Problem(
        detail: message,
        statusCode: StatusCodes.Status502BadGateway);

    private static AcpiTableWebRecord InvalidAcpi(string identity, FirmwareDecodeStatus status)
    {
        var signature = identity.StartsWith("acpi:", StringComparison.Ordinal) && identity.Length > "acpi:".Length
            ? identity["acpi:".Length..]
            : identity;
        return new(identity, signature, "—", "—", "—", 0, 0, status.ToString(), []);
    }

    private static string GetProcessorName(CpuidSnapshot snapshot)
    {
        Span<byte> name = stackalloc byte[48];
        for (var index = 0; index < 3; index++)
        {
            if (!snapshot.TryGetRecord(0x80000002U + (uint)index, 0, out var record))
            {
                return string.Empty;
            }
            var destination = name[(index * 16)..];
            BinaryPrimitives.WriteUInt32LittleEndian(destination, record.Eax);
            BinaryPrimitives.WriteUInt32LittleEndian(destination[4..], record.Ebx);
            BinaryPrimitives.WriteUInt32LittleEndian(destination[8..], record.Ecx);
            BinaryPrimitives.WriteUInt32LittleEndian(destination[12..], record.Edx);
        }
        return Encoding.ASCII.GetString(name).Trim('\0', ' ');
    }

    private static string FormatSmbiosField(SmbiosStructure structure, SmbiosFieldInfo field)
    {
        if (field.DataType == SmbiosDataType.StringIndex && structure.TryGetString(field, out var text))
        {
            return string.IsNullOrEmpty(text) ? "—" : text;
        }
        if (structure.TryReadUnsigned(field, out var value))
        {
            var name = field.DataType == SmbiosDataType.Enum ? field.GetEnumName(value) : null;
            return name is null ? FormatUnsigned(value) : $"{name} (0x{value:X})";
        }
        if (field.Offset < 0 || field.Size < 0 || field.Offset > structure.Formatted.Length ||
            field.Size > structure.Formatted.Length - field.Offset)
        {
            return "—";
        }
        var data = structure.Formatted.Span.Slice(field.Offset, field.Size);
        return field.DataType == SmbiosDataType.Uuid && data.Length == 16 ?
            new Guid(data).ToString("D") :
            Convert.ToHexString(data);
    }

    private static string FormatUnsigned(ulong value) => $"0x{value:X} ({value})";
}

internal sealed record CpuidWebRecord(
    string VendorId,
    string ProcessorName,
    CpuidRecord[] Records,
    string[] Features);

internal sealed record SmbiosWebRecord(
    byte MajorVersion,
    byte MinorVersion,
    byte DmiRevision,
    SmbiosStructureWebRecord[] Structures);

internal sealed record SmbiosStructureWebRecord(
    byte Type,
    string Name,
    ushort Handle,
    byte FormattedLength,
    int TotalLength,
    int Offset,
    FirmwareFieldWebRecord[] Fields);

internal sealed record AcpiTableWebRecord(
    string Identity,
    string Signature,
    string Description,
    string OemId,
    string OemTableId,
    byte Revision,
    uint Length,
    string? Error,
    FirmwareFieldWebRecord[] Fields);

internal sealed record FirmwareFieldWebRecord(string Name, string Value);
internal sealed record FirmwareDataRequest(string Identity, string Offset, uint Length);
internal sealed record FirmwareVariableWriteRequest(
    AdministrationAction Action,
    string? Identity,
    string? Name,
    string? VendorGuid,
    uint Attributes,
    byte[] Data);
internal sealed record FirmwareVariableRangeWriteRequest(
    string Identity,
    uint Attributes,
    string Offset,
    byte[] Data);
