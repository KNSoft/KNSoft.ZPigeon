#pragma once

#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <KNSoft/ZPigeon/SDK.h>

static
NTSTATUS
ZpConfig_AddSize(
    _Inout_ PSIZE_T Size,
    _In_ SIZE_T Addition)
{
    if (*Size > MAXSIZE_T - Addition)
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    *Size += Addition;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpConfig_GetStringSize(
    _In_opt_ PCWSTR String,
    _In_ LOGICAL Required,
    _Out_ PSIZE_T Size)
{
    NTSTATUS Status;
    UNICODE_STRING UnicodeString;

    if (String == NULL)
    {
        if (Required)
        {
            return STATUS_INVALID_PARAMETER;
        }
        *Size = 0;
        return STATUS_SUCCESS;
    }
    Status = RtlInitUnicodeStringEx(&UnicodeString, String);
    if (!NT_SUCCESS(Status) || (Required && UnicodeString.Length == 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    *Size = UnicodeString.Length + sizeof(UNICODE_NULL);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpConfig_AddStringSize(
    _Inout_ PSIZE_T AllocationSize,
    _In_opt_ PCWSTR String,
    _In_ LOGICAL Required,
    _Out_ PSIZE_T StringSize)
{
    NTSTATUS Status;

    Status = ZpConfig_GetStringSize(String, Required, StringSize);
    if (NT_SUCCESS(Status))
    {
        Status = ZpConfig_AddSize(AllocationSize, *StringSize);
    }
    return Status;
}

static
VOID
ZpConfig_CopyString(
    _Inout_ PBYTE* Cursor,
    _In_opt_ PCWSTR Source,
    _Out_ PCWSTR* Destination)
{
    UNICODE_STRING UnicodeString;
    SIZE_T Size;

    if (Source == NULL)
    {
        *Destination = NULL;
        return;
    }
    RtlInitUnicodeString(&UnicodeString, Source);
    Size = UnicodeString.Length + sizeof(UNICODE_NULL);
    RtlCopyMemory(*Cursor, Source, Size);
    *Destination = (PCWSTR)*Cursor;
    *Cursor += Size;
}

static
LOGICAL
ZpConfig_IsTransportValid(
    _In_ ZP_TRANSPORT_TYPE Transport)
{
    return Transport >= ZpTransportQuic && Transport < ZpTransportCount;
}

static
LOGICAL
ZpConfig_AreModulesValid(
    _In_reads_(ModuleCount) PCZP_MODULE_VERSION Modules,
    _In_ BYTE ModuleCount)
{
    BYTE Index, PreviousId = 0;

    if (Modules == NULL || ModuleCount == 0 || ModuleCount > ZP_MODULE_MAX_ID)
    {
        return FALSE;
    }
    for (Index = 0; Index < ModuleCount; Index++)
    {
        if (Modules[Index].ModuleId <= PreviousId ||
            Modules[Index].ModuleId > ZP_MODULE_MAX_ID ||
            Modules[Index].Version == 0)
        {
            return FALSE;
        }
        PreviousId = Modules[Index].ModuleId;
    }
    return TRUE;
}
