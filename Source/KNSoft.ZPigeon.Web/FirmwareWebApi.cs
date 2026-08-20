using KNSoft.FirmwareSpec;
using KNSoft.ZPigeon.Server.Managed;
using System.Buffers.Binary;
using System.Globalization;
using System.Text;

namespace KNSoft.ZPigeon.Web;

internal static class FirmwareWebApi
{
    private const uint MaximumFirmwareDataLength = 16 * 1024 * 1024;
    private const uint MaximumRangeLength = 0x10000;
    private const int MaximumVariableLength = 0x100000;

    internal static void MapFirmwareApi(this WebApplication app, NativeServer server)
    {
        app.MapPost("/api/firmware/bios", async () =>
            (await server.EnumerateAdministrationAsync(AdministrationOperation.EnumerateSystem))
                .Where(record => record.Identity is "firmware" or "secureBoot" || record.Description == "固件"));
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
                CpuidFeatures.All.Where(snapshot.IsSupported).Select(feature => new CpuidFeatureWebRecord(
                    feature.Leaf,
                    feature.SubLeaf,
                    feature.Register.ToString(),
                    feature.Bit,
                    feature.Name)).ToArray()));
        });
        app.MapPost("/api/firmware/smbios", async () =>
        {
            var records = await server.QueryAdministrationAsync(AdministrationOperation.QueryFirmware, "smbios");
            if (records.Any(record => record.Kind != AdministrationKind.SmbiosTable))
            {
                return InvalidData("SMBIOS 响应格式无效");
            }
            var structures = new SmbiosStructureWebRecord[records.Length];
            for (var index = 0; index < records.Length; index++)
            {
                var record = records[index];
                var offset = ParseTableIdentity(record.Identity, "smbios:");
                if (offset is null ||
                    !ulong.TryParse(record.Value, out var value) ||
                    record.Flags > ushort.MaxValue || (value >> 32) > byte.MaxValue ||
                    (uint)value is < sizeof(uint) or > MaximumFirmwareDataLength ||
                    (value >> 32) is < sizeof(uint) || (value >> 32) > (uint)value ||
                    offset > MaximumFirmwareDataLength - (uint)value)
                {
                    return InvalidData("SMBIOS 目录响应格式无效");
                }
                var type = (byte)record.State;
                structures[index] = new SmbiosStructureWebRecord(
                    record.Identity,
                    type,
                    SmbiosParser.GetTypeName(type),
                    (ushort)record.Flags,
                    (byte)(value >> 32),
                    (int)(uint)value,
                    (int)offset.Value,
                    [],
                    null);
            }
            return Results.Ok(new SmbiosWebRecord(
                records.Length == 0 ? (byte)0 : (byte)(records[0].State >> 8),
                records.Length == 0 ? (byte)0 : (byte)(records[0].State >> 16),
                records.Length == 0 ? (byte)0 : (byte)(records[0].State >> 24),
                structures));
        });
        app.MapPost("/api/firmware/smbios/structure", async (FirmwareIdentityRequest request) =>
        {
            var offset = ParseTableIdentity(request.Identity, "smbios:");
            if (offset is null ||
                offset > MaximumFirmwareDataLength)
            {
                return Results.BadRequest();
            }
            var records = await server.QueryAdministrationAsync(
                AdministrationOperation.QueryFirmware,
                request.Identity);
            if (records.Length != 1 || records[0].Kind != AdministrationKind.SmbiosTable ||
                !TryDecodeData(records[0], out var data) || data.Length > MaximumFirmwareDataLength)
            {
                return InvalidData("SMBIOS Structure 响应格式无效");
            }
            var status = SmbiosParser.TryParse(
                data,
                new SmbiosVersion((byte)records[0].Flags,
                                   (byte)(records[0].Flags >> 8),
                                   (byte)(records[0].Flags >> 16)),
                out var table);
            if (status != FirmwareDecodeStatus.Success || table is null || table.Structures.Count != 1)
            {
                return InvalidData($"SMBIOS Structure 解析失败：{status}");
            }
            var structure = table.Structures[0];
            return Results.Ok(new SmbiosStructureWebRecord(
                request.Identity,
                structure.Type,
                structure.TypeName,
                structure.Handle,
                structure.Length,
                structure.Data.Length,
                (int)offset.Value,
                structure.Fields.Select(field =>
                    new FirmwareFieldWebRecord(field.Name, FormatSmbiosField(structure, field))).ToArray(),
                data));
        });
        app.MapPost("/api/firmware/acpi", async () =>
        {
            var records = await server.QueryAdministrationAsync(AdministrationOperation.QueryFirmware, "acpi");
            if (records.Any(record => record.Kind != AdministrationKind.AcpiTable))
            {
                return InvalidData("ACPI 响应格式无效");
            }
            return Results.Ok(records.Select(record => new AcpiTableWebRecord(
                record.Identity,
                record.Name,
                AcpiParser.GetDescription(record.Name),
                null,
                null,
                0,
                0,
                null,
                [],
                null)));
        });
        app.MapPost("/api/firmware/acpi/table", async (FirmwareIdentityRequest request) =>
        {
            if (ParseTableIdentity(request.Identity, "acpi:") is null) return Results.BadRequest();
            var records = await server.QueryAdministrationAsync(
                AdministrationOperation.QueryFirmware,
                request.Identity);
            if (!TryGetSingleData(records, AdministrationKind.AcpiTable, out var data) ||
                data.Length > MaximumFirmwareDataLength)
            {
                return InvalidData("ACPI 表响应格式无效");
            }
            var status = AcpiParser.TryParse(data, out var table);
            if (status != FirmwareDecodeStatus.Success || table is null)
            {
                return Results.Ok(InvalidAcpi(request.Identity, status, data));
            }
            var header = table.Header;
            return Results.Ok(new AcpiTableWebRecord(
                request.Identity,
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
                    .ToArray(),
                data));
        });
        app.MapPost("/api/firmware/data", async (HttpContext context, FirmwareDataRequest request) =>
        {
            context.Response.Headers.CacheControl = "no-store";
            if (!ulong.TryParse(request.Offset, out var offset) || request.Length is 0 or > MaximumRangeLength ||
                !ulong.TryParse(request.BaseOffset ?? "0", out var baseOffset))
            {
                return Results.BadRequest();
            }
            var records = await server.QueryAdministrationAsync(
                AdministrationOperation.QueryFirmware,
                request.Identity);
            if (records.Length != 1 || !TryDecodeData(records[0], out var data) ||
                baseOffset > (ulong)data.Length ||
                !ulong.TryParse(request.ViewLength ??
                                    (data.Length - (int)baseOffset).ToString(CultureInfo.InvariantCulture),
                                out var viewLength) ||
                viewLength > (ulong)data.Length - baseOffset ||
                offset > viewLength)
            {
                return Results.BadRequest();
            }
            var length = Math.Min(request.Length, (uint)(viewLength - offset));
            return Results.Ok(new
            {
                Size = viewLength.ToString(CultureInfo.InvariantCulture),
                Data = data.AsSpan((int)(baseOffset + offset), (int)length).ToArray()
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

    private static AcpiTableWebRecord InvalidAcpi(
        string identity,
        FirmwareDecodeStatus status,
        byte[]? data = null)
    {
        var signature = identity.StartsWith("acpi:", StringComparison.Ordinal) && identity.Length > "acpi:".Length
            ? identity["acpi:".Length..]
            : identity;
        return new(identity, signature, "—", "—", "—", 0, (uint)(data?.Length ?? 0), status.ToString(), [], data);
    }

    private static uint? ParseTableIdentity(string identity, string prefix) =>
        identity.Length == prefix.Length + 8 && identity.StartsWith(prefix, StringComparison.Ordinal) &&
        uint.TryParse(identity.AsSpan(prefix.Length),
                      NumberStyles.HexNumber,
                      CultureInfo.InvariantCulture,
                      out var value)
            ? value
            : null;

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
    CpuidFeatureWebRecord[] Features);

internal sealed record CpuidFeatureWebRecord(uint Leaf, uint SubLeaf, string Register, int Bit, string Name);

internal sealed record SmbiosWebRecord(
    byte MajorVersion,
    byte MinorVersion,
    byte DmiRevision,
    SmbiosStructureWebRecord[] Structures);

internal sealed record SmbiosStructureWebRecord(
    string Identity,
    byte Type,
    string Name,
    ushort Handle,
    byte FormattedLength,
    int TotalLength,
    int Offset,
    FirmwareFieldWebRecord[] Fields,
    byte[]? Data);

internal sealed record AcpiTableWebRecord(
    string Identity,
    string Signature,
    string Description,
    string? OemId,
    string? OemTableId,
    byte Revision,
    uint Length,
    string? Error,
    FirmwareFieldWebRecord[] Fields,
    byte[]? Data);

internal sealed record FirmwareFieldWebRecord(string Name, string Value);
internal sealed record FirmwareIdentityRequest(string Identity);
internal sealed record FirmwareDataRequest(
    string Identity,
    string Offset,
    uint Length,
    string? BaseOffset = null,
    string? ViewLength = null);
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
