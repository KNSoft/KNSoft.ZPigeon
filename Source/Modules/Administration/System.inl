#include <KNSoft/MakeLifeEasier/System/Registry.h>

#define ZP_SYSTEM_INFORMATION_EDITABLE 0x00000001
#define ZP_SYSTEM_INFORMATION_RESTART_REQUIRED 0x00000002

typedef struct _ZP_SYSTEM_REGISTRY_RECORD
{
    UNICODE_STRING ValueName;
    PCWSTR Identity;
    PCWSTR Name;
    PCWSTR Group;
    ULONG Flags;
} ZP_SYSTEM_REGISTRY_RECORD, *PZP_SYSTEM_REGISTRY_RECORD;

typedef const ZP_SYSTEM_REGISTRY_RECORD* PCZP_SYSTEM_REGISTRY_RECORD;

static const UNICODE_STRING ZpSystemCurrentVersionKey = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion");
static const UNICODE_STRING ZpSystemBiosKey = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\BIOS");
static const UNICODE_STRING ZpSystemProcessorKey = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0");
static const UNICODE_STRING ZpSystemSecureBootKey = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\SecureBoot\\State");

static
NTSTATUS
ZpAdministration_AddSystemValue(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCWSTR Identity,
    _In_ PCWSTR Name,
    _In_ PCWSTR Group,
    _In_opt_ PCWSTR Value,
    _In_ ULONG Flags,
    _In_ ULONGLONG Number)
{
    return ZpAdministration_AddRecord(Builder,
                                      ZpAdministrationKindSystemInformation,
                                      0,
                                      Flags,
                                      Number,
                                      Identity,
                                      Name,
                                      Group,
                                      Value);
}

static
NTSTATUS
ZpAdministration_AddSystemRegistryRecord(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ HANDLE Key,
    _In_ PCZP_SYSTEM_REGISTRY_RECORD Record)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    PWSTR Value;
    ULONG Length;
    NTSTATUS Status;

    Status = Sys_RegQueryData(Key, &Record->ValueName, &Data);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return Record->Flags & ZP_SYSTEM_INFORMATION_EDITABLE ?
                   ZpAdministration_AddSystemValue(Builder,
                                                   Record->Identity,
                                                   Record->Name,
                                                   Record->Group,
                                                   NULL,
                                                   Record->Flags,
                                                   0) :
                   STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status)) return Status;
    if ((Data->Type != REG_SZ && Data->Type != REG_EXPAND_SZ) || Data->DataLength % sizeof(WCHAR) != 0)
    {
        Mem_Free(Data);
        return STATUS_DATA_ERROR;
    }
    Length = Data->DataLength / sizeof(WCHAR);
    while (Length != 0 && ((PCWCHAR)Data->Data)[Length - 1] == UNICODE_NULL) Length--;
    Value = Mem_Alloc(((SIZE_T)Length + 1) * sizeof(WCHAR));
    if (Value == NULL)
    {
        Mem_Free(Data);
        return STATUS_NO_MEMORY;
    }
    RtlCopyMemory(Value, Data->Data, (SIZE_T)Length * sizeof(WCHAR));
    Value[Length] = UNICODE_NULL;
    Status = ZpAdministration_AddSystemValue(Builder,
                                             Record->Identity,
                                             Record->Name,
                                             Record->Group,
                                             Value,
                                             Record->Flags,
                                             0);
    Mem_Free(Value);
    Mem_Free(Data);
    return Status;
}

static
NTSTATUS
ZpAdministration_AddSystemRegistryKey(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCUNICODE_STRING Path,
    _In_reads_(RecordCount) PCZP_SYSTEM_REGISTRY_RECORD Records,
    _In_ ULONG RecordCount)
{
    HANDLE Key;
    ULONG Index;
    NTSTATUS Status;

    Status = Sys_RegOpenKey(&Key, KEY_QUERY_VALUE, Path);
    if (!NT_SUCCESS(Status)) return Status;
    for (Index = 0; Index < RecordCount && NT_SUCCESS(Status); Index++)
    {
        Status = ZpAdministration_AddSystemRegistryRecord(Builder, Key, &Records[Index]);
    }
    NtClose(Key);
    return Status;
}

static
NTSTATUS
ZpAdministration_AddSystemDword(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCUNICODE_STRING KeyPath,
    _In_ PCUNICODE_STRING ValueName,
    _In_ PCWSTR Identity,
    _In_ PCWSTR Name,
    _In_ PCWSTR Group,
    _In_ BOOLEAN FileTime)
{
    HANDLE Key;
    ULONG Value;
    NTSTATUS Status;

    Status = Sys_RegOpenKey(&Key, KEY_QUERY_VALUE, KeyPath);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;
    Status = Sys_RegQueryDword(Key, ValueName, &Value);
    NtClose(Key);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;
    return ZpAdministration_AddSystemValue(Builder,
                                           Identity,
                                           Name,
                                           Group,
                                           NULL,
                                           0,
                                           FileTime ?
                                               (ULONGLONG)Value * 10000000 + 116444736000000000 : Value);
}

static
ZP_STATUS
ZpAdministration_EnumerateSystem(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static const ZP_SYSTEM_REGISTRY_RECORD CurrentVersionRecords[] = {
        { RTL_CONSTANT_STRING(L"ProductName"), L"productName", L"产品名称", L"Windows", 0 },
        { RTL_CONSTANT_STRING(L"DisplayVersion"), L"displayVersion", L"显示版本", L"Windows", 0 },
        { RTL_CONSTANT_STRING(L"EditionID"), L"edition", L"版本", L"Windows", 0 },
        { RTL_CONSTANT_STRING(L"InstallationType"), L"installationType", L"安装类型", L"Windows", 0 },
        { RTL_CONSTANT_STRING(L"BuildLabEx"), L"buildLab", L"完整版本", L"Windows", 0 },
        { RTL_CONSTANT_STRING(L"RegisteredOwner"), L"registeredOwner", L"注册所有者", L"Windows",
          ZP_SYSTEM_INFORMATION_EDITABLE },
        { RTL_CONSTANT_STRING(L"RegisteredOrganization"), L"registeredOrganization", L"注册组织", L"Windows",
          ZP_SYSTEM_INFORMATION_EDITABLE }
    };
    static const ZP_SYSTEM_REGISTRY_RECORD BiosRecords[] = {
        { RTL_CONSTANT_STRING(L"SystemManufacturer"), L"manufacturer", L"制造商", L"硬件", 0 },
        { RTL_CONSTANT_STRING(L"SystemProductName"), L"model", L"型号", L"硬件", 0 },
        { RTL_CONSTANT_STRING(L"SystemFamily"), L"family", L"产品系列", L"硬件", 0 },
        { RTL_CONSTANT_STRING(L"SystemSKU"), L"sku", L"系统 SKU", L"硬件", 0 },
        { RTL_CONSTANT_STRING(L"BaseBoardManufacturer"), L"baseboardManufacturer", L"主板制造商", L"硬件", 0 },
        { RTL_CONSTANT_STRING(L"BaseBoardProduct"), L"baseboardProduct", L"主板型号", L"硬件", 0 },
        { RTL_CONSTANT_STRING(L"BIOSVendor"), L"biosVendor", L"BIOS 制造商", L"固件", 0 },
        { RTL_CONSTANT_STRING(L"BIOSVersion"), L"biosVersion", L"BIOS 版本", L"固件", 0 },
        { RTL_CONSTANT_STRING(L"BIOSReleaseDate"), L"biosDate", L"BIOS 日期", L"固件", 0 }
    };
    static const ZP_SYSTEM_REGISTRY_RECORD ProcessorRecords[] = {
        { RTL_CONSTANT_STRING(L"ProcessorNameString"), L"processor", L"处理器", L"硬件", 0 }
    };
    static const UNICODE_STRING InstallDate = RTL_CONSTANT_STRING(L"InstallDate");
    static const UNICODE_STRING SecureBoot = RTL_CONSTANT_STRING(L"UEFISecureBootEnabled");
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    SYSTEM_TIMEOFDAY_INFORMATION Time;
    DYNAMIC_TIME_ZONE_INFORMATION TimeZone;
    WCHAR Buffer[256];
    DWORD Length, Error;
    FIRMWARE_TYPE Firmware;
    PCWSTR FirmwareName;
    NTSTATUS Status;

    Length = ARRAYSIZE(Buffer);
    if (!GetComputerNameExW(ComputerNamePhysicalDnsHostname, Buffer, &Length))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    Status = ZpAdministration_AddSystemValue(&Builder,
                                             L"computerName",
                                             L"计算机名",
                                             L"系统",
                                             Buffer,
                                             ZP_SYSTEM_INFORMATION_EDITABLE |
                                                 ZP_SYSTEM_INFORMATION_RESTART_REQUIRED,
                                             0);
    if (NT_SUCCESS(Status))
    {
        Length = ARRAYSIZE(Buffer);
        if (!GetComputerNameExW(ComputerNameDnsFullyQualified, Buffer, &Length))
        {
            Error = GetLastError();
            ZpAdministration_FreeBuilder(&Builder);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        Status = ZpAdministration_AddSystemValue(
            &Builder, L"fullComputerName", L"完整计算机名", L"系统", Buffer, 0, 0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemValue(&Builder,
                                                 L"systemRoot",
                                                 L"系统目录",
                                                 L"系统",
                                                 SharedUserData->NtSystemRoot,
                                                 0,
                                                 0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = NtQuerySystemInformation(SystemTimeOfDayInformation, &Time, sizeof(Time), NULL);
        if (NT_SUCCESS(Status))
        {
            Status = ZpAdministration_AddSystemValue(&Builder,
                                                     L"bootTime",
                                                     L"启动时间",
                                                     L"系统",
                                                     NULL,
                                                     0,
                                                     Time.BootTime.QuadPart);
        }
    }
    if (NT_SUCCESS(Status))
    {
        if (GetDynamicTimeZoneInformation(&TimeZone) == TIME_ZONE_ID_INVALID)
        {
            Error = GetLastError();
            ZpAdministration_FreeBuilder(&Builder);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        Status = ZpAdministration_AddSystemValue(&Builder,
                                                 L"timeZone",
                                                 L"时区",
                                                 L"区域",
                                                 TimeZone.TimeZoneKeyName,
                                                 0,
                                                 0);
    }
    if (NT_SUCCESS(Status))
    {
        if (GetSystemDefaultLocaleName(Buffer, ARRAYSIZE(Buffer)) == 0)
        {
            Error = GetLastError();
            ZpAdministration_FreeBuilder(&Builder);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        Status = ZpAdministration_AddSystemValue(&Builder, L"locale", L"系统区域", L"区域", Buffer, 0, 0);
    }
    if (NT_SUCCESS(Status))
    {
        if (!GetFirmwareType(&Firmware))
        {
            Error = GetLastError();
            ZpAdministration_FreeBuilder(&Builder);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        switch (Firmware)
        {
            case FirmwareTypeBios: FirmwareName = L"BIOS"; break;
            case FirmwareTypeUefi: FirmwareName = L"UEFI"; break;
            default: FirmwareName = L"未知"; break;
        }
        Status = ZpAdministration_AddSystemValue(&Builder,
                                                 L"firmware",
                                                 L"固件类型",
                                                 L"固件",
                                                 FirmwareName,
                                                 0,
                                                 Firmware);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemRegistryKey(&Builder,
                                                       &ZpSystemCurrentVersionKey,
                                                       CurrentVersionRecords,
                                                       ARRAYSIZE(CurrentVersionRecords));
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemRegistryKey(
            &Builder, &ZpSystemBiosKey, BiosRecords, ARRAYSIZE(BiosRecords));
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemRegistryKey(
            &Builder, &ZpSystemProcessorKey, ProcessorRecords, ARRAYSIZE(ProcessorRecords));
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemDword(&Builder,
                                                 &ZpSystemCurrentVersionKey,
                                                 &InstallDate,
                                                 L"installTime",
                                                 L"安装时间",
                                                 L"Windows",
                                                 TRUE);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemDword(&Builder,
                                                 &ZpSystemSecureBootKey,
                                                 &SecureBoot,
                                                 L"secureBoot",
                                                 L"安全启动",
                                                 L"固件",
                                                 FALSE);
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlSystem(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    static const UNICODE_STRING RegisteredOwner = RTL_CONSTANT_STRING(L"RegisteredOwner");
    static const UNICODE_STRING RegisteredOrganization = RTL_CONSTANT_STRING(L"RegisteredOrganization");
    PCUNICODE_STRING ValueName;
    PWSTR Identity, Argument;
    HANDLE Key;
    NTSTATUS Status;
    DWORD Error;

    if (Control->Action != ZpAdministrationActionConfigure)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    if (Control->Argument.Length > 32767) return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    Identity = ZpAdministration_CopyView(&Control->Identity);
    Argument = ZpAdministration_CopyView(&Control->Argument);
    if (Identity == NULL || Argument == NULL)
    {
        Mem_Free(Argument);
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    if (wcslen(Identity) != Control->Identity.Length || wcslen(Argument) != Control->Argument.Length)
    {
        Mem_Free(Argument);
        Mem_Free(Identity);
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    if (wcscmp(Identity, L"computerName") == 0)
    {
        if (!SetComputerNameExW(ComputerNamePhysicalDnsHostname, Argument))
        {
            Error = GetLastError();
            Mem_Free(Argument);
            Mem_Free(Identity);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        Status = STATUS_SUCCESS;
    }
    else
    {
        ValueName = wcscmp(Identity, L"registeredOwner") == 0 ? &RegisteredOwner :
                        wcscmp(Identity, L"registeredOrganization") == 0 ? &RegisteredOrganization : NULL;
        if (ValueName == NULL)
        {
            Status = STATUS_NOT_SUPPORTED;
        }
        else
        {
            Status = Sys_RegOpenKey(&Key, KEY_SET_VALUE, &ZpSystemCurrentVersionKey);
            if (NT_SUCCESS(Status))
            {
                Status = NtSetValueKey(Key,
                                       (PUNICODE_STRING)ValueName,
                                       0,
                                       REG_SZ,
                                       Argument,
                                       (Control->Argument.Length + 1) * sizeof(WCHAR));
                NtClose(Key);
            }
        }
    }
    Mem_Free(Argument);
    Mem_Free(Identity);
    return ZpStatus_FromNtStatus(Status);
}
