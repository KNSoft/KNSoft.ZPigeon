#define ZP_FIRMWARE_MAX_DATA_SIZE (16 * 1024 * 1024)
#define ZP_FIRMWARE_VARIABLE_PREFIX L"uefi:"
#define ZP_FIRMWARE_CPUID_IDENTITY L"cpuid"
#define ZP_FIRMWARE_SMBIOS_IDENTITY L"smbios"
#define ZP_FIRMWARE_SMBIOS_PREFIX L"smbios:"
#define ZP_FIRMWARE_ACPI_IDENTITY L"acpi"
#define ZP_FIRMWARE_ACPI_PREFIX L"acpi:"
#define ZP_FIRMWARE_BOOT_ENTRY_MISSING 0x80000000
#define ZP_FIRMWARE_FOURCC(A, B, C, D) \
    (((ULONG)(A) << 24) | ((ULONG)(B) << 16) | ((ULONG)(C) << 8) | (ULONG)(D))

static const GUID ZpFirmwareGlobalVariableGuid = {
    0x8be4df61, 0x93ca, 0x11d2, { 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b, 0x8c }
};

#pragma pack(push, 1)
typedef struct _ZP_FIRMWARE_LOAD_OPTION
{
    ULONG Attributes;
    USHORT FilePathListLength;
    WCHAR Description[ANYSIZE_ARRAY];
} ZP_FIRMWARE_LOAD_OPTION, *PZP_FIRMWARE_LOAD_OPTION;

typedef struct _ZP_FIRMWARE_CPUID_RECORD
{
    ULONG Leaf;
    ULONG SubLeaf;
    ULONG Eax;
    ULONG Ebx;
    ULONG Ecx;
    ULONG Edx;
} ZP_FIRMWARE_CPUID_RECORD, *PZP_FIRMWARE_CPUID_RECORD;

typedef struct _ZP_FIRMWARE_SMBIOS_RAW_DATA
{
    BYTE Used20CallingMethod;
    BYTE MajorVersion;
    BYTE MinorVersion;
    BYTE DmiRevision;
    ULONG Length;
    BYTE Data[];
} ZP_FIRMWARE_SMBIOS_RAW_DATA, *PZP_FIRMWARE_SMBIOS_RAW_DATA;

typedef struct _ZP_FIRMWARE_SMBIOS_HEADER
{
    BYTE Type;
    BYTE Length;
    USHORT Handle;
} ZP_FIRMWARE_SMBIOS_HEADER, *PZP_FIRMWARE_SMBIOS_HEADER;
#pragma pack(pop)

C_ASSERT(sizeof(ZP_FIRMWARE_CPUID_RECORD) == 24);
C_ASSERT(FIELD_OFFSET(ZP_FIRMWARE_SMBIOS_RAW_DATA, Data) == 8);
C_ASSERT(sizeof(ZP_FIRMWARE_SMBIOS_HEADER) == 4);

typedef struct _ZP_FIRMWARE_SMBIOS_STRUCTURE
{
    const ZP_FIRMWARE_SMBIOS_HEADER* Header;
    ULONG Offset;
    ULONG TotalLength;
} ZP_FIRMWARE_SMBIOS_STRUCTURE, *PZP_FIRMWARE_SMBIOS_STRUCTURE;

static RTL_SRWLOCK ZpFirmwareSmbiosLock = RTL_SRWLOCK_INIT;
// SMBIOS is immutable for the current boot; retain one validated process-lifetime snapshot.
static PZP_FIRMWARE_SMBIOS_RAW_DATA ZpFirmwareSmbiosSnapshot;
static RTL_SRWLOCK ZpFirmwareCacheLock = RTL_SRWLOCK_INIT;
static PBYTE ZpFirmwareCpuidResponse;
static ULONG ZpFirmwareCpuidResponseLength;
static PBYTE ZpFirmwareAcpiDirectoryResponse;
static ULONG ZpFirmwareAcpiDirectoryResponseLength;

typedef struct _ZP_FIRMWARE_ACPI_CACHE_ENTRY
{
    ULONG TableId;
    PBYTE Data;
    ULONG Length;
} ZP_FIRMWARE_ACPI_CACHE_ENTRY, *PZP_FIRMWARE_ACPI_CACHE_ENTRY;

static PZP_FIRMWARE_ACPI_CACHE_ENTRY ZpFirmwareAcpiTables;
static ULONG ZpFirmwareAcpiTableCount;

static
NTSTATUS
ZpFirmware_AdjustPrivilege(
    _Out_ PBOOLEAN Previous)
{
    return RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, TRUE, FALSE, Previous);
}

static
NTSTATUS
ZpFirmware_CreateVariableIdentity(
    _In_ PCGUID VendorGuid,
    _In_ PCWSTR Name,
    _Outptr_ PWSTR* Identity)
{
    UNICODE_STRING Guid;
    SIZE_T Length;
    PWSTR Buffer;
    NTSTATUS Status;

    Status = RtlStringFromGUID(VendorGuid, &Guid);
    if (!NT_SUCCESS(Status)) return Status;
    Length = RTL_NUMBER_OF(ZP_FIRMWARE_VARIABLE_PREFIX) - 1 + Guid.Length / sizeof(WCHAR) + 1 + wcslen(Name) + 1;
    Buffer = Length <= MAXULONG ? Mem_Alloc(Length * sizeof(WCHAR)) : NULL;
    if (Buffer == NULL)
    {
        RtlFreeUnicodeString(&Guid);
        return Length <= MAXULONG ? STATUS_NO_MEMORY : STATUS_QUOTA_EXCEEDED;
    }
    _snwprintf_s(Buffer,
                 Length,
                 _TRUNCATE,
                 ZP_FIRMWARE_VARIABLE_PREFIX L"%wZ:%s",
                 &Guid,
                 Name);
    RtlFreeUnicodeString(&Guid);
    *Identity = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFirmware_ParseVariableIdentity(
    _In_ PCZP_STRING_VIEW Identity,
    _Out_ GUID* VendorGuid,
    _Out_ UNICODE_STRING* Name,
    _Outptr_ PWSTR* Buffer)
{
    UNICODE_STRING Guid;
    PWSTR Separator;
    NTSTATUS Status;

    *Buffer = ZpAdministration_CopyView(Identity);
    if (*Buffer == NULL) return STATUS_NO_MEMORY;
    if (wcsncmp(*Buffer,
                ZP_FIRMWARE_VARIABLE_PREFIX,
                RTL_NUMBER_OF(ZP_FIRMWARE_VARIABLE_PREFIX) - 1) != 0)
    {
        Mem_Free(*Buffer);
        return STATUS_INVALID_PARAMETER;
    }
    Separator = wcschr(*Buffer + RTL_NUMBER_OF(ZP_FIRMWARE_VARIABLE_PREFIX) - 1, L':');
    if (Separator == NULL || Separator[1] == UNICODE_NULL)
    {
        Mem_Free(*Buffer);
        return STATUS_INVALID_PARAMETER;
    }
    RtlInitUnicodeString(&Guid, *Buffer + RTL_NUMBER_OF(ZP_FIRMWARE_VARIABLE_PREFIX) - 1);
    Guid.Length = (USHORT)((Separator - Guid.Buffer) * sizeof(WCHAR));
    Guid.MaximumLength = Guid.Length;
    Status = RtlGUIDFromString(&Guid, VendorGuid);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(*Buffer);
        return Status;
    }
    RtlInitUnicodeString(Name, Separator + 1);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFirmware_QueryVariable(
    _In_ PCUNICODE_STRING Name,
    _In_ PCGUID VendorGuid,
    _Outptr_result_bytebuffer_(*Length) PBYTE* Data,
    _Out_ PULONG Length,
    _Out_opt_ PULONG Attributes)
{
    BOOLEAN Previous;
    ULONG Capacity;
    NTSTATUS Status;

    Status = ZpFirmware_AdjustPrivilege(&Previous);
    if (!NT_SUCCESS(Status)) return Status;
    *Length = 0;
    Status = NtQuerySystemEnvironmentValueEx((PUNICODE_STRING)Name, VendorGuid, NULL, Length, Attributes);
    if (NT_SUCCESS(Status))
    {
        *Data = Mem_Alloc(1);
        RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, Previous, FALSE, &Previous);
        return *Data == NULL ? STATUS_NO_MEMORY : STATUS_SUCCESS;
    }
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
    {
        RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, Previous, FALSE, &Previous);
        return Status;
    }
    if (*Length > ZP_FIRMWARE_MAX_DATA_SIZE)
    {
        RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, Previous, FALSE, &Previous);
        return STATUS_QUOTA_EXCEEDED;
    }
    Capacity = *Length;
    *Data = Mem_Alloc(Capacity == 0 ? 1 : Capacity);
    if (*Data == NULL)
    {
        RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, Previous, FALSE, &Previous);
        return STATUS_NO_MEMORY;
    }
    Status = NtQuerySystemEnvironmentValueEx((PUNICODE_STRING)Name, VendorGuid, *Data, Length, Attributes);
    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, Previous, FALSE, &Previous);
    if (!NT_SUCCESS(Status))
    {
        RtlSecureZeroMemory(*Data, min(*Length, Capacity));
        Mem_Free(*Data);
    }
    return Status;
}

static
ULONG
ZpFirmware_FindBootOrder(
    _In_ USHORT Id,
    _In_reads_(Count) const USHORT* Order,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++) if (Order[Index] == Id) return Index;
    return MAXULONG;
}

static
_Success_(return != FALSE)
LOGICAL
ZpFirmware_ParseBootId(
    _In_ PCWSTR Name,
    _Out_ PUSHORT Id)
{
    ULONG Value = 0, Index;

    if (wcslen(Name) != 8 || wcsncmp(Name, L"Boot", 4) != 0) return FALSE;
    for (Index = 4; Index < 8; Index++)
    {
        WCHAR Character = Name[Index];

        Value <<= 4;
        if (Character >= L'0' && Character <= L'9') Value |= Character - L'0';
        else if (Character >= L'A' && Character <= L'F') Value |= Character - L'A' + 10;
        else if (Character >= L'a' && Character <= L'f') Value |= Character - L'a' + 10;
        else return FALSE;
    }
    *Id = (USHORT)Value;
    return TRUE;
}

static
NTSTATUS
ZpFirmware_AddBootEntry(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ PVARIABLE_NAME_AND_VALUE Variable,
    _In_ ULONG EntryLength,
    _In_ ULONG OrderIndex)
{
    PZP_FIRMWARE_LOAD_OPTION Option;
    ULONG ValueEnd, DescriptionBytes;
    PWSTR Identity;
    USHORT Id;
    NTSTATUS Status;

    if (!IsEqualGUID(&Variable->VendorGuid, &ZpFirmwareGlobalVariableGuid) ||
        !ZpFirmware_ParseBootId(Variable->Name, &Id) ||
        Variable->ValueOffset > EntryLength || Variable->ValueLength > EntryLength - Variable->ValueOffset ||
        Variable->ValueLength < FIELD_OFFSET(ZP_FIRMWARE_LOAD_OPTION, Description) + sizeof(WCHAR))
    {
        return STATUS_SUCCESS;
    }
    Option = (PZP_FIRMWARE_LOAD_OPTION)Add2Ptr(Variable, Variable->ValueOffset);
    ValueEnd = Variable->ValueLength - FIELD_OFFSET(ZP_FIRMWARE_LOAD_OPTION, Description);
    for (DescriptionBytes = 0;
         DescriptionBytes + sizeof(WCHAR) <= ValueEnd &&
         Option->Description[DescriptionBytes / sizeof(WCHAR)] != UNICODE_NULL;
         DescriptionBytes += sizeof(WCHAR));
    if (DescriptionBytes + sizeof(WCHAR) > ValueEnd) return STATUS_DATA_ERROR;
    if (FIELD_OFFSET(ZP_FIRMWARE_LOAD_OPTION, Description) + DescriptionBytes + sizeof(WCHAR) +
            Option->FilePathListLength >
        Variable->ValueLength)
    {
        return STATUS_DATA_ERROR;
    }
    Status = ZpFirmware_CreateVariableIdentity(&Variable->VendorGuid, Variable->Name, &Identity);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpAdministration_AddRecord(Builder,
                                         ZpAdministrationKindFirmwareBootEntry,
                                         Option->Attributes,
                                         OrderIndex,
                                         Id,
                                         Identity,
                                         Option->Description,
                                         Variable->Name,
                                         NULL);
    Mem_Free(Identity);
    return Status;
}

static
NTSTATUS
ZpFirmware_AddMissingBootEntry(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ USHORT Id,
    _In_ ULONG OrderIndex)
{
    WCHAR Name[9], Identity[80];
    UNICODE_STRING Guid;
    NTSTATUS Status;

    _snwprintf_s(Name, ARRAYSIZE(Name), _TRUNCATE, L"Boot%04X", Id);
    Status = RtlStringFromGUID(&ZpFirmwareGlobalVariableGuid, &Guid);
    if (!NT_SUCCESS(Status)) return Status;
    _snwprintf_s(Identity,
                 ARRAYSIZE(Identity),
                 _TRUNCATE,
                 ZP_FIRMWARE_VARIABLE_PREFIX L"%wZ:%s",
                 &Guid,
                 Name);
    RtlFreeUnicodeString(&Guid);
    return ZpAdministration_AddRecord(Builder,
                                      ZpAdministrationKindFirmwareBootEntry,
                                      0,
                                      OrderIndex | ZP_FIRMWARE_BOOT_ENTRY_MISSING,
                                      Id,
                                      Identity,
                                      NULL,
                                      Name,
                                      NULL);
}

static
ZP_STATUS
ZpAdministration_EnumerateFirmwareVariables(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PVARIABLE_NAME_AND_VALUE Variable;
    PBYTE Buffer = NULL;
    const USHORT* BootOrder = NULL;
    ULONG Length = 0, AllocatedLength = 0, Offset, EntryLength, OrderCount = 0;
    BOOLEAN Previous;
    NTSTATUS Status;

    Status = ZpFirmware_AdjustPrivilege(&Previous);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = NtEnumerateSystemEnvironmentValuesEx(SystemEnvironmentValueInformation, NULL, &Length);
    if (Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_BUFFER_OVERFLOW)
    {
        AllocatedLength = Length;
        Buffer = Length <= ZP_FIRMWARE_MAX_DATA_SIZE ? Mem_Alloc(Length) : NULL;
        Status = Buffer == NULL ?
                     (Length <= ZP_FIRMWARE_MAX_DATA_SIZE ? STATUS_NO_MEMORY : STATUS_QUOTA_EXCEEDED) :
                     NtEnumerateSystemEnvironmentValuesEx(SystemEnvironmentValueInformation, Buffer, &Length);
    }
    RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, Previous, FALSE, &Previous);
    for (Offset = 0; NT_SUCCESS(Status) && Offset < Length; Offset += EntryLength)
    {
        PWSTR Identity;

        Variable = (PVARIABLE_NAME_AND_VALUE)(Buffer + Offset);
        EntryLength = Variable->NextEntryOffset == 0 ? Length - Offset : Variable->NextEntryOffset;
        if (EntryLength < FIELD_OFFSET(VARIABLE_NAME_AND_VALUE, Name) + sizeof(WCHAR) ||
            EntryLength > Length - Offset ||
            Variable->ValueOffset < FIELD_OFFSET(VARIABLE_NAME_AND_VALUE, Name) + sizeof(WCHAR) ||
            Variable->ValueOffset > EntryLength || Variable->ValueLength > EntryLength - Variable->ValueOffset ||
            wcsnlen(Variable->Name,
                    (Variable->ValueOffset - FIELD_OFFSET(VARIABLE_NAME_AND_VALUE, Name)) / sizeof(WCHAR)) ==
                (Variable->ValueOffset - FIELD_OFFSET(VARIABLE_NAME_AND_VALUE, Name)) / sizeof(WCHAR))
        {
            Status = STATUS_DATA_ERROR;
            break;
        }
        Status = ZpFirmware_CreateVariableIdentity(&Variable->VendorGuid, Variable->Name, &Identity);
        if (!NT_SUCCESS(Status)) break;
        {
            UNICODE_STRING Guid;

            Status = RtlStringFromGUID(&Variable->VendorGuid, &Guid);
            if (NT_SUCCESS(Status))
            {
                Status = ZpAdministration_AddRecord(&Builder,
                                                     ZpAdministrationKindFirmwareVariable,
                                                     Variable->Attributes,
                                                     0,
                                                     Variable->ValueLength,
                                                     Identity,
                                                     Variable->Name,
                                                     Guid.Buffer,
                                                     NULL);
                RtlFreeUnicodeString(&Guid);
            }
        }
        Mem_Free(Identity);
        if (!NT_SUCCESS(Status)) break;
        if (IsEqualGUID(&Variable->VendorGuid, &ZpFirmwareGlobalVariableGuid) &&
            wcscmp(Variable->Name, L"BootOrder") == 0 &&
            Variable->ValueLength % sizeof(USHORT) == 0)
        {
            BootOrder = (const USHORT*)Add2Ptr(Variable, Variable->ValueOffset);
            OrderCount = Variable->ValueLength / sizeof(USHORT);
        }
        if (Variable->NextEntryOffset == 0) break;
    }
    if (NT_SUCCESS(Status))
    {
        ULONG OrderIndex;

        for (OrderIndex = 0; NT_SUCCESS(Status) && OrderIndex < OrderCount; OrderIndex++)
        {
            LOGICAL Found = FALSE;

            for (Offset = 0; Offset < Length; Offset += EntryLength)
            {
                USHORT Id;

                Variable = (PVARIABLE_NAME_AND_VALUE)(Buffer + Offset);
                EntryLength = Variable->NextEntryOffset == 0 ? Length - Offset : Variable->NextEntryOffset;
                if (IsEqualGUID(&Variable->VendorGuid, &ZpFirmwareGlobalVariableGuid) &&
                    ZpFirmware_ParseBootId(Variable->Name, &Id) && Id == BootOrder[OrderIndex])
                {
                    Status = ZpFirmware_AddBootEntry(&Builder, Variable, EntryLength, OrderIndex);
                    Found = TRUE;
                    break;
                }
                if (Variable->NextEntryOffset == 0) break;
            }
            if (NT_SUCCESS(Status) && !Found)
            {
                Status = ZpFirmware_AddMissingBootEntry(&Builder, BootOrder[OrderIndex], OrderIndex);
            }
        }
    }
    for (Offset = 0; NT_SUCCESS(Status) && Offset < Length; Offset += EntryLength)
    {
        USHORT Id;

        Variable = (PVARIABLE_NAME_AND_VALUE)(Buffer + Offset);
        EntryLength = Variable->NextEntryOffset == 0 ? Length - Offset : Variable->NextEntryOffset;
        if (IsEqualGUID(&Variable->VendorGuid, &ZpFirmwareGlobalVariableGuid) &&
            ZpFirmware_ParseBootId(Variable->Name, &Id) &&
            ZpFirmware_FindBootOrder(Id, BootOrder, OrderCount) == MAXULONG)
        {
            Status = ZpFirmware_AddBootEntry(&Builder, Variable, EntryLength, MAXULONG);
        }
        if (Variable->NextEntryOffset == 0) break;
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    if (Buffer != NULL)
    {
        RtlSecureZeroMemory(Buffer, min(Length, AllocatedLength));
        Mem_Free(Buffer);
    }
    return ZpStatus_FromNtStatus(Status);
}

static
NTSTATUS
ZpFirmware_QueryTable(
    _In_ ULONG Provider,
    _In_ SYSTEM_FIRMWARE_TABLE_ACTION Action,
    _In_ ULONG TableId,
    _Outptr_result_bytebuffer_(*Length) PBYTE* Data,
    _Out_ PULONG Length)
{
    PSYSTEM_FIRMWARE_TABLE_INFORMATION Information;
    ULONG Size = FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer), Required = 0;
    NTSTATUS Status;

    Information = Mem_Alloc(Size);
    if (Information == NULL) return STATUS_NO_MEMORY;
    Information->ProviderSignature = Provider;
    Information->Action = Action;
    Information->TableID = TableId;
    Information->TableBufferLength = 0;
    Status = NtQuerySystemInformation(SystemFirmwareTableInformation, Information, Size, &Required);
    if (Status == STATUS_INFO_LENGTH_MISMATCH || Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_BUFFER_OVERFLOW)
    {
        Required = max(Required,
                       FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer) +
                           Information->TableBufferLength);
        Mem_Free(Information);
        if (Required > ZP_FIRMWARE_MAX_DATA_SIZE + FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer))
        {
            return STATUS_QUOTA_EXCEEDED;
        }
        Information = Mem_Alloc(Required);
        if (Information == NULL) return STATUS_NO_MEMORY;
        Information->ProviderSignature = Provider;
        Information->Action = Action;
        Information->TableID = TableId;
        Information->TableBufferLength = Required - FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer);
        Status = NtQuerySystemInformation(SystemFirmwareTableInformation, Information, Required, &Size);
    }
    if (NT_SUCCESS(Status) &&
        Information->TableBufferLength > Size - FIELD_OFFSET(SYSTEM_FIRMWARE_TABLE_INFORMATION, TableBuffer))
    {
        Status = STATUS_DATA_ERROR;
    }
    if (NT_SUCCESS(Status))
    {
        *Length = Information->TableBufferLength;
        *Data = Mem_Alloc(*Length == 0 ? 1 : *Length);
        Status = *Data == NULL ? STATUS_NO_MEMORY : STATUS_SUCCESS;
        if (NT_SUCCESS(Status)) RtlCopyMemory(*Data, Information->TableBuffer, *Length);
    }
    Mem_Free(Information);
    return Status;
}

static
NTSTATUS
ZpFirmware_ParseTableIdentity(
    _In_ PCWSTR Text,
    _In_ PCWSTR Prefix,
    _Out_ PULONG Value)
{
    ULONG Result = 0, Index, PrefixLength = (ULONG)wcslen(Prefix);

    if (wcslen(Text) != PrefixLength + 8 || wcsncmp(Text, Prefix, PrefixLength) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    for (Index = PrefixLength; Index < PrefixLength + 8; Index++)
    {
        WCHAR Character = Text[Index];

        Result <<= 4;
        if (Character >= L'0' && Character <= L'9') Result |= Character - L'0';
        else if (Character >= L'A' && Character <= L'F') Result |= Character - L'A' + 10;
        else if (Character >= L'a' && Character <= L'f') Result |= Character - L'a' + 10;
        else return STATUS_INVALID_PARAMETER;
    }
    *Value = Result;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFirmware_GetSmbiosSnapshot(
    _Outptr_ const ZP_FIRMWARE_SMBIOS_RAW_DATA** Snapshot)
{
    PBYTE Data;
    ULONG Length;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlAcquireSRWLockExclusive(&ZpFirmwareSmbiosLock);
    if (ZpFirmwareSmbiosSnapshot == NULL)
    {
        Status = ZpFirmware_QueryTable(ZP_FIRMWARE_FOURCC('R', 'S', 'M', 'B'),
                                       SystemFirmwareTableGet,
                                       0,
                                       &Data,
                                       &Length);
        if (NT_SUCCESS(Status))
        {
            if (Length < (ULONG)FIELD_OFFSET(ZP_FIRMWARE_SMBIOS_RAW_DATA, Data) ||
                ((PZP_FIRMWARE_SMBIOS_RAW_DATA)Data)->Length >
                    Length - (ULONG)FIELD_OFFSET(ZP_FIRMWARE_SMBIOS_RAW_DATA, Data))
            {
                Status = STATUS_DATA_ERROR;
                Mem_Free(Data);
            }
            else
            {
                ZpFirmwareSmbiosSnapshot = (PZP_FIRMWARE_SMBIOS_RAW_DATA)Data;
            }
        }
    }
    if (NT_SUCCESS(Status)) *Snapshot = ZpFirmwareSmbiosSnapshot;
    RtlReleaseSRWLockExclusive(&ZpFirmwareSmbiosLock);
    return Status;
}

static
NTSTATUS
ZpFirmware_GetSmbiosStructure(
    _In_ const ZP_FIRMWARE_SMBIOS_RAW_DATA* Snapshot,
    _In_ ULONG Offset,
    _Out_ PZP_FIRMWARE_SMBIOS_STRUCTURE Structure)
{
    const ZP_FIRMWARE_SMBIOS_HEADER* Header;
    const BYTE* Data = Snapshot->Data;
    ULONG End;

    if (Offset == Snapshot->Length) return STATUS_NO_MORE_ENTRIES;
    if (Offset > Snapshot->Length || Snapshot->Length - Offset < sizeof(*Header)) return STATUS_DATA_ERROR;
    Header = (const ZP_FIRMWARE_SMBIOS_HEADER*)(Data + Offset);
    if (Header->Length < sizeof(*Header) || Header->Length > Snapshot->Length - Offset)
    {
        return STATUS_DATA_ERROR;
    }
    for (End = Offset + Header->Length; End + 1 < Snapshot->Length; End++)
    {
        if (Data[End] == 0 && Data[End + 1] == 0)
        {
            Structure->Header = Header;
            Structure->Offset = Offset;
            Structure->TotalLength = End + 2 - Offset;
            return STATUS_SUCCESS;
        }
    }
    return STATUS_DATA_ERROR;
}

static
NTSTATUS
ZpFirmware_CaptureCpuidRecord(
    _In_ ULONG Leaf,
    _In_ ULONG SubLeaf,
    _Out_ PZP_FIRMWARE_CPUID_RECORD Record)
{
    CPUID_INFO Information;
    FIRMWARE_DECODE_STATUS DecodeStatus;

    DecodeStatus = CpuidExecute(Leaf, SubLeaf, &Information);
    if (DecodeStatus != FirmwareDecodeSuccess)
    {
        return DecodeStatus == FirmwareDecodeUnsupported ? STATUS_NOT_SUPPORTED : STATUS_UNSUCCESSFUL;
    }
    Record->Leaf = Leaf;
    Record->SubLeaf = SubLeaf;
    Record->Eax = (ULONG)Information.Registers[0];
    Record->Ebx = (ULONG)Information.Registers[1];
    Record->Ecx = (ULONG)Information.Registers[2];
    Record->Edx = (ULONG)Information.Registers[3];
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpFirmware_BuildCpuid(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_FIRMWARE_CPUID_RECORD Records[10];
    PBYTE Data;
    ULONG Count = 0, Length, MaximumLeaf, MaximumSubLeaf, MaximumExtendedLeaf, SubLeaf;
    NTSTATUS Status;

    Status = ZpFirmware_CaptureCpuidRecord(0, 0, &Records[Count]);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    MaximumLeaf = Records[Count++].Eax;
    if (MaximumLeaf >= 1)
    {
        Status = ZpFirmware_CaptureCpuidRecord(1, 0, &Records[Count]);
        if (NT_SUCCESS(Status)) Count++;
    }
    if (NT_SUCCESS(Status) && MaximumLeaf >= 7)
    {
        Status = ZpFirmware_CaptureCpuidRecord(7, 0, &Records[Count]);
        if (NT_SUCCESS(Status))
        {
            MaximumSubLeaf = min(Records[Count++].Eax, 2);
            for (SubLeaf = 1; NT_SUCCESS(Status) && SubLeaf <= MaximumSubLeaf; SubLeaf++)
            {
                Status = ZpFirmware_CaptureCpuidRecord(7, SubLeaf, &Records[Count]);
                if (NT_SUCCESS(Status)) Count++;
            }
        }
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFirmware_CaptureCpuidRecord(0x80000000, 0, &Records[Count]);
        if (NT_SUCCESS(Status))
        {
            MaximumExtendedLeaf = Records[Count++].Eax;
            if (MaximumExtendedLeaf >= 0x80000001)
            {
                Status = ZpFirmware_CaptureCpuidRecord(0x80000001, 0, &Records[Count]);
                if (NT_SUCCESS(Status)) Count++;
            }
            for (SubLeaf = 0x80000002;
                 NT_SUCCESS(Status) && SubLeaf <= min(MaximumExtendedLeaf, 0x80000004);
                 SubLeaf++)
            {
                Status = ZpFirmware_CaptureCpuidRecord(SubLeaf, 0, &Records[Count]);
                if (NT_SUCCESS(Status)) Count++;
            }
        }
    }
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Length = Count * sizeof(Records[0]);
    Data = Mem_Alloc(Length);
    if (Data == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    RtlCopyMemory(Data, Records, Length);
    *Response = Data;
    *ResponseLength = Length;
    return ZpStatus_FromNtStatus(Status);
}

static
NTSTATUS
ZpFirmware_CopyCache(
    _In_reads_bytes_(Length) const BYTE* Data,
    _In_ ULONG Length,
    _Outptr_result_bytebuffer_(Length) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PBYTE Copy = Mem_Alloc(Length);

    if (Copy == NULL) return STATUS_NO_MEMORY;
    RtlCopyMemory(Copy, Data, Length);
    *Response = Copy;
    *ResponseLength = Length;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpFirmware_QueryCpuid(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_STATUS Result;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&ZpFirmwareCacheLock);
    if (ZpFirmwareCpuidResponse == NULL)
    {
        Result = ZpFirmware_BuildCpuid(&ZpFirmwareCpuidResponse,
                                      &ZpFirmwareCpuidResponseLength);
        if (!ZpStatus_IsSuccess(Result))
        {
            RtlReleaseSRWLockExclusive(&ZpFirmwareCacheLock);
            return Result;
        }
    }
    Status = ZpFirmware_CopyCache(ZpFirmwareCpuidResponse,
                                  ZpFirmwareCpuidResponseLength,
                                  Response,
                                  ResponseLength);
    RtlReleaseSRWLockExclusive(&ZpFirmwareCacheLock);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpFirmware_BuildAcpiDirectory(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PBYTE IdData;
    PULONG Ids;
    ULONG Length, Index, Prior;
    NTSTATUS Status;

    Status = ZpFirmware_QueryTable(ZP_FIRMWARE_FOURCC('A', 'C', 'P', 'I'),
                                   SystemFirmwareTableEnumerate,
                                   0,
                                   &IdData,
                                   &Length);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (Length % sizeof(ULONG) != 0) Status = STATUS_DATA_ERROR;
    Ids = (PULONG)IdData;
    for (Index = 0; NT_SUCCESS(Status) && Index < Length / sizeof(ULONG); Index++)
    {
        WCHAR Identity[14];
        WCHAR Signature[5];

        for (Prior = 0; Prior < Index && Ids[Prior] != Ids[Index]; Prior++);
        if (Prior != Index) continue;
        _snwprintf_s(Identity, ARRAYSIZE(Identity), _TRUNCATE, ZP_FIRMWARE_ACPI_PREFIX L"%08lX", Ids[Index]);
        Signature[0] = (WCHAR)(BYTE)Ids[Index];
        Signature[1] = (WCHAR)(BYTE)(Ids[Index] >> 8);
        Signature[2] = (WCHAR)(BYTE)(Ids[Index] >> 16);
        Signature[3] = (WCHAR)(BYTE)(Ids[Index] >> 24);
        Signature[4] = UNICODE_NULL;
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindAcpiTable,
                                             0,
                                             0,
                                             0,
                                             Identity,
                                             Signature,
                                             NULL,
                                             NULL);
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    Mem_Free(IdData);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpFirmware_QueryAcpi(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_STATUS Result;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&ZpFirmwareCacheLock);
    if (ZpFirmwareAcpiDirectoryResponse == NULL)
    {
        Result = ZpFirmware_BuildAcpiDirectory(&ZpFirmwareAcpiDirectoryResponse,
                                               &ZpFirmwareAcpiDirectoryResponseLength);
        if (!ZpStatus_IsSuccess(Result))
        {
            RtlReleaseSRWLockExclusive(&ZpFirmwareCacheLock);
            return Result;
        }
    }
    Status = ZpFirmware_CopyCache(ZpFirmwareAcpiDirectoryResponse,
                                  ZpFirmwareAcpiDirectoryResponseLength,
                                  Response,
                                  ResponseLength);
    RtlReleaseSRWLockExclusive(&ZpFirmwareCacheLock);
    return ZpStatus_FromNtStatus(Status);
}

static
NTSTATUS
ZpFirmware_QueryAcpiTable(
    _In_ ULONG TableId,
    _Outptr_result_bytebuffer_(*Length) PBYTE* Data,
    _Out_ PULONG Length)
{
    PZP_FIRMWARE_ACPI_CACHE_ENTRY Entries, Entry = NULL;
    ULONG Index;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&ZpFirmwareCacheLock);
    for (Index = 0; Index < ZpFirmwareAcpiTableCount; Index++)
    {
        if (ZpFirmwareAcpiTables[Index].TableId == TableId)
        {
            Entry = &ZpFirmwareAcpiTables[Index];
            break;
        }
    }
    if (Entry == NULL)
    {
        Entries = Mem_ReAlloc(ZpFirmwareAcpiTables,
                              (SIZE_T)(ZpFirmwareAcpiTableCount + 1) * sizeof(*Entries));
        if (Entries == NULL)
        {
            RtlReleaseSRWLockExclusive(&ZpFirmwareCacheLock);
            return STATUS_NO_MEMORY;
        }
        ZpFirmwareAcpiTables = Entries;
        Entry = &Entries[ZpFirmwareAcpiTableCount];
        Status = ZpFirmware_QueryTable(ZP_FIRMWARE_FOURCC('A', 'C', 'P', 'I'),
                                       SystemFirmwareTableGet,
                                       TableId,
                                       &Entry->Data,
                                       &Entry->Length);
        if (!NT_SUCCESS(Status))
        {
            RtlReleaseSRWLockExclusive(&ZpFirmwareCacheLock);
            return Status;
        }
        Entry->TableId = TableId;
        ZpFirmwareAcpiTableCount++;
    }
    Status = ZpFirmware_CopyCache(Entry->Data, Entry->Length, Data, Length);
    RtlReleaseSRWLockExclusive(&ZpFirmwareCacheLock);
    return Status;
}

static
ZP_STATUS
ZpFirmware_QuerySmbios(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    const ZP_FIRMWARE_SMBIOS_RAW_DATA* Snapshot;
    ZP_FIRMWARE_SMBIOS_STRUCTURE Structure;
    ULONG Offset = 0, State;
    WCHAR Identity[16];
    NTSTATUS Status;

    Status = ZpFirmware_GetSmbiosSnapshot(&Snapshot);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    State = ((ULONG)Snapshot->DmiRevision << 24) | ((ULONG)Snapshot->MinorVersion << 16) |
            ((ULONG)Snapshot->MajorVersion << 8);
    while (NT_SUCCESS(Status = ZpFirmware_GetSmbiosStructure(Snapshot, Offset, &Structure)))
    {
        _snwprintf_s(Identity, ARRAYSIZE(Identity), _TRUNCATE, ZP_FIRMWARE_SMBIOS_PREFIX L"%08lX", Offset);
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindSmbiosTable,
                                             State | Structure.Header->Type,
                                             Structure.Header->Handle,
                                             ((ULONGLONG)Structure.Header->Length << 32) | Structure.TotalLength,
                                             Identity,
                                             NULL,
                                             NULL,
                                             NULL);
        if (!NT_SUCCESS(Status)) break;
        Offset += Structure.TotalLength;
    }
    if (Status == STATUS_NO_MORE_ENTRIES) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
NTSTATUS
ZpFirmware_QuerySmbiosStructure(
    _In_ ULONG RequestedOffset,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    const ZP_FIRMWARE_SMBIOS_RAW_DATA* Snapshot;
    ZP_FIRMWARE_SMBIOS_STRUCTURE Structure;
    ULONG Offset = 0;
    NTSTATUS Status;

    Status = ZpFirmware_GetSmbiosSnapshot(&Snapshot);
    while (NT_SUCCESS(Status))
    {
        Status = ZpFirmware_GetSmbiosStructure(Snapshot, Offset, &Structure);
        if (!NT_SUCCESS(Status) || Offset == RequestedOffset) break;
        Offset += Structure.TotalLength;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFirmware_CopyCache(Snapshot->Data + Offset,
                                      Structure.TotalLength,
                                      Response,
                                      ResponseLength);
    }
    else if (Status == STATUS_NO_MORE_ENTRIES)
    {
        Status = STATUS_NOT_FOUND;
    }
    return Status;
}

static
ZP_STATUS
ZpAdministration_QueryFirmware(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PWSTR Text = ZpAdministration_CopyView(Identity);
    ZP_STATUS Status;

    if (Text == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    if (wcscmp(Text, ZP_FIRMWARE_ACPI_IDENTITY) == 0)
    {
        Status = ZpFirmware_QueryAcpi(Response, ResponseLength);
    }
    else if (wcscmp(Text, ZP_FIRMWARE_SMBIOS_IDENTITY) == 0)
    {
        Status = ZpFirmware_QuerySmbios(Response, ResponseLength);
    }
    else
    {
        Status = ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Mem_Free(Text);
    return Status;
}

static
ZP_STATUS
ZpAdministration_QueryFirmwareData(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PBYTE Data;
    ULONG Length, TableId;
    PWSTR Text = ZpAdministration_CopyView(Identity), VariableBuffer;
    UNICODE_STRING Name;
    GUID VendorGuid;
    NTSTATUS Status;

    if (Text == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    if (wcscmp(Text, ZP_FIRMWARE_CPUID_IDENTITY) == 0)
    {
        Mem_Free(Text);
        return ZpFirmware_QueryCpuid(Response, ResponseLength);
    }
    if (wcsncmp(Text, ZP_FIRMWARE_VARIABLE_PREFIX, RTL_NUMBER_OF(ZP_FIRMWARE_VARIABLE_PREFIX) - 1) == 0)
    {
        Status = ZpFirmware_ParseVariableIdentity(Identity, &VendorGuid, &Name, &VariableBuffer);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Text);
            return ZpStatus_FromNtStatus(Status);
        }
        Status = ZpFirmware_QueryVariable(&Name, &VendorGuid, &Data, &Length, NULL);
        Mem_Free(VariableBuffer);
        Mem_Free(Text);
        if (NT_SUCCESS(Status))
        {
            *Response = Data;
            *ResponseLength = Length;
        }
        return ZpStatus_FromNtStatus(Status);
    }
    if (NT_SUCCESS(Status = ZpFirmware_ParseTableIdentity(Text, ZP_FIRMWARE_SMBIOS_PREFIX, &TableId)))
    {
        Status = ZpFirmware_QuerySmbiosStructure(TableId, &Data, &Length);
        Mem_Free(Text);
        if (NT_SUCCESS(Status))
        {
            *Response = Data;
            *ResponseLength = Length;
        }
        return ZpStatus_FromNtStatus(Status);
    }
    if (NT_SUCCESS(Status = ZpFirmware_ParseTableIdentity(Text, ZP_FIRMWARE_ACPI_PREFIX, &TableId)))
    {
        Status = ZpFirmware_QueryAcpiTable(TableId, &Data, &Length);
        Mem_Free(Text);
        if (NT_SUCCESS(Status))
        {
            *Response = Data;
            *ResponseLength = Length;
        }
        return ZpStatus_FromNtStatus(Status);
    }
    Mem_Free(Text);
    return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
}

static
ZP_STATUS
ZpAdministration_ControlFirmwareData(
    _In_ PCZP_ADMINISTRATION_DATA_CONTROL_VIEW Control)
{
    GUID VendorGuid;
    UNICODE_STRING Name;
    ZP_STRING_VIEW Identity;
    PWSTR VariableBuffer;
    PBYTE Existing = NULL;
    ULONG ExistingLength;
    BOOLEAN Previous;
    NTSTATUS Status;

    if (Control->Action != ZpAdministrationActionCreate &&
        Control->Action != ZpAdministrationActionConfigure &&
        Control->Action != ZpAdministrationActionDelete)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Status = ZpAdministration_GetDataControlIdentityString(Control, &Identity);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = ZpFirmware_ParseVariableIdentity(&Identity, &VendorGuid, &Name, &VariableBuffer);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (Control->Data.Length > ZP_FIRMWARE_MAX_DATA_SIZE)
    {
        Mem_Free(VariableBuffer);
        return ZpStatus_FromNtStatus(STATUS_QUOTA_EXCEEDED);
    }
    if (Control->Action == ZpAdministrationActionCreate || Control->Action == ZpAdministrationActionConfigure)
    {
        Status = ZpFirmware_QueryVariable(&Name, &VendorGuid, &Existing, &ExistingLength, NULL);
        if (Control->Action == ZpAdministrationActionCreate)
        {
            Status = NT_SUCCESS(Status) ? STATUS_OBJECT_NAME_COLLISION :
                         (Status == STATUS_VARIABLE_NOT_FOUND ? STATUS_SUCCESS : Status);
        }
        if (Control->Action == ZpAdministrationActionConfigure && Status == STATUS_VARIABLE_NOT_FOUND)
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
        if (Existing != NULL)
        {
            RtlSecureZeroMemory(Existing, ExistingLength);
            Mem_Free(Existing);
        }
    }
    if (Control->Action == ZpAdministrationActionDelete) Status = STATUS_SUCCESS;
    if (NT_SUCCESS(Status)) Status = ZpFirmware_AdjustPrivilege(&Previous);
    if (NT_SUCCESS(Status))
    {
        Status = NtSetSystemEnvironmentValueEx(&Name,
                                               &VendorGuid,
                                               (PVOID)Control->Data.Buffer,
                                               Control->Action == ZpAdministrationActionDelete ?
                                                   0 : Control->Data.Length,
                                               Control->Flags);
        RtlAdjustPrivilege(SE_SYSTEM_ENVIRONMENT_PRIVILEGE, Previous, FALSE, &Previous);
    }
    Mem_Free(VariableBuffer);
    return ZpStatus_FromNtStatus(Status);
}
