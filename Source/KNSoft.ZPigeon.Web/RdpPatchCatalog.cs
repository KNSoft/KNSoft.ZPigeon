using System.Buffers.Binary;
using System.Globalization;

namespace KNSoft.ZPigeon.Web;

internal sealed class RdpPatchCatalog
{
    private const byte Executable = 0x80;
    private static readonly string[] SlInitNames =
    [
        "bServerSku",
        "bRemoteConnAllowed",
        "bFUSEnabled",
        "bAppServerAllowed",
        "bMultimonAllowed",
        "lMaxUserSessions",
        "ulMaxDebugSessions",
        "bInitialized"
    ];
    private readonly Dictionary<string, Dictionary<string, string>> sections =
        new(StringComparer.OrdinalIgnoreCase);

    internal RdpPatchCatalog(string path)
    {
        Dictionary<string, string>? section = null;
        foreach (var sourceLine in File.ReadLines(path))
        {
            var line = sourceLine.AsSpan().Trim();
            if (line.IsEmpty || line[0] is ';' or '#') continue;
            if (line[0] == '[' && line[^1] == ']')
            {
                var name = line[1..^1].Trim().ToString();
                if (!sections.TryGetValue(name, out section))
                {
                    section = new(StringComparer.OrdinalIgnoreCase);
                    sections.Add(name, section);
                }
                continue;
            }
            var separator = line.IndexOf('=');
            if (section is null || separator <= 0) continue;
            section[line[..separator].Trim().ToString()] = line[(separator + 1)..].Trim().ToString();
        }
    }

    internal bool TryCreatePlan(ulong version, out byte[] plan)
    {
        var versionName = FormatVersion(version);
        if (!sections.TryGetValue(versionName, out var versionSection) ||
            !sections.TryGetValue("PatchCodes", out var patchCodes) ||
            !sections.TryGetValue("SLInit", out var slInit) ||
            !sections.TryGetValue($"{versionName}-SLInit", out var versionSlInit))
        {
            plan = [];
            return false;
        }
        var patches = new List<Patch>(11);
        AddCodePatch(versionSection, patchCodes, "LocalOnly", false, patches);
        if (!AddCodePatch(versionSection, patchCodes, "SingleUser", true, patches) ||
            !AddCodePatch(versionSection, patchCodes, "DefPolicy", true, patches) ||
            !versionSection.TryGetValue("SLInitHook.x64", out var slInitHook) || slInitHook != "1")
        {
            plan = [];
            return false;
        }
        foreach (var name in SlInitNames)
        {
            var value = ParseDecimal(Get(slInit, name));
            var data = GC.AllocateUninitializedArray<byte>(sizeof(uint));
            BinaryPrimitives.WriteUInt32LittleEndian(data, value);
            patches.Add(new(ParseHexUInt32(Get(versionSlInit, $"{name}.x64")), false, data));
        }
        if (patches.Count > 16) throw new InvalidDataException("The RDP patch plan is too large.");
        var length = sizeof(ulong) + sizeof(byte) + patches.Sum(patch => sizeof(uint) + sizeof(byte) + patch.Data.Length);
        plan = GC.AllocateUninitializedArray<byte>(length);
        BinaryPrimitives.WriteUInt64LittleEndian(plan, version);
        plan[sizeof(ulong)] = checked((byte)patches.Count);
        var offset = sizeof(ulong) + sizeof(byte);
        foreach (var patch in patches)
        {
            BinaryPrimitives.WriteUInt32LittleEndian(plan.AsSpan(offset), patch.Offset);
            plan[offset + sizeof(uint)] = checked((byte)(patch.Data.Length | (patch.Executable ? Executable : 0)));
            offset += sizeof(uint) + sizeof(byte);
            patch.Data.CopyTo(plan, offset);
            offset += patch.Data.Length;
        }
        return true;
    }

    internal static string FormatVersion(ulong version) =>
        $"{version >> 48}.{version >> 32 & 0xFFFF}.{version >> 16 & 0xFFFF}.{version & 0xFFFF}";

    private static bool AddCodePatch(
        Dictionary<string, string> version,
        Dictionary<string, string> patchCodes,
        string name,
        bool required,
        List<Patch> patches)
    {
        if (!version.TryGetValue($"{name}Patch.x64", out var enabled) || enabled == "0") return !required;
        if (enabled != "1") throw new InvalidDataException($"Invalid {name} patch switch.");
        var codeName = Get(version, $"{name}Code.x64");
        byte[] data;
        try
        {
            data = Convert.FromHexString(Get(patchCodes, codeName));
        }
        catch (FormatException exception)
        {
            throw new InvalidDataException($"Invalid {name} patch code.", exception);
        }
        if (data.Length is 0 or > 32) throw new InvalidDataException($"Invalid {name} patch length.");
        patches.Add(new(ParseHexUInt32(Get(version, $"{name}Offset.x64")), true, data));
        return true;
    }

    private static string Get(Dictionary<string, string> section, string name) =>
        section.TryGetValue(name, out var value) ? value :
            throw new InvalidDataException($"Missing rdpwrap.ini value: {name}.");

    private static uint ParseHexUInt32(string value) =>
        uint.TryParse(value, NumberStyles.AllowHexSpecifier, CultureInfo.InvariantCulture, out var result) ? result :
            throw new InvalidDataException("Invalid rdpwrap.ini offset.");

    private static uint ParseDecimal(string value) =>
        uint.TryParse(value, NumberStyles.None, CultureInfo.InvariantCulture, out var result) ? result :
            throw new InvalidDataException("Invalid rdpwrap.ini value.");

    private sealed record Patch(uint Offset, bool Executable, byte[] Data);
}
