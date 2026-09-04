#include <KNSoft/MakeLifeEasier/System/Registry.h>

#define ZP_SYSTEM_INFORMATION_EDITABLE 0x00000001
#define ZP_SYSTEM_INFORMATION_RESTART_REQUIRED 0x00000002
#define ZP_SYSTEM_INFORMATION_DISPLAY 0x00000004
#define ZP_ENVIRONMENT_USER 0x00000100
#define ZP_ENVIRONMENT_SYSTEM 0x00000200

typedef struct _ZP_SYSTEM_REGISTRY_RECORD
{
    UNICODE_STRING ValueName;
    PCWSTR Identity;
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
static const UNICODE_STRING ZpSystemEnvironmentKey = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment");
static const UNICODE_STRING ZpUserEnvironmentKey = RTL_CONSTANT_STRING(L"Environment");
static
NTSTATUS
ZpAdministration_AddSystemValue(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PCWSTR Identity,
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
                                      NULL,
                                      NULL,
                                      Value);
}

static
NTSTATUS
ZpAdministration_AddDisplays(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    DISPLAY_DEVICEW Adapter = { sizeof(Adapter) }, Monitor = { sizeof(Monitor) };
    DEVMODEW Mode = { 0 };
    ZP_CODEC_WRITER Writer;
    BYTE Data[sizeof(ULONG) * 3];
    NTSTATUS Status = STATUS_SUCCESS;
    DWORD AdapterIndex, MonitorIndex;
    ULONG Width, Height, Frequency;

    Mode.dmSize = sizeof(Mode);
    for (AdapterIndex = 0; NT_SUCCESS(Status) && EnumDisplayDevicesW(NULL, AdapterIndex, &Adapter, 0);
         AdapterIndex++)
    {
        if (!(Adapter.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
        for (MonitorIndex = 0;
             NT_SUCCESS(Status) && EnumDisplayDevicesW(Adapter.DeviceName, MonitorIndex, &Monitor, 0);
             MonitorIndex++)
        {
            if (!(Monitor.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
            Width = Height = Frequency = 0;
            if (EnumDisplaySettingsExW(Adapter.DeviceName, ENUM_CURRENT_SETTINGS, &Mode, 0))
            {
                Width = Mode.dmPelsWidth;
                Height = Mode.dmPelsHeight;
                Frequency = Mode.dmDisplayFrequency;
            }
            ZpCodec_InitializeWriter(&Writer, Data, sizeof(Data));
            Status = ZpCodec_WriteUInt32(&Writer, Width);
            if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Height);
            if (NT_SUCCESS(Status)) Status = ZpCodec_WriteUInt32(&Writer, Frequency);
            if (NT_SUCCESS(Status))
            {
                Status = ZpAdministration_AddRecordData(Builder,
                                                         ZpAdministrationKindSystemInformation,
                                                         0,
                                                         ZP_SYSTEM_INFORMATION_DISPLAY,
                                                         0,
                                                         Monitor.DeviceID,
                                                         Monitor.DeviceString,
                                                         Adapter.DeviceName,
                                                         NULL,
                                                         Data,
                                                         Writer.Offset);
            }
        }
    }
    return Status;
}

#include "RemoteDesktop.inl"

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
                                           NULL,
                                           0,
                                           FileTime ?
                                               (ULONGLONG)Value * 10000000 + 116444736000000000 : Value);
}

typedef
PWSTR
(WINAPI* ZP_BRANDING_FORMAT_STRING)(
    _In_ PCWSTR Format);

static
NTSTATUS
ZpAdministration_AddWindowsProductName(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    ZP_BRANDING_FORMAT_STRING BrandingFormatString;
    HMODULE Module;
    PWSTR Value;
    NTSTATUS Status;

    Module = LoadLibraryExW(L"winbrand.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Module == NULL) return NTSTATUS_FROM_WIN32(GetLastError());
    BrandingFormatString = (ZP_BRANDING_FORMAT_STRING)GetProcAddress(
        Module,
        "BrandingFormatString");
    if (BrandingFormatString == NULL)
    {
        Status = NTSTATUS_FROM_WIN32(GetLastError());
    }
    else
    {
        Value = BrandingFormatString(L"%WINDOWS_LONG%");
        if (Value == NULL)
        {
            Status = STATUS_RESOURCE_DATA_NOT_FOUND;
        }
        else
        {
            Status = ZpAdministration_AddSystemValue(Builder,
                                                     L"productName",
                                                     Value,
                                                     0,
                                                     0);
            GlobalFree(Value);
        }
    }
    FreeLibrary(Module);
    return Status;
}

static
NTSTATUS
ZpAdministration_OpenEnvironment(
    _In_ BOOLEAN User,
    _In_ ACCESS_MASK Access,
    _Out_ PHANDLE Key)
{
    HANDLE CurrentUser;
    NTSTATUS Status;

    if (!User)
    {
        return Sys_RegOpenKey(Key, Access, &ZpSystemEnvironmentKey);
    }
    Status = RtlOpenCurrentUser(KEY_READ, &CurrentUser);
    if (NT_SUCCESS(Status))
    {
        Status = Sys_RegOpenKeyEx(Key, CurrentUser, Access, &ZpUserEnvironmentKey);
        NtClose(CurrentUser);
    }
    return Status;
}

static
NTSTATUS
ZpAdministration_AddEnvironment(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ BOOLEAN User)
{
    PKEY_VALUE_FULL_INFORMATION Information = NULL;
    HANDLE Key;
    PWSTR Identity = NULL, Name = NULL, Value = NULL;
    PKEY_VALUE_FULL_INFORMATION NewInformation;
    SIZE_T IdentitySize;
    ULONG Index = 0, Length = 1024, Required, NameLength, ValueLength;
    NTSTATUS Status;

    Status = ZpAdministration_OpenEnvironment(User, KEY_QUERY_VALUE, &Key);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Information = Mem_Alloc(Length);
    if (Information == NULL)
    {
        NtClose(Key);
        return STATUS_NO_MEMORY;
    }
    for (;; Index++)
    {
        for (;;)
        {
            Status = NtEnumerateValueKey(Key,
                                         Index,
                                         KeyValueFullInformation,
                                         Information,
                                         Length,
                                         &Required);
            if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            {
                break;
            }
            NewInformation = Mem_ReAlloc(Information, Required);
            if (NewInformation == NULL)
            {
                Status = STATUS_NO_MEMORY;
                break;
            }
            Information = NewInformation;
            Length = Required;
        }
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        if ((Information->Type != REG_SZ && Information->Type != REG_EXPAND_SZ) ||
            Information->NameLength % sizeof(WCHAR) != 0 ||
            Information->DataLength % sizeof(WCHAR) != 0)
        {
            continue;
        }
        NameLength = Information->NameLength / sizeof(WCHAR);
        ValueLength = Information->DataLength / sizeof(WCHAR);
        while (ValueLength != 0 &&
               ((PCWCHAR)Add2Ptr(Information, Information->DataOffset))[ValueLength - 1] == UNICODE_NULL)
        {
            ValueLength--;
        }
        IdentitySize = ((SIZE_T)NameLength + 20) * sizeof(WCHAR);
        Identity = Mem_Alloc(IdentitySize);
        Name = Mem_Alloc(((SIZE_T)NameLength + 1) * sizeof(WCHAR));
        Value = Mem_Alloc(((SIZE_T)ValueLength + 1) * sizeof(WCHAR));
        if (Identity == NULL || Name == NULL || Value == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        _snwprintf_s(Identity,
                     IdentitySize / sizeof(WCHAR),
                     _TRUNCATE,
                     User ? L"environment:user:%.*s" : L"environment:system:%.*s",
                     NameLength,
                     Information->Name);
        RtlCopyMemory(Value,
                      Add2Ptr(Information, Information->DataOffset),
                      (SIZE_T)ValueLength * sizeof(WCHAR));
        Value[ValueLength] = UNICODE_NULL;
        RtlCopyMemory(Name, Information->Name, Information->NameLength);
        Name[NameLength] = UNICODE_NULL;
        Status = ZpAdministration_AddRecord(
            Builder,
            ZpAdministrationKindEnvironmentVariable,
            Information->Type,
            ZP_SYSTEM_INFORMATION_EDITABLE |
                (User ? ZP_ENVIRONMENT_USER : ZP_ENVIRONMENT_SYSTEM),
            0,
            Identity,
            Name,
            NULL,
            Value);
        Mem_Free(Value);
        Mem_Free(Name);
        Mem_Free(Identity);
        Value = Name = Identity = NULL;
        if (!NT_SUCCESS(Status))
        {
            break;
        }
    }
    Mem_Free(Value);
    Mem_Free(Name);
    Mem_Free(Identity);
    Mem_Free(Information);
    NtClose(Key);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateSystem(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static const ZP_SYSTEM_REGISTRY_RECORD CurrentVersionRecords[] = {
        { RTL_CONSTANT_STRING(L"DisplayVersion"), L"displayVersion", 0 },
        { RTL_CONSTANT_STRING(L"EditionID"), L"edition", 0 },
        { RTL_CONSTANT_STRING(L"InstallationType"), L"installationType", 0 },
        { RTL_CONSTANT_STRING(L"BuildLabEx"), L"buildLab", 0 },
        { RTL_CONSTANT_STRING(L"RegisteredOwner"), L"registeredOwner", ZP_SYSTEM_INFORMATION_EDITABLE },
        { RTL_CONSTANT_STRING(L"RegisteredOrganization"), L"registeredOrganization",
          ZP_SYSTEM_INFORMATION_EDITABLE }
    };
    static const ZP_SYSTEM_REGISTRY_RECORD BiosRecords[] = {
        { RTL_CONSTANT_STRING(L"SystemManufacturer"), L"manufacturer", 0 },
        { RTL_CONSTANT_STRING(L"SystemProductName"), L"model", 0 },
        { RTL_CONSTANT_STRING(L"SystemFamily"), L"family", 0 },
        { RTL_CONSTANT_STRING(L"SystemSKU"), L"sku", 0 },
        { RTL_CONSTANT_STRING(L"BaseBoardManufacturer"), L"baseboardManufacturer", 0 },
        { RTL_CONSTANT_STRING(L"BaseBoardProduct"), L"baseboardProduct", 0 },
        { RTL_CONSTANT_STRING(L"BIOSVendor"), L"biosVendor", 0 },
        { RTL_CONSTANT_STRING(L"BIOSVersion"), L"biosVersion", 0 },
        { RTL_CONSTANT_STRING(L"BIOSReleaseDate"), L"biosDate", 0 }
    };
    static const ZP_SYSTEM_REGISTRY_RECORD ProcessorRecords[] = {
        { RTL_CONSTANT_STRING(L"ProcessorNameString"), L"processor", 0 }
    };
    static const UNICODE_STRING InstallDate = RTL_CONSTANT_STRING(L"InstallDate");
    static const UNICODE_STRING SecureBoot = RTL_CONSTANT_STRING(L"UEFISecureBootEnabled");
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    SYSTEM_TIMEOFDAY_INFORMATION Time;
    DYNAMIC_TIME_ZONE_INFORMATION TimeZone;
    WCHAR Buffer[256];
    DWORD Length, Error;
    FIRMWARE_TYPE Firmware;
    NTSTATUS Status;

    Length = ARRAYSIZE(Buffer);
    if (!GetComputerNameExW(ComputerNamePhysicalDnsHostname, Buffer, &Length))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    Status = ZpAdministration_AddSystemValue(&Builder,
                                             L"computerName",
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
        Status = ZpAdministration_AddSystemValue(&Builder, L"fullComputerName", Buffer, 0, 0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemValue(&Builder,
                                                 L"systemRoot",
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
                                                 TimeZone.TimeZoneKeyName,
                                                 ZP_SYSTEM_INFORMATION_EDITABLE,
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
        Status = ZpAdministration_AddSystemValue(&Builder,
                                                 L"locale",
                                                 Buffer,
                                                 ZP_SYSTEM_INFORMATION_EDITABLE,
                                                 0);
    }
    if (NT_SUCCESS(Status))
    {
        if (!GetFirmwareType(&Firmware))
        {
            Error = GetLastError();
            ZpAdministration_FreeBuilder(&Builder);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        Status = ZpAdministration_AddSystemValue(&Builder,
                                                 L"firmware",
                                                 NULL,
                                                 0,
                                                 Firmware);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddWindowsProductName(&Builder);
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
                                                 TRUE);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddSystemDword(&Builder,
                                                 &ZpSystemSecureBootKey,
                                                 &SecureBoot,
                                                 L"secureBoot",
                                                 FALSE);
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_AddDisplays(&Builder);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_AddEnvironment(&Builder, TRUE);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_AddEnvironment(&Builder, FALSE);
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
    UNICODE_STRING EnvironmentName;
    BOOLEAN User;
    PWSTR Identity, Argument;
    HANDLE Key;
    NTSTATUS Status;
    DWORD Error;

    if (Control->Action != ZpAdministrationActionConfigure &&
        Control->Action != ZpAdministrationActionDelete)
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
    if (wcsncmp(Identity, L"environment:user:", 17) == 0 ||
        wcsncmp(Identity, L"environment:system:", 19) == 0)
    {
        User = Identity[12] == L'u';
        RtlInitUnicodeString(&EnvironmentName, Identity + (User ? 17 : 19));
        if (EnvironmentName.Length == 0)
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        else
        {
            Status = ZpAdministration_OpenEnvironment(User, KEY_SET_VALUE, &Key);
            if (NT_SUCCESS(Status))
            {
                Status = Control->Action == ZpAdministrationActionDelete ?
                             NtDeleteValueKey(Key, &EnvironmentName) :
                             NtSetValueKey(Key,
                                           &EnvironmentName,
                                           0,
                                           REG_SZ,
                                           Argument,
                                           (Control->Argument.Length + 1) * sizeof(WCHAR));
                NtClose(Key);
            }
        }
    }
    else if (Control->Action != ZpAdministrationActionConfigure)
    {
        Status = STATUS_NOT_SUPPORTED;
    }
    else if (wcscmp(Identity, L"computerName") == 0)
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
    else if (wcscmp(Identity, L"locale") == 0)
    {
        LCID Locale;

        Status = RtlLocaleNameToLcid(Argument, &Locale, 0);
        if (NT_SUCCESS(Status))
        {
            Status = NtSetDefaultLocale(FALSE, Locale);
        }
    }
    else if (wcscmp(Identity, L"timeZone") == 0)
    {
        DYNAMIC_TIME_ZONE_INFORMATION TimeZone;
        DWORD Index = 0;

        for (;; Index++)
        {
            Error = EnumDynamicTimeZoneInformation(Index, &TimeZone);
            if (Error != ERROR_SUCCESS)
            {
                Status = Error == ERROR_NO_MORE_ITEMS ? STATUS_OBJECT_NAME_NOT_FOUND :
                                                        NTSTATUS_FROM_WIN32(Error);
                break;
            }
            if (_wcsicmp(TimeZone.TimeZoneKeyName, Argument) == 0)
            {
                if (!SetDynamicTimeZoneInformation(&TimeZone))
                {
                    Error = GetLastError();
                    Mem_Free(Argument);
                    Mem_Free(Identity);
                    return ZpStatus_FromCode(ZpStatusWin32, Error);
                }
                Status = STATUS_SUCCESS;
                break;
            }
        }
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
