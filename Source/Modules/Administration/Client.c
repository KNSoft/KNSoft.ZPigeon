#include "Client.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#define COBJMACROS
#include <netfw.h>
#include <oleauto.h>
#include <powrprof.h>
#include <wlanapi.h>
#include <KNSoft/NDK/NT/Win32K/Win32KApi.h>

typedef struct _ZP_ADMINISTRATION_BUILDER
{
    PZP_ADMINISTRATION_RECORD Records;
    ULONG Count;
    ULONG Capacity;
} ZP_ADMINISTRATION_BUILDER, *PZP_ADMINISTRATION_BUILDER;

static
NTSTATUS
ZpAdministration_AddRecord(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ ZP_ADMINISTRATION_KIND Kind,
    _In_ ULONG State,
    _In_ ULONG Flags,
    _In_ ULONGLONG Value,
    _In_ PCWSTR Identity,
    _In_opt_ PCWSTR Name,
    _In_opt_ PCWSTR Description,
    _In_opt_ PCWSTR Detail)
{
    ULONG IdentityLength = (ULONG)wcslen(Identity);
    ULONG NameLength = Name == NULL ? 0 : (ULONG)wcslen(Name);
    ULONG DescriptionLength = Description == NULL ? 0 : (ULONG)wcslen(Description);
    ULONG DetailLength = Detail == NULL ? 0 : (ULONG)wcslen(Detail);
    SIZE_T CharacterCount = (SIZE_T)IdentityLength + NameLength + DescriptionLength + DetailLength + 4;
    PZP_ADMINISTRATION_RECORD Records;
    PZP_ADMINISTRATION_RECORD Record;
    PWCHAR Strings, Cursor;

    if (Builder->Count == ZP_CODEC_MAX_ELEMENT_COUNT || CharacterCount > MAXULONG)
    {
        return STATUS_QUOTA_EXCEEDED;
    }
    if (Builder->Count == Builder->Capacity)
    {
        ULONG Capacity = Builder->Capacity == 0 ? 16 : min(Builder->Capacity * 2, ZP_CODEC_MAX_ELEMENT_COUNT);

        Records = Mem_ReAlloc(Builder->Records, (SIZE_T)Capacity * sizeof(*Records));
        if (Records == NULL) return STATUS_NO_MEMORY;
        Builder->Records = Records;
        Builder->Capacity = Capacity;
    }
    Strings = Mem_Alloc(CharacterCount * sizeof(WCHAR));
    if (Strings == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Record = &Builder->Records[Builder->Count++];
    Record->Kind = Kind;
    Record->State = State;
    Record->Flags = Flags;
    Record->Value = Value;
    Cursor = Strings;
#define ZP_ADMINISTRATION_COPY_STRING(Field, Source, Len) \
    Record->Field = Cursor; \
    Record->Field##Length = Len; \
    if (Len != 0) RtlCopyMemory(Cursor, Source, (SIZE_T)Len * sizeof(WCHAR)); \
    Cursor[Len] = UNICODE_NULL; \
    Cursor += (SIZE_T)Len + 1
    ZP_ADMINISTRATION_COPY_STRING(Identity, Identity, IdentityLength);
    ZP_ADMINISTRATION_COPY_STRING(Name, Name, NameLength);
    ZP_ADMINISTRATION_COPY_STRING(Description, Description, DescriptionLength);
    ZP_ADMINISTRATION_COPY_STRING(Detail, Detail, DetailLength);
#undef ZP_ADMINISTRATION_COPY_STRING
    return STATUS_SUCCESS;
}

static
VOID
ZpAdministration_FreeBuilder(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    ULONG Index;

    for (Index = 0; Index < Builder->Count; Index++)
    {
        Mem_Free((PVOID)Builder->Records[Index].Identity);
    }
    Mem_Free(Builder->Records);
}

static
NTSTATUS
ZpAdministration_EncodeBuilder(
    _In_ PZP_ADMINISTRATION_BUILDER Builder,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    NTSTATUS Status;

    Status = ZpAdministration_EncodeList(Builder->Records, Builder->Count, NULL, 0, ResponseLength);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (!NT_SUCCESS(Status) || *Response == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpAdministration_EncodeList(
        Builder->Records,
        Builder->Count,
        *Response,
        *ResponseLength,
        ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(*Response);
        *Response = NULL;
    }
    return Status;
}

static
PWSTR
ZpAdministration_CopyView(
    _In_ PCZP_STRING_VIEW View)
{
    PWSTR String = Mem_Alloc(((SIZE_T)View->Length + 1) * sizeof(WCHAR));

    if (String != NULL)
    {
        RtlCopyMemory(String, View->Buffer, (SIZE_T)View->Length * sizeof(WCHAR));
        String[View->Length] = UNICODE_NULL;
    }
    return String;
}

static
ULONGLONG
ZpAdministration_DateToFileTime(
    _In_ DATE Date)
{
    SYSTEMTIME SystemTime;
    FILETIME FileTime;

    return Date != 0 && VariantTimeToSystemTime(Date, &SystemTime) &&
           SystemTimeToFileTime(&SystemTime, &FileTime) ?
               ((ULONGLONG)FileTime.dwHighDateTime << 32) | FileTime.dwLowDateTime :
               0;
}

typedef const VOID* PCVOID;

#include "User.inl"
#include "Hardware.inl"
#include "Software.inl"
#include "Update.inl"
#include "Task.inl"
#include "Firewall.inl"
#include "Power.inl"
#include "System.inl"
#include "Wlan.inl"
#include "Certificate.inl"

ZP_STATUS
ZpAdministration_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_CONTROL_VIEW Control;
    ZP_STRING_VIEW Identity;
    NTSTATUS Status;

    switch (OperationId)
    {
        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_USERS:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateUsers(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_SOFTWARE:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateSoftware(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_HARDWARE:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateHardware(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_UPDATES:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateUpdates(FALSE, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_TASKS:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateTasks(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_FIREWALL:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateFirewall(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_POWER:
            return RequestLength == 0 ?
                       ZpAdministration_EnumeratePower(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_FEATURES:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateFeatures(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_SYSTEM:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateSystem(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_WLAN:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateWlan(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_ENUMERATE_CERTIFICATES:
            return RequestLength == 0 ?
                       ZpAdministration_EnumerateCertificates(Response, ResponseLength) :
                       ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);

        case ZP_ADMINISTRATION_OPERATION_QUERY_CERTIFICATE:
            Status = ZpAdministration_DecodeQuery(Request, RequestLength, &Identity);
            return NT_SUCCESS(Status) ?
                       ZpAdministration_QueryCertificate(&Identity, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(Status);
    }
    Status = ZpAdministration_DecodeControl(Request, RequestLength, &Control);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    switch (OperationId)
    {
        case ZP_ADMINISTRATION_OPERATION_CONTROL_USER:
            return ZpAdministration_ControlUser(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_SOFTWARE:
            return ZpAdministration_ControlSoftware(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_HARDWARE:
            return ZpAdministration_ControlHardware(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_UPDATE:
            return ZpAdministration_ControlUpdate(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_TASK:
            return ZpAdministration_ControlTask(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_FIREWALL:
            return ZpAdministration_ControlFirewall(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_POWER:
            return ZpAdministration_ControlPower(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_FEATURE:
            return ZpAdministration_ControlFeature(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_SYSTEM:
            return ZpAdministration_ControlSystem(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_WLAN:
            return ZpAdministration_ControlWlan(&Control);

        case ZP_ADMINISTRATION_OPERATION_CONTROL_CERTIFICATE:
            return ZpAdministration_ControlCertificate(&Control);
    }
    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
}
