#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <KNSoft/ZPigeon/Server.h>
#include "../../KNSoft.ZPigeon.Server.SDK/Core/Channel.h"

typedef union _ZP_SERVER_PORTABLE_CALLBACK
{
    ZP_PORTABLE_DEVICES_CALLBACK Devices;
    ZP_PORTABLE_OBJECTS_CALLBACK Objects;
    ZP_REQUEST_STATUS_CALLBACK Status;
} ZP_SERVER_PORTABLE_CALLBACK;

typedef struct _ZP_SERVER_PORTABLE_CONTEXT
{
    ZP_SERVER_PORTABLE_CALLBACK Callback;
    PVOID Context;
} ZP_SERVER_PORTABLE_CONTEXT, *PZP_SERVER_PORTABLE_CONTEXT;

typedef struct _ZP_SERVER_PORTABLE_OPEN_READ_CONTEXT
{
    ZP_CONNECTION_HANDLE Connection;
    ZP_FILE_OPEN_READ_CALLBACK OpenCallback;
    ZP_CHANNEL_DATA_CALLBACK DataCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_SERVER_PORTABLE_OPEN_READ_CONTEXT, *PZP_SERVER_PORTABLE_OPEN_READ_CONTEXT;

typedef struct _ZP_SERVER_PORTABLE_OPEN_WRITE_CONTEXT
{
    ZP_CONNECTION_HANDLE Connection;
    ZP_FILE_OPEN_WRITE_CALLBACK OpenCallback;
    ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback;
    ZP_CHANNEL_CLOSE_CALLBACK CloseCallback;
    PVOID Context;
} ZP_SERVER_PORTABLE_OPEN_WRITE_CONTEXT, *PZP_SERVER_PORTABLE_OPEN_WRITE_CONTEXT;

static
NTSTATUS
ZpServerPortable_Send(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_ ULONG TimeoutMilliseconds,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ZP_REQUEST_COMPLETE_CALLBACK Callback,
    _In_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpServer_SendRequest(Connection,
                                ZP_PORTABLE_DEVICE_MODULE_ID,
                                OperationId,
                                TimeoutMilliseconds,
                                Payload,
                                PayloadLength,
                                Callback,
                                Context,
                                Request);
}

static
VOID
NTAPI
ZpServerPortable_DevicesComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_PORTABLE_CONTEXT PortableContext = Context;
    ZP_PORTABLE_DEVICE_LIST_VIEW Devices;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpPortable_DecodeDeviceList(Payload->Buffer, Payload->Length, &Devices));
    }
    PortableContext->Callback.Devices(Request,
                                      Status,
                                      ZpStatus_IsSuccess(Status) ? &Devices : NULL,
                                      PortableContext->Context);
    Mem_Free(PortableContext);
}

NTSTATUS
NTAPI
ZpServer_EnumeratePortableDevices(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PORTABLE_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_PORTABLE_CONTEXT PortableContext;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    PortableContext = Mem_Alloc(sizeof(*PortableContext));
    if (PortableContext == NULL) return STATUS_NO_MEMORY;
    PortableContext->Callback.Devices = Callback;
    PortableContext->Context = Context;
    Status = ZpServerPortable_Send(Connection,
                                   ZP_PORTABLE_DEVICE_OPERATION_ENUMERATE_DEVICES,
                                   TimeoutMilliseconds,
                                   NULL,
                                   0,
                                   ZpServerPortable_DevicesComplete,
                                   PortableContext,
                                   Request);
    if (!NT_SUCCESS(Status)) Mem_Free(PortableContext);
    return Status;
}

static
VOID
NTAPI
ZpServerPortable_ObjectsComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_PORTABLE_CONTEXT PortableContext = Context;
    ZP_PORTABLE_OBJECT_PAGE_VIEW Objects;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpPortable_DecodeObjectPage(Payload->Buffer, Payload->Length, &Objects));
    }
    PortableContext->Callback.Objects(Request,
                                      Status,
                                      ZpStatus_IsSuccess(Status) ? &Objects : NULL,
                                      PortableContext->Context);
    Mem_Free(PortableContext);
}

NTSTATUS
NTAPI
ZpServer_EnumeratePortableObjects(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_opt_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_ ULONG Offset,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_PORTABLE_OBJECTS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_PORTABLE_CONTEXT PortableContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpPortable_EncodeObjectPageRequest(DeviceId,
                                                DeviceIdLength,
                                                ParentId,
                                                ParentIdLength,
                                                Offset,
                                                NULL,
                                                0,
                                                &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpPortable_EncodeObjectPageRequest(DeviceId,
                                                    DeviceIdLength,
                                                    ParentId,
                                                    ParentIdLength,
                                                    Offset,
                                                    Payload,
                                                    PayloadLength,
                                                    &PayloadLength);
    }
    PortableContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*PortableContext)) : NULL;
    if (NT_SUCCESS(Status) && PortableContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        PortableContext->Callback.Objects = Callback;
        PortableContext->Context = Context;
        Status = ZpServerPortable_Send(Connection,
                                       ZP_PORTABLE_DEVICE_OPERATION_ENUMERATE_OBJECTS,
                                       TimeoutMilliseconds,
                                       Payload,
                                       PayloadLength,
                                       ZpServerPortable_ObjectsComplete,
                                       PortableContext,
                                       Request);
        if (!NT_SUCCESS(Status)) Mem_Free(PortableContext);
    }
    Mem_Free(Payload);
    return Status;
}

static
VOID
NTAPI
ZpServerPortable_StatusComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_PORTABLE_CONTEXT PortableContext = Context;

    if (ZpStatus_IsSuccess(Status) && Payload->Length != 0)
    {
        Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    PortableContext->Callback.Status(Request, Status, PortableContext->Context);
    Mem_Free(PortableContext);
}

static
NTSTATUS
ZpServerPortable_SendStatus(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_PORTABLE_CONTEXT PortableContext;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    PortableContext = Mem_Alloc(sizeof(*PortableContext));
    if (PortableContext == NULL) return STATUS_NO_MEMORY;
    PortableContext->Callback.Status = Callback;
    PortableContext->Context = Context;
    Status = ZpServerPortable_Send(Connection,
                                   OperationId,
                                   TimeoutMilliseconds,
                                   Payload,
                                   PayloadLength,
                                   ZpServerPortable_StatusComplete,
                                   PortableContext,
                                   Request);
    if (!NT_SUCCESS(Status)) Mem_Free(PortableContext);
    return Status;
}

static
NTSTATUS
ZpServerPortable_NameOperation(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ BYTE OperationId,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    Status = ZpPortable_EncodeNameRequest(DeviceId,
                                          DeviceIdLength,
                                          ObjectId,
                                          ObjectIdLength,
                                          Name,
                                          NameLength,
                                          NULL,
                                          0,
                                          &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpPortable_EncodeNameRequest(DeviceId,
                                              DeviceIdLength,
                                              ObjectId,
                                              ObjectIdLength,
                                              Name,
                                              NameLength,
                                              Payload,
                                              PayloadLength,
                                              &PayloadLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerPortable_SendStatus(Connection,
                                             OperationId,
                                             Payload,
                                             PayloadLength,
                                             TimeoutMilliseconds,
                                             Callback,
                                             Context,
                                             Request);
    }
    Mem_Free(Payload);
    return Status;
}

NTSTATUS
NTAPI
ZpServer_CreatePortableFolder(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpServerPortable_NameOperation(Connection,
                                          ZP_PORTABLE_DEVICE_OPERATION_CREATE_FOLDER,
                                          DeviceId,
                                          DeviceIdLength,
                                          ParentId,
                                          ParentIdLength,
                                          Name,
                                          NameLength,
                                          TimeoutMilliseconds,
                                          Callback,
                                          Context,
                                          Request);
}

NTSTATUS
NTAPI
ZpServer_RenamePortableObject(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    return ZpServerPortable_NameOperation(Connection,
                                          ZP_PORTABLE_DEVICE_OPERATION_RENAME,
                                          DeviceId,
                                          DeviceIdLength,
                                          ObjectId,
                                          ObjectIdLength,
                                          Name,
                                          NameLength,
                                          TimeoutMilliseconds,
                                          Callback,
                                          Context,
                                          Request);
}

NTSTATUS
NTAPI
ZpServer_DeletePortableObject(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_REQUEST_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;

    Status = ZpPortable_EncodeObjectRequest(DeviceId,
                                            DeviceIdLength,
                                            ObjectId,
                                            ObjectIdLength,
                                            NULL,
                                            0,
                                            &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpPortable_EncodeObjectRequest(DeviceId,
                                                DeviceIdLength,
                                                ObjectId,
                                                ObjectIdLength,
                                                Payload,
                                                PayloadLength,
                                                &PayloadLength);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerPortable_SendStatus(Connection,
                                             ZP_PORTABLE_DEVICE_OPERATION_DELETE,
                                             Payload,
                                             PayloadLength,
                                             TimeoutMilliseconds,
                                             Callback,
                                             Context,
                                             Request);
    }
    Mem_Free(Payload);
    return Status;
}

static
VOID
NTAPI
ZpServerPortable_OpenReadComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_PORTABLE_OPEN_READ_CONTEXT PortableContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONG ChannelId = 0;
    ULONGLONG FileSize = 0, Offset = 0;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpFile_DecodeOpenReadResponse(Payload->Buffer,
                                                                     Payload->Length,
                                                                     &ChannelId,
                                                                     &FileSize,
                                                                     &Offset));
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpServerChannel_Create(PortableContext->Connection,
                                                              ChannelId,
                                                              ZP_PORTABLE_DEVICE_MODULE_ID,
                                                              TRUE,
                                                              FileSize,
                                                              FALSE,
                                                              0,
                                                              PortableContext->DataCallback,
                                                              NULL,
                                                              PortableContext->CloseCallback,
                                                              PortableContext->Context,
                                                              TRUE,
                                                              &Channel));
    }
    else ZpServerChannel_ReleaseReservation(PortableContext->Connection);
    if (!ZpStatus_IsSuccess(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(PortableContext->Connection, ChannelId, Status);
    }
    PortableContext->OpenCallback(Request,
                                  Status,
                                  ZpStatus_IsSuccess(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
                                  ZpStatus_IsSuccess(Status) ? FileSize : 0,
                                  0,
                                  PortableContext->Context);
    if (Channel != NULL)
    {
        if (FileSize != 0)
        {
            NTSTATUS ChannelStatus = ZpServerChannel_SendWindow(Channel,
                                                                 (ULONG)min(FileSize,
                                                                            ZP_SERVER_DEFAULT_CHANNEL_WINDOW_SIZE));
            if (!NT_SUCCESS(ChannelStatus))
            {
                ZpServerChannel_Abort(Channel, ZpStatus_FromNtStatus(ChannelStatus));
            }
        }
        ZpChannel_Close((ZP_CHANNEL_HANDLE)Channel);
    }
    Mem_Free(PortableContext);
}

NTSTATUS
NTAPI
ZpServer_OpenPortableRead(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_OPEN_READ_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_DATA_CALLBACK DataCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_PORTABLE_OPEN_READ_CONTEXT PortableContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpPortable_EncodeObjectRequest(DeviceId,
                                            DeviceIdLength,
                                            ObjectId,
                                            ObjectIdLength,
                                            NULL,
                                            0,
                                            &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpPortable_EncodeObjectRequest(DeviceId,
                                                DeviceIdLength,
                                                ObjectId,
                                                ObjectIdLength,
                                                Payload,
                                                PayloadLength,
                                                &PayloadLength);
    }
    PortableContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*PortableContext)) : NULL;
    if (NT_SUCCESS(Status) && PortableContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerChannel_Reserve(Connection);
        Reserved = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        PortableContext->Connection = Connection;
        PortableContext->OpenCallback = OpenCallback;
        PortableContext->DataCallback = DataCallback;
        PortableContext->CloseCallback = CloseCallback;
        PortableContext->Context = Context;
        Status = ZpServerPortable_Send(Connection,
                                       ZP_PORTABLE_DEVICE_OPERATION_OPEN_READ,
                                       TimeoutMilliseconds,
                                       Payload,
                                       PayloadLength,
                                       ZpServerPortable_OpenReadComplete,
                                       PortableContext,
                                       Request);
        if (NT_SUCCESS(Status)) Reserved = FALSE;
        else Mem_Free(PortableContext);
    }
    if (Reserved) ZpServerChannel_ReleaseReservation(Connection);
    Mem_Free(Payload);
    return Status;
}

static
VOID
NTAPI
ZpServerPortable_OpenWriteComplete(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ PCZP_BUFFER_VIEW Payload,
    _In_opt_ PVOID Context)
{
    PZP_SERVER_PORTABLE_OPEN_WRITE_CONTEXT PortableContext = Context;
    PZP_SERVER_CHANNEL_OBJECT Channel = NULL;
    ULONG ChannelId = 0;
    ULONGLONG FileSize = 0;

    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpFile_DecodeOpenWriteResponse(Payload->Buffer,
                                                                      Payload->Length,
                                                                      &ChannelId,
                                                                      &FileSize));
    }
    if (ZpStatus_IsSuccess(Status))
    {
        Status = ZpStatus_FromNtStatus(ZpServerChannel_Create(PortableContext->Connection,
                                                              ChannelId,
                                                              ZP_PORTABLE_DEVICE_MODULE_ID,
                                                              FALSE,
                                                              0,
                                                              TRUE,
                                                              FileSize,
                                                              NULL,
                                                              PortableContext->WritableCallback,
                                                              PortableContext->CloseCallback,
                                                              PortableContext->Context,
                                                              TRUE,
                                                              &Channel));
    }
    else ZpServerChannel_ReleaseReservation(PortableContext->Connection);
    if (!ZpStatus_IsSuccess(Status) && ChannelId != 0)
    {
        ZpServerConnection_RejectChannel(PortableContext->Connection, ChannelId, Status);
    }
    PortableContext->OpenCallback(Request,
                                  Status,
                                  ZpStatus_IsSuccess(Status) ? (ZP_CHANNEL_HANDLE)Channel : NULL,
                                  ZpStatus_IsSuccess(Status) ? FileSize : 0,
                                  PortableContext->Context);
    if (Channel != NULL) ZpChannel_Close((ZP_CHANNEL_HANDLE)Channel);
    Mem_Free(PortableContext);
}

NTSTATUS
NTAPI
ZpServer_OpenPortableWrite(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG FileSize,
    _In_ ULONG TimeoutMilliseconds,
    _In_ ZP_FILE_OPEN_WRITE_CALLBACK OpenCallback,
    _In_ ZP_CHANNEL_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_CHANNEL_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context,
    _Out_ ZP_REQUEST_HANDLE* Request)
{
    PZP_SERVER_PORTABLE_OPEN_WRITE_CONTEXT PortableContext;
    PBYTE Payload = NULL;
    ULONG PayloadLength;
    NTSTATUS Status;
    LOGICAL Reserved = FALSE;

    if (OpenCallback == NULL || WritableCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Status = ZpPortable_EncodeWriteRequest(DeviceId,
                                           DeviceIdLength,
                                           ParentId,
                                           ParentIdLength,
                                           Name,
                                           NameLength,
                                           FileSize,
                                           NULL,
                                           0,
                                           &PayloadLength);
    Payload = NT_SUCCESS(Status) ? Mem_Alloc(PayloadLength) : NULL;
    if (NT_SUCCESS(Status) && Payload == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpPortable_EncodeWriteRequest(DeviceId,
                                               DeviceIdLength,
                                               ParentId,
                                               ParentIdLength,
                                               Name,
                                               NameLength,
                                               FileSize,
                                               Payload,
                                               PayloadLength,
                                               &PayloadLength);
    }
    PortableContext = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*PortableContext)) : NULL;
    if (NT_SUCCESS(Status) && PortableContext == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpServerChannel_Reserve(Connection);
        Reserved = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        PortableContext->Connection = Connection;
        PortableContext->OpenCallback = OpenCallback;
        PortableContext->WritableCallback = WritableCallback;
        PortableContext->CloseCallback = CloseCallback;
        PortableContext->Context = Context;
        Status = ZpServerPortable_Send(Connection,
                                       ZP_PORTABLE_DEVICE_OPERATION_OPEN_WRITE,
                                       TimeoutMilliseconds,
                                       Payload,
                                       PayloadLength,
                                       ZpServerPortable_OpenWriteComplete,
                                       PortableContext,
                                       Request);
        if (NT_SUCCESS(Status)) Reserved = FALSE;
        else Mem_Free(PortableContext);
    }
    if (Reserved) ZpServerChannel_ReleaseReservation(Connection);
    Mem_Free(Payload);
    return Status;
}
