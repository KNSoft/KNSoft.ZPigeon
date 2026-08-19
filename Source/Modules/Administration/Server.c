#include <KNSoft/ZPigeon/Server.h>

#include <KNSoft/MakeLifeEasier/Memory/Core.h>

typedef union _ZP_ADMINISTRATION_CALLBACK
{
    ZP_ADMINISTRATION_ENUMERATE_CALLBACK Enumerate;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_ADMINISTRATION_CALLBACK;

typedef struct _ZP_ADMINISTRATION_CONTEXT
{
    ZP_ADMINISTRATION_CALLBACK Callback;
    PVOID Context;
} ZP_ADMINISTRATION_CONTEXT, *PZP_ADMINISTRATION_CONTEXT;

static
VOID
NTAPI
ZpAdministration_EnumerateComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_ADMINISTRATION_CONTEXT AdministrationContext = Context;
    ZP_ADMINISTRATION_LIST_VIEW Records;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(
            ZpAdministration_DecodeList(Payload->Buffer, Payload->Length, &Records));
    }
    AdministrationContext->Callback.Enumerate(
        Request,
        Status,
        ZpStatus_IsSuccess(Status) ? &Records : NULL,
        AdministrationContext->Context);
    Mem_Free(AdministrationContext);
}

static
VOID
NTAPI
ZpAdministration_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_ADMINISTRATION_CONTEXT AdministrationContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    AdministrationContext->Callback.Status(Request, Status, AdministrationContext->Context);
    Mem_Free(AdministrationContext);
}

static
NTSTATUS
ZpAdministration_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Complete,
    _In_ ZP_ADMINISTRATION_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_ADMINISTRATION_CONTEXT AdministrationContext;
    NTSTATUS Status;

    AdministrationContext = Mem_Alloc(sizeof(*AdministrationContext));
    if (AdministrationContext == NULL) return STATUS_NO_MEMORY;
    AdministrationContext->Callback = Callback;
    AdministrationContext->Context = Context;
    Status = ZpServer_SendRequest(Connection,
                                  ZP_ADMINISTRATION_MODULE_ID,
                                  OperationId,
                                  TimeoutMilliseconds,
                                  Payload,
                                  PayloadLength,
                                  Complete,
                                  AdministrationContext,
                                  Request);
    if (!NT_SUCCESS(Status)) Mem_Free(AdministrationContext);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_EnumerateAdministration(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_ADMINISTRATION_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_ADMINISTRATION_CALLBACK AdministrationCallback;

    if (Callback == NULL ||
        (OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_USERS &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_SOFTWARE &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_HARDWARE &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_UPDATES &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_TASKS &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_FIREWALL &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_POWER &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_FEATURES &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_SYSTEM &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_WLAN &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_CERTIFICATES &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_CLIPBOARD &&
         OperationId != ZP_ADMINISTRATION_OPERATION_ENUMERATE_CREDENTIALS))
    {
        return STATUS_INVALID_PARAMETER;
    }
    AdministrationCallback.Enumerate = Callback;
    return ZpAdministration_Send(Connection,
                                 OperationId,
                                 TimeoutMilliseconds,
                                 NULL,
                                 0,
                                 ZpAdministration_EnumerateComplete,
                                 AdministrationCallback,
                                 Context,
                                 Request);
}

NTSTATUS
NTAPI
ZpServer_QueryAdministration(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_reads_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_ADMINISTRATION_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_ADMINISTRATION_CALLBACK AdministrationCallback;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL ||
        (OperationId != ZP_ADMINISTRATION_OPERATION_QUERY_CERTIFICATE &&
         OperationId != ZP_ADMINISTRATION_OPERATION_WAIT_CLIPBOARD &&
         OperationId != ZP_ADMINISTRATION_OPERATION_QUERY_WLAN_PROFILE &&
         OperationId != ZP_ADMINISTRATION_OPERATION_QUERY_CREDENTIAL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpAdministration_EncodeQuery(Identity, IdentityLength, NULL, 0, &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpAdministration_EncodeQuery(Identity,
                                           IdentityLength,
                                           Payload,
                                           PayloadLength,
                                           &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        AdministrationCallback.Enumerate = Callback;
        Status = ZpAdministration_Send(Connection,
                                       OperationId,
                                       TimeoutMilliseconds,
                                       Payload,
                                       PayloadLength,
                                       ZpAdministration_EnumerateComplete,
                                       AdministrationCallback,
                                       Context,
                                       Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_ControlAdministration(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ USHORT OperationId,
    _In_ ZP_ADMINISTRATION_ACTION Action,
    _In_reads_opt_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_reads_opt_(SecretLength) PCWCH Secret,
    _In_ ULONG SecretLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    ZP_ADMINISTRATION_CALLBACK AdministrationCallback;
    PBYTE Payload;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL ||
        (OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_USER &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_SOFTWARE &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_HARDWARE &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_UPDATE &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_TASK &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_FIREWALL &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_POWER &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_FEATURE &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_SYSTEM &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_WLAN &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_CERTIFICATE &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_CLIPBOARD &&
         OperationId != ZP_ADMINISTRATION_OPERATION_CONTROL_CREDENTIAL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ZpAdministration_EncodeControl(Action,
                                             Identity,
                                             IdentityLength,
                                             Argument,
                                             ArgumentLength,
                                             Secret,
                                             SecretLength,
                                             NULL,
                                             0,
                                             &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (!NT_SUCCESS(Status) || Payload == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpAdministration_EncodeControl(Action,
                                             Identity,
                                             IdentityLength,
                                             Argument,
                                             ArgumentLength,
                                             Secret,
                                             SecretLength,
                                             Payload,
                                             PayloadLength,
                                             &PayloadLength);
    if (NT_SUCCESS(Status))
    {
        AdministrationCallback.Status = Callback;
        Status = ZpAdministration_Send(Connection,
                                       OperationId,
                                       TimeoutMilliseconds,
                                       Payload,
                                       PayloadLength,
                                       ZpAdministration_StatusComplete,
                                       AdministrationCallback,
                                       Context,
                                       Request);
    }
    RtlSecureZeroMemory(Payload, PayloadLength);
    Mem_Free(Payload);
    return Status;
}
