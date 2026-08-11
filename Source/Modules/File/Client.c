#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <Bcrypt.h>
#include <stdlib.h>

#pragma comment(lib, "Bcrypt.lib")

#define ZP_FILE_HASH_BUFFER_SIZE 0x00010000UL
#define ZP_FILE_CHANNEL_CHUNK_SIZE 0x00010000UL
#define ZP_FILE_WRITE_WINDOW_SIZE 0x00100000UL
#define ZP_FILE_SNAPSHOT_MAX_BYTES 0x01000000UL
#define ZP_FILE_SNAPSHOT_MAX_COUNT 65536

typedef struct _ZP_FILE_ENTRY
{
    LIST_ENTRY ListEntry;
    ZP_FILE_INFO Info;
    ULONG NameLength;
    WCHAR Name[ANYSIZE_ARRAY];
} ZP_FILE_ENTRY, *PZP_FILE_ENTRY;

typedef enum _ZP_CLIENT_FILE_CHANNEL_TYPE
{
    ZpClientFileChannelRead,
    ZpClientFileChannelWrite
} ZP_CLIENT_FILE_CHANNEL_TYPE;

struct _ZP_CLIENT_FILE_CHANNEL
{
    union
    {
        ZP_CLIENT_LOCAL_CHANNEL Header;
        struct
        {
            LIST_ENTRY ListEntry;
            PZP_CLIENT_OBJECT Owner;
            volatile LONG ReferenceCount;
            volatile LONG Pending;
            ULONGLONG ChannelId;
            USHORT ModuleId;
            ZP_CLIENT_LOCAL_CHANNEL_DATA_ROUTINE ReceiveData;
            ZP_CLIENT_LOCAL_CHANNEL_WINDOW_ROUTINE ReceiveWindow;
            ZP_CLIENT_LOCAL_CHANNEL_CLOSE_ROUTINE ReceiveClose;
            ZP_CLIENT_LOCAL_CHANNEL_ABORT_ROUTINE Abort;
            ZP_CLIENT_LOCAL_CHANNEL_DESTROY_ROUTINE Destroy;
        };
    };
    LOGICAL WorkerActive;
    ZP_CLIENT_FILE_CHANNEL_TYPE Type;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    ULONGLONG RemainingBytes;
    ULONGLONG Offset;
    HANDLE File;
    PUNICODE_STRING FinalPath;
    PUNICODE_STRING TemporaryPath;
    ZP_FILE_CREATE_DISPOSITION Disposition;
    LOGICAL OwnsTemporaryPath;
};

static
NTSTATUS
ZpFile_ChannelData(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message);

static
NTSTATUS
ZpFile_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ULONG CreditBytes);

static
NTSTATUS
ZpFile_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ZP_STATUS Status);

static
VOID
ZpFile_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ZP_STATUS Status);

static
VOID
ZpFile_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel);

static
PUNICODE_STRING
ZpFile_CopyPath(
    _In_ PCZP_STRING_VIEW Path)
{
    PUNICODE_STRING String;

    if (Path->Length > MAXUSHORT / sizeof(WCHAR))
    {
        return NULL;
    }
    String = NT_AllocStringW((USHORT)Path->Length);
    if (String != NULL)
    {
        RtlCopyMemory(String->Buffer,
                      Path->Buffer,
                      (SIZE_T)Path->Length * sizeof(WCHAR));
        String->Buffer[Path->Length] = UNICODE_NULL;
    }
    return String;
}

static
VOID
ZpFile_DestroyChannel(
    _Inout_ PZP_CLIENT_FILE_CHANNEL Channel)
{
    if (Channel->File != NULL)
    {
        NtClose(Channel->File);
    }
    if (Channel->TemporaryPath != NULL)
    {
        if (Channel->OwnsTemporaryPath)
        {
            IO_DeleteWin32File(Channel->TemporaryPath->Buffer, NULL);
        }
        NT_FreeStringW(Channel->TemporaryPath);
    }
    if (Channel->FinalPath != NULL)
    {
        NT_FreeStringW(Channel->FinalPath);
    }
    Mem_Free(Channel);
}

static
VOID
ZpFile_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel)
{
    ZpFile_DestroyChannel((PZP_CLIENT_FILE_CHANNEL)Channel);
}

static
VOID
ZpFile_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL Channel,
    _In_ ZP_STATUS Status)
{
    UNREFERENCED_PARAMETER(Channel);
    UNREFERENCED_PARAMETER(Status);
}

static
NTSTATUS
ZpFile_SendLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PCZP_TRANSPORT_OPERATIONS Operations =
        Object->TransportOperations[Object->ActiveTransport];

    return Object->State == ZpClientStateReady && Operations->Send != NULL ?
               Operations->Send(
                   Object->TransportContexts[Object->ActiveTransport],
                   MessageType,
                   Body,
                   BodyLength) :
               STATUS_CONNECTION_DISCONNECTED;
}

static
NTSTATUS
ZpFile_SendCloseLocked(
    _In_ PZP_CLIENT_FILE_CHANNEL Channel,
    _In_ NTSTATUS CloseStatus)
{
    BYTE Body[sizeof(ULONGLONG) + ZP_STATUS_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->ChannelId,
                                          ZpStatus_FromNtStatus(CloseStatus),
                                          Body,
                                          sizeof(Body),
                                          &BodyLength);
    return NT_SUCCESS(Status) ?
               ZpFile_SendLocked(Channel->Owner,
                                 ZpMessageChannelClose,
                                 Body,
                                 BodyLength) :
               Status;
}

static
VOID
ZpFile_CompleteChannel(
    _Inout_ PZP_CLIENT_FILE_CHANNEL Channel,
    _In_ NTSTATUS Status)
{
    PZP_CLIENT_OBJECT Object = Channel->Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ZpClientLocalChannel_RemoveLocked(&Channel->Header))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return;
    }
    ZpFile_SendCloseLocked(Channel, Status);
    Removed = TRUE;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
    }
}

static
NTSTATUS
ZpFile_CommitWrite(
    _Inout_ PZP_CLIENT_FILE_CHANNEL Channel)
{
    PFILE_RENAME_INFORMATION_EX Rename;
    UNICODE_STRING NativePath;
    IO_STATUS_BLOCK IoStatusBlock;
    SIZE_T RenameSize;
    NTSTATUS Status;

    Status = NtFlushBuffersFile(Channel->File, &IoStatusBlock);
    if (NT_SUCCESS(Status))
    {
        Status = NT_Win32PathToNtPath(Channel->FinalPath->Buffer,
                                      NULL,
                                      &NativePath);
    }
    if (NT_SUCCESS(Status))
    {
        RenameSize = UFIELD_OFFSET(FILE_RENAME_INFORMATION_EX, FileName) +
                     NativePath.Length;
        Rename = Mem_Alloc(RenameSize);
        if (Rename == NULL)
        {
            Status = STATUS_NO_MEMORY;
        }
        else
        {
            Rename->Flags = Channel->Disposition == ZpFileCreateAlways ?
                                FILE_RENAME_REPLACE_IF_EXISTS : 0;
            Rename->RootDirectory = NULL;
            Rename->FileNameLength = NativePath.Length;
            RtlCopyMemory(Rename->FileName,
                          NativePath.Buffer,
                          NativePath.Length);
            Status = NtSetInformationFile(Channel->File,
                                          &IoStatusBlock,
                                          Rename,
                                          (ULONG)RenameSize,
                                          FileRenameInformationEx);
            Mem_Free(Rename);
        }
        NT_FreeNtPath(&NativePath);
    }
    NtClose(Channel->File);
    Channel->File = NULL;
    if (NT_SUCCESS(Status))
    {
        NT_FreeStringW(Channel->TemporaryPath);
        Channel->TemporaryPath = NULL;
        Channel->OwnsTemporaryPath = FALSE;
    }
    return Status;
}

static
NTSTATUS
ZpFile_CreateReadChannel(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ PCZP_STRING_VIEW Path,
    _In_ ULONGLONG Offset,
    _Out_ PZP_CLIENT_FILE_CHANNEL* Channel,
    _Out_ PULONGLONG FileSize)
{
    PZP_CLIENT_FILE_CHANNEL ChannelObject;
    PUNICODE_STRING PathString;
    NTSTATUS Status;

    PathString = ZpFile_CopyPath(Path);
    ChannelObject = Mem_Alloc(sizeof(*ChannelObject));
    if (PathString == NULL || ChannelObject == NULL)
    {
        if (PathString != NULL)
        {
            NT_FreeStringW(PathString);
        }
        Mem_Free(ChannelObject);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(ChannelObject, sizeof(*ChannelObject));
    ChannelObject->Type = ZpClientFileChannelRead;
    Status = IO_OpenWin32File(&ChannelObject->File,
                              PathString->Buffer,
                              NULL,
                              FILE_READ_DATA | FILE_READ_ATTRIBUTES |
                                  SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE);
    NT_FreeStringW(PathString);
    if (NT_SUCCESS(Status))
    {
        Status = IO_GetFileSize(ChannelObject->File, FileSize);
    }
    if (NT_SUCCESS(Status) && Offset > *FileSize)
    {
        Status = STATUS_END_OF_FILE;
    }
    if (NT_SUCCESS(Status))
    {
        ChannelObject->RemainingBytes = *FileSize - Offset;
        ChannelObject->Offset = Offset;
        Status = ZpClientLocalChannel_Insert(
            Object,
            &ChannelObject->Header,
            ZP_FILE_MODULE_ID,
            NULL,
            ZpFile_ChannelWindow,
            ZpFile_ChannelClose,
            ZpFile_ChannelAbort,
            ZpFile_ChannelDestroy);
    }
    if (!NT_SUCCESS(Status))
    {
        ZpFile_DestroyChannel(ChannelObject);
        return Status;
    }
    *Channel = ChannelObject;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFile_CreateWriteChannel(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ PCZP_STRING_VIEW Path,
    _In_ ULONGLONG FileSize,
    _In_ ZP_FILE_CREATE_DISPOSITION Disposition,
    _Out_ PZP_CLIENT_FILE_CHANNEL* Channel)
{
    PZP_CLIENT_FILE_CHANNEL ChannelObject;
    ULONGLONG RandomValue;
    ULONG Attempt;
    INT Length;
    NTSTATUS Status;

    if (Path->Length > MAXUSHORT / sizeof(WCHAR) - 32)
    {
        return STATUS_NAME_TOO_LONG;
    }
    ChannelObject = Mem_Alloc(sizeof(*ChannelObject));
    if (ChannelObject == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(ChannelObject, sizeof(*ChannelObject));
    ChannelObject->Type = ZpClientFileChannelWrite;
    ChannelObject->FinalPath = ZpFile_CopyPath(Path);
    ChannelObject->TemporaryPath =
        NT_AllocStringW((USHORT)(Path->Length + 32));
    if (ChannelObject->FinalPath == NULL ||
        ChannelObject->TemporaryPath == NULL)
    {
        ZpFile_DestroyChannel(ChannelObject);
        return STATUS_NO_MEMORY;
    }
    Status = STATUS_OBJECT_NAME_COLLISION;
    for (Attempt = 0; Attempt < 16; Attempt++)
    {
        Status = BCryptGenRandom(NULL,
                                 (PBYTE)&RandomValue,
                                 sizeof(RandomValue),
                                 BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        Length = _snwprintf_s(ChannelObject->TemporaryPath->Buffer,
                              Path->Length + 33,
                              _TRUNCATE,
                              L"%s.%016llX.zpigeon.tmp",
                              ChannelObject->FinalPath->Buffer,
                              RandomValue);
        if (Length < 0)
        {
            Status = STATUS_NAME_TOO_LONG;
            break;
        }
        ChannelObject->TemporaryPath->Length =
            (USHORT)(Length * sizeof(WCHAR));
        Status = IO_CreateWin32File(&ChannelObject->File,
                                    ChannelObject->TemporaryPath->Buffer,
                                    NULL,
                                    FILE_WRITE_DATA | FILE_WRITE_ATTRIBUTES |
                                        DELETE | SYNCHRONIZE,
                                    FILE_SHARE_READ | FILE_SHARE_DELETE,
                                    FILE_CREATE,
                                    FILE_NON_DIRECTORY_FILE |
                                        FILE_SYNCHRONOUS_IO_NONALERT |
                                        FILE_SEQUENTIAL_ONLY);
        if (Status != STATUS_OBJECT_NAME_COLLISION)
        {
            break;
        }
    }
    if (NT_SUCCESS(Status))
    {
        ChannelObject->OwnsTemporaryPath = TRUE;
        ChannelObject->RemainingBytes = FileSize;
        ChannelObject->Disposition = Disposition;
        Status = ZpClientLocalChannel_Insert(
            Object,
            &ChannelObject->Header,
            ZP_FILE_MODULE_ID,
            ZpFile_ChannelData,
            NULL,
            ZpFile_ChannelClose,
            ZpFile_ChannelAbort,
            ZpFile_ChannelDestroy);
    }
    if (!NT_SUCCESS(Status))
    {
        ZpFile_DestroyChannel(ChannelObject);
        return Status;
    }
    *Channel = ChannelObject;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFile_SendWindowLocked(
    _Inout_ PZP_CLIENT_FILE_CHANNEL Channel,
    _In_ ULONG CreditBytes)
{
    BYTE Body[sizeof(ULONGLONG) + sizeof(ULONG)];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelWindow(Channel->ChannelId,
                                           CreditBytes,
                                           Body,
                                           sizeof(Body),
                                           &BodyLength);
    if (NT_SUCCESS(Status))
    {
        Channel->ReceiveCredit += CreditBytes;
        Status = ZpFile_SendLocked(Channel->Owner,
                                  ZpMessageChannelWindow,
                                  Body,
                                  BodyLength);
        if (!NT_SUCCESS(Status))
        {
            Channel->ReceiveCredit -= CreditBytes;
        }
    }
    return Status;
}

static
VOID
ZpFile_FinishWorker(
    _Inout_ PZP_CLIENT_FILE_CHANNEL Channel,
    _In_ NTSTATUS Status,
    _In_ LOGICAL Notify)
{
    PZP_CLIENT_OBJECT Object = Channel->Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    if (Removed && Notify)
    {
        ZpFile_SendCloseLocked(Channel, Status);
    }
    Channel->WorkerActive = FALSE;
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
VOID
CALLBACK
ZpFile_ReadChannelCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_FILE_CHANNEL Channel = Context;
    PZP_CLIENT_OBJECT Object = Channel->Owner;
    PBYTE Body;
    LARGE_INTEGER Offset;
    ULONG ReadLength, BytesRead, BodyLength;
    NTSTATUS Status;
    LOGICAL Removed = FALSE;

    UNREFERENCED_PARAMETER(Instance);
    Body = Mem_Alloc(sizeof(ULONGLONG) + ZP_FILE_CHANNEL_CHUNK_SIZE);
    if (Body == NULL)
    {
        ZpFile_FinishWorker(Channel, STATUS_NO_MEMORY, TRUE);
        return;
    }
    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (!Channel->Pending)
        {
            Channel->WorkerActive = FALSE;
            Object->CallbackCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            break;
        }
        ReadLength = (ULONG)min(min(Channel->Credit,
                                   Channel->RemainingBytes),
                                ZP_FILE_CHANNEL_CHUNK_SIZE);
        if (ReadLength == 0)
        {
            if (Channel->RemainingBytes == 0)
            {
                Removed = ZpClientLocalChannel_RemoveLocked(
                    &Channel->Header);
                ZpFile_SendCloseLocked(Channel, STATUS_SUCCESS);
            }
            Channel->WorkerActive = FALSE;
            Object->CallbackCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            if (Channel->RemainingBytes != 0)
            {
                Mem_Free(Body);
                ZpClientLocalChannel_Release(&Channel->Header);
                return;
            }
            break;
        }
        Offset.QuadPart = Channel->Offset;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Status = IO_ReadFile(Channel->File,
                             &Offset,
                             Add2Ptr(Body, sizeof(ULONGLONG)),
                             ReadLength,
                             &BytesRead);
        if (!NT_SUCCESS(Status) || BytesRead != ReadLength)
        {
            Mem_Free(Body);
            ZpFile_FinishWorker(
                Channel,
                NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status,
                TRUE);
            return;
        }
        Status = ZpMessage_EncodeChannelData(
            Channel->ChannelId,
            Add2Ptr(Body, sizeof(ULONGLONG)),
            BytesRead,
            Body,
            sizeof(ULONGLONG) + ZP_FILE_CHANNEL_CHUNK_SIZE,
            &BodyLength);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Body);
            ZpFile_FinishWorker(Channel, Status, TRUE);
            return;
        }
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (!Channel->Pending)
        {
            Channel->WorkerActive = FALSE;
            Object->CallbackCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            break;
        }
        Channel->Credit -= BytesRead;
        Channel->RemainingBytes -= BytesRead;
        Channel->Offset += BytesRead;
        Status = ZpFile_SendLocked(Object,
                                   ZpMessageChannelData,
                                   Body,
                                   BodyLength);
        if (!NT_SUCCESS(Status) || Channel->RemainingBytes == 0)
        {
            Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
            ZpFile_SendCloseLocked(Channel, Status);
            Channel->WorkerActive = FALSE;
            Object->CallbackCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            break;
        }
        RtlReleaseSRWLockExclusive(&Object->Lock);
    }
    Mem_Free(Body);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    ZpClientLocalChannel_Release(&Channel->Header);
}

VOID
ZpFile_CommitChannel(
    _Inout_ PZP_CLIENT_FILE_CHANNEL Channel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_OBJECT Object = Channel->Owner;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG CreditBytes;
    LOGICAL Removed = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ResponseSent)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    }
    else if (Channel->Type == ZpClientFileChannelWrite)
    {
        if (Channel->RemainingBytes == 0)
        {
            Status = ZpFile_CommitWrite(Channel);
            Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
            ZpFile_SendCloseLocked(Channel, Status);
        }
        else
        {
            CreditBytes = (ULONG)min(Channel->RemainingBytes,
                                     ZP_FILE_WRITE_WINDOW_SIZE);
            Status = ZpFile_SendWindowLocked(Channel, CreditBytes);
            if (!NT_SUCCESS(Status))
            {
                Removed = ZpClientLocalChannel_RemoveLocked(
                    &Channel->Header);
            }
        }
    }
    else if (Channel->RemainingBytes == 0)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        ZpFile_SendCloseLocked(Channel, STATUS_SUCCESS);
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
    }
}

static
NTSTATUS
ZpFile_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_FILE_CHANNEL Channel =
        (PZP_CLIENT_FILE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Owner;
    LOGICAL Queue = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Pending || Channel->Type != ZpClientFileChannelRead ||
        MAXULONGLONG - Channel->Credit < CreditBytes)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->Credit += CreditBytes;
    if (!Channel->WorkerActive)
    {
        Channel->WorkerActive = TRUE;
        ZpClientLocalChannel_AddRef(&Channel->Header);
        Object->CallbackCount++;
        Queue = TRUE;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Queue && !TrySubmitThreadpoolCallback(ZpFile_ReadChannelCallback,
                                              Channel,
                                              NULL))
    {
        ZpFile_FinishWorker(Channel, STATUS_NO_MEMORY, TRUE);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFile_ChannelData(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_CLIENT_FILE_CHANNEL Channel =
        (PZP_CLIENT_FILE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Owner;
    ULONG BytesWritten, CreditBytes;
    NTSTATUS Status;
    LOGICAL Removed = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Pending || Channel->Type != ZpClientFileChannelWrite ||
        Message->Data.Length > Channel->ReceiveCredit ||
        Message->Data.Length > Channel->RemainingBytes)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->ReceiveCredit -= Message->Data.Length;
    Status = IO_WriteFile(Channel->File,
                          NULL,
                          (PVOID)Message->Data.Buffer,
                          Message->Data.Length,
                          &BytesWritten);
    if (NT_SUCCESS(Status) && BytesWritten != Message->Data.Length)
    {
        Status = STATUS_UNSUCCESSFUL;
    }
    if (NT_SUCCESS(Status))
    {
        Channel->RemainingBytes -= BytesWritten;
        if (Channel->RemainingBytes == 0)
        {
            Status = ZpFile_CommitWrite(Channel);
        }
    }
    if (!NT_SUCCESS(Status) || Channel->RemainingBytes == 0)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        ZpFile_SendCloseLocked(Channel, Status);
    }
    else
    {
        CreditBytes = (ULONG)min(
            Message->Data.Length,
            Channel->RemainingBytes - Channel->ReceiveCredit);
        if (CreditBytes != 0)
        {
            Status = ZpFile_SendWindowLocked(Channel, CreditBytes);
            if (!NT_SUCCESS(Status))
            {
                Removed = ZpClientLocalChannel_RemoveLocked(
                    &Channel->Header);
            }
        }
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    return NT_SUCCESS(Status) ? STATUS_SUCCESS : Status;
}

static
NTSTATUS
ZpFile_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_FILE_CHANNEL Channel =
        (PZP_CLIENT_FILE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if ((ZpStatus_IsSuccess(Status) && Channel->RemainingBytes != 0) ||
        !ZpClientLocalChannel_RemoveLocked(&Channel->Header))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Removed = TRUE;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
    }
    return STATUS_SUCCESS;
}

static
VOID
ZpFile_SetInfo(
    _In_ PFILE_DIRECTORY_INFORMATION Data,
    _Out_ PZP_FILE_INFO Info)
{
    Info->Attributes = Data->FileAttributes;
    Info->Size = Data->EndOfFile.QuadPart;
    Info->CreationTime = Data->CreationTime.QuadPart;
    Info->LastAccessTime = Data->LastAccessTime.QuadPart;
    Info->LastWriteTime = Data->LastWriteTime.QuadPart;
}

static
NTSTATUS
ZpFile_Query(
    _In_ PCZP_STRING_VIEW Path,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    FILE_NETWORK_OPEN_INFORMATION Data;
    ZP_FILE_INFO Info;
    PUNICODE_STRING String;
    NTSTATUS Status;

    String = ZpFile_CopyPath(Path);
    if (String == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = IO_GetWin32FileAttributes(String->Buffer, NULL, &Data);
    NT_FreeStringW(String);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Info.Attributes = Data.FileAttributes;
    Info.Size = Data.EndOfFile.QuadPart;
    Info.CreationTime = Data.CreationTime.QuadPart;
    Info.LastAccessTime = Data.LastAccessTime.QuadPart;
    Info.LastWriteTime = Data.LastWriteTime.QuadPart;
    Status = ZpFile_EncodeInfo(&Info, NULL, 0, ResponseLength);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    return NT_SUCCESS(Status) ?
               ZpFile_EncodeInfo(&Info,
                                 *Response,
                                 *ResponseLength,
                                 ResponseLength) :
               Status;
}

static
int
__cdecl
ZpFile_CompareEntries(
    _In_ const VOID* Left,
    _In_ const VOID* Right)
{
    const PZP_FILE_ENTRY* LeftEntry = Left;
    const PZP_FILE_ENTRY* RightEntry = Right;
    int Result;

    Result = CompareStringOrdinal((*LeftEntry)->Name,
                                  (*LeftEntry)->NameLength,
                                  (*RightEntry)->Name,
                                  (*RightEntry)->NameLength,
                                  TRUE);
    if (Result == CSTR_EQUAL)
    {
        Result = CompareStringOrdinal((*LeftEntry)->Name,
                                      (*LeftEntry)->NameLength,
                                      (*RightEntry)->Name,
                                      (*RightEntry)->NameLength,
                                      FALSE);
    }
    return Result == CSTR_LESS_THAN ? -1 :
           Result == CSTR_GREATER_THAN ? 1 : 0;
}

static
VOID
ZpFile_FreeEntries(
    _Inout_ PLIST_ENTRY Entries)
{
    PZP_FILE_ENTRY Entry;

    while (!IsListEmpty(Entries))
    {
        Entry = CONTAINING_RECORD(RemoveHeadList(Entries),
                                  ZP_FILE_ENTRY,
                                  ListEntry);
        Mem_Free(Entry);
    }
}

static
NTSTATUS
ZpFile_SnapshotDirectory(
    _In_ PCZP_STRING_VIEW Path,
    _Outptr_result_buffer_(*Count) PZP_FILE_ENTRY** Entries,
    _Out_ PULONG Count)
{
    LIST_ENTRY List;
    FILE_FIND Find;
    PFILE_DIRECTORY_INFORMATION Data;
    PZP_FILE_ENTRY Entry;
    PZP_FILE_ENTRY* Result = NULL;
    PUNICODE_STRING PathString;
    UNICODE_STRING NativePath;
    HANDLE Directory = NULL;
    SIZE_T AllocationSize, SnapshotSize = 0;
    ULONG EntryCount = 0, Index, Remaining;
    NTSTATUS Status;
    LOGICAL FindInitialized = FALSE;

    InitializeListHead(&List);
    PathString = ZpFile_CopyPath(Path);
    if (PathString == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = NT_Win32PathToNtPath(PathString->Buffer, NULL, &NativePath);
    NT_FreeStringW(PathString);
    if (NT_SUCCESS(Status))
    {
        Status = IO_OpenDirectory(&Directory,
                                  &NativePath,
                                  FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE |
                                      FILE_SHARE_DELETE);
        NT_FreeNtPath(&NativePath);
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = IO_BeginFindFile(&Find,
                              Directory,
                              NULL,
                              FileDirectoryInformation);
    FindInitialized = NT_SUCCESS(Status);
    while (NT_SUCCESS(Status) && Find.HasData)
    {
        Data = Find.Buffer;
        Remaining = Find.Length;
        for (;;)
        {
            if (Remaining < UFIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) ||
                Data->FileNameLength >
                    Remaining - UFIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) ||
                (Data->FileNameLength & (sizeof(WCHAR) - 1)) != 0)
            {
                Status = STATUS_DATA_ERROR;
                break;
            }
            if (!((Data->FileNameLength == sizeof(WCHAR) &&
                   Data->FileName[0] == L'.') ||
                  (Data->FileNameLength == 2 * sizeof(WCHAR) &&
                   Data->FileName[0] == L'.' &&
                   Data->FileName[1] == L'.')))
            {
                AllocationSize = UFIELD_OFFSET(ZP_FILE_ENTRY, Name) +
                                 Data->FileNameLength;
                if (AllocationSize >
                        ZP_FILE_SNAPSHOT_MAX_BYTES - sizeof(Entry) ||
                    EntryCount == ZP_FILE_SNAPSHOT_MAX_COUNT ||
                    SnapshotSize > ZP_FILE_SNAPSHOT_MAX_BYTES -
                                       AllocationSize - sizeof(Entry))
                {
                    Status = STATUS_QUOTA_EXCEEDED;
                    break;
                }
                Entry = Mem_Alloc(AllocationSize);
                if (Entry == NULL)
                {
                    Status = STATUS_NO_MEMORY;
                    break;
                }
                ZpFile_SetInfo(Data, &Entry->Info);
                Entry->NameLength = Data->FileNameLength / sizeof(WCHAR);
                RtlCopyMemory(Entry->Name,
                              Data->FileName,
                              Data->FileNameLength);
                InsertTailList(&List, &Entry->ListEntry);
                SnapshotSize += AllocationSize + sizeof(Entry);
                EntryCount++;
            }
            if (Data->NextEntryOffset == 0)
            {
                break;
            }
            if (Data->NextEntryOffset > Remaining ||
                Data->NextEntryOffset <
                    UFIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName))
            {
                Status = STATUS_DATA_ERROR;
                break;
            }
            Remaining -= Data->NextEntryOffset;
            Data = Add2Ptr(Data, Data->NextEntryOffset);
        }
        if (NT_SUCCESS(Status))
        {
            Status = IO_ContinueFindFileFind(&Find);
        }
    }
    if (FindInitialized)
    {
        IO_EndFindFile(&Find);
    }
    NtClose(Directory);
    if (!NT_SUCCESS(Status))
    {
        ZpFile_FreeEntries(&List);
        return Status;
    }
    Result = EntryCount != 0 ?
                 Mem_Alloc((SIZE_T)EntryCount * sizeof(*Result)) :
                 NULL;
    if (EntryCount != 0 && Result == NULL)
    {
        ZpFile_FreeEntries(&List);
        return STATUS_NO_MEMORY;
    }
    for (Index = 0; Index < EntryCount; Index++)
    {
        Result[Index] = CONTAINING_RECORD(List.Flink,
                                          ZP_FILE_ENTRY,
                                          ListEntry);
        RemoveEntryList(&Result[Index]->ListEntry);
    }
    qsort(Result, EntryCount, sizeof(*Result), ZpFile_CompareEntries);
    *Entries = Result;
    *Count = EntryCount;
    return STATUS_SUCCESS;
}

static
VOID
ZpFile_FreeSnapshot(
    _In_reads_(Count) PZP_FILE_ENTRY* Entries,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Mem_Free(Entries[Index]);
    }
    Mem_Free(Entries);
}

static
NTSTATUS
ZpFile_EncodeSnapshot(
    _In_reads_(Count) PZP_FILE_ENTRY* Entries,
    _In_ ULONG Count,
    _In_opt_ PCZP_STRING_VIEW Cursor,
    _In_ ULONG MaxEntries,
    _In_ LOGICAL Paged,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_FILE_RECORD Records;
    ULONG Start = 0, PageCount, Index;
    int Compare;
    LOGICAL HasMore;
    NTSTATUS Status;

    if (Paged && Cursor->Length != 0)
    {
        while (Start < Count)
        {
            Compare = CompareStringOrdinal(Entries[Start]->Name,
                                           Entries[Start]->NameLength,
                                           (PCWCH)Cursor->Buffer,
                                           Cursor->Length,
                                           TRUE);
            if (Compare == CSTR_EQUAL)
            {
                Compare = CompareStringOrdinal(Entries[Start]->Name,
                                               Entries[Start]->NameLength,
                                               (PCWCH)Cursor->Buffer,
                                               Cursor->Length,
                                               FALSE);
            }
            if (Compare == CSTR_GREATER_THAN)
            {
                break;
            }
            Start++;
        }
    }
    PageCount = Paged ? min(MaxEntries, Count - Start) : Count;
    Records = PageCount != 0 ?
                  Mem_Alloc((SIZE_T)PageCount * sizeof(*Records)) :
                  NULL;
    if (PageCount != 0 && Records == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    for (Index = 0; Index < PageCount; Index++)
    {
        Records[Index].Info = Entries[Start + Index]->Info;
        Records[Index].Name = Entries[Start + Index]->Name;
        Records[Index].NameLength = Entries[Start + Index]->NameLength;
    }
    do
    {
        HasMore = Paged && Start + PageCount < Count;
        Status = Paged ?
                     ZpFile_EncodePage(
                         Records,
                         PageCount,
                         HasMore ? Records[PageCount - 1].Name : NULL,
                         HasMore ? Records[PageCount - 1].NameLength : 0,
                         NULL,
                         0,
                         ResponseLength) :
                     ZpFile_EncodeList(Records,
                                       PageCount,
                                       NULL,
                                       0,
                                       ResponseLength);
        if (Status == STATUS_BUFFER_OVERFLOW && Paged && PageCount > 1)
        {
            PageCount /= 2;
        }
        else
        {
            break;
        }
    } while (TRUE);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = Paged ?
                     ZpFile_EncodePage(
                         Records,
                         PageCount,
                         HasMore ? Records[PageCount - 1].Name : NULL,
                         HasMore ? Records[PageCount - 1].NameLength : 0,
                         *Response,
                         *ResponseLength,
                         ResponseLength) :
                     ZpFile_EncodeList(Records,
                                       PageCount,
                                       *Response,
                                       *ResponseLength,
                                       ResponseLength);
    }
    Mem_Free(Records);
    return Status;
}

static
NTSTATUS
ZpFile_Enumerate(
    _In_ PCZP_STRING_VIEW Path,
    _In_opt_ PCZP_STRING_VIEW Cursor,
    _In_ ULONG MaxEntries,
    _In_ LOGICAL Paged,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_FILE_ENTRY* Entries;
    ULONG Count;
    NTSTATUS Status;

    Status = ZpFile_SnapshotDirectory(Path, &Entries, &Count);
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeSnapshot(Entries,
                                       Count,
                                       Cursor,
                                       MaxEntries,
                                       Paged,
                                       Response,
                                       ResponseLength);
        ZpFile_FreeSnapshot(Entries, Count);
    }
    return Status;
}

static
NTSTATUS
ZpFile_Hash(
    _In_ PCZP_STRING_VIEW Path,
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_ volatile LONG* Pending,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    BCRYPT_ALG_HANDLE AlgorithmHandle = NULL;
    BCRYPT_HASH_HANDLE HashHandle = NULL;
    PUNICODE_STRING PathString;
    HANDLE File = NULL;
    PBYTE Buffer = NULL;
    BYTE Digest[ZP_FILE_SHA256_SIZE];
    ULONGLONG FileSize;
    ULONG BytesRead;
    NTSTATUS Status;

    if (Algorithm != ZpFileHashSha256)
    {
        return STATUS_NOT_SUPPORTED;
    }
    PathString = ZpFile_CopyPath(Path);
    if (PathString == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = IO_OpenWin32File(&File,
                              PathString->Buffer,
                              NULL,
                              FILE_READ_DATA | FILE_READ_ATTRIBUTES |
                                  SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE |
                                  FILE_SHARE_DELETE);
    NT_FreeStringW(PathString);
    if (NT_SUCCESS(Status))
    {
        Status = IO_GetFileSize(File, &FileSize);
    }
    if (NT_SUCCESS(Status))
    {
        Status = BCryptOpenAlgorithmProvider(&AlgorithmHandle,
                                             BCRYPT_SHA256_ALGORITHM,
                                             NULL,
                                             0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = BCryptCreateHash(AlgorithmHandle,
                                  &HashHandle,
                                  NULL,
                                  0,
                                  NULL,
                                  0,
                                  0);
    }
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(ZP_FILE_HASH_BUFFER_SIZE) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    while (NT_SUCCESS(Status))
    {
        if (!InterlockedCompareExchange(Pending, TRUE, TRUE))
        {
            Status = STATUS_CANCELLED;
            break;
        }
        Status = IO_ReadFile(File,
                             NULL,
                             Buffer,
                             ZP_FILE_HASH_BUFFER_SIZE,
                             &BytesRead);
        if (Status == STATUS_END_OF_FILE)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status) || BytesRead == 0)
        {
            break;
        }
        Status = BCryptHashData(HashHandle, Buffer, BytesRead, 0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = BCryptFinishHash(HashHandle,
                                  Digest,
                                  sizeof(Digest),
                                  0);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeHashResponse(Algorithm,
                                           FileSize,
                                           Digest,
                                           sizeof(Digest),
                                           NULL,
                                           0,
                                           ResponseLength);
    }
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeHashResponse(Algorithm,
                                           FileSize,
                                           Digest,
                                           sizeof(Digest),
                                           *Response,
                                           *ResponseLength,
                                           ResponseLength);
    }
    Mem_Free(Buffer);
    if (HashHandle != NULL)
    {
        BCryptDestroyHash(HashHandle);
    }
    if (AlgorithmHandle != NULL)
    {
        BCryptCloseAlgorithmProvider(AlgorithmHandle, 0);
    }
    if (File != NULL)
    {
        NtClose(File);
    }
    return Status;
}

NTSTATUS
ZpFile_Execute(
    _Inout_opt_ PZP_CLIENT_OBJECT Client,
    _In_ USHORT OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _In_ volatile LONG* Pending,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_FILE_CHANNEL* Channel)
{
    ZP_STRING_VIEW Path, Cursor;
    ZP_FILE_HASH_ALGORITHM Algorithm;
    PZP_CLIENT_FILE_CHANNEL FileChannel = NULL;
    ULONGLONG FileSize, Offset;
    ZP_FILE_CREATE_DISPOSITION Disposition;
    ULONG MaxEntries;
    NTSTATUS Status;

    *Response = NULL;
    *ResponseLength = 0;
    *Channel = NULL;
    switch (OperationId)
    {
    case ZP_FILE_OPERATION_QUERY:
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ?
                   ZpFile_Query(&Path, Response, ResponseLength) : Status;

    case ZP_FILE_OPERATION_ENUMERATE:
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ?
                   ZpFile_Enumerate(&Path,
                                    NULL,
                                    MAXULONG,
                                    FALSE,
                                    Response,
                                    ResponseLength) :
                   Status;

    case ZP_FILE_OPERATION_ENUMERATE_PAGE:
        Status = ZpFile_DecodeEnumeratePageRequest(Request,
                                                   RequestLength,
                                                   &Path,
                                                   &Cursor,
                                                   &MaxEntries);
        return NT_SUCCESS(Status) ?
                   ZpFile_Enumerate(&Path,
                                    &Cursor,
                                    MaxEntries,
                                    TRUE,
                                    Response,
                                    ResponseLength) :
                   Status;

    case ZP_FILE_OPERATION_HASH:
        Status = ZpFile_DecodeHashRequest(Request,
                                          RequestLength,
                                          &Algorithm,
                                          &Path);
        return NT_SUCCESS(Status) ?
                   ZpFile_Hash(&Path,
                               Algorithm,
                               Pending,
                               Response,
                               ResponseLength) :
                   Status;

    case ZP_FILE_OPERATION_OPEN_READ:
        if (Client == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpFile_DecodeOpenReadRequest(Request,
                                              RequestLength,
                                              &Path,
                                              &Offset);
        if (NT_SUCCESS(Status))
        {
            Status = ZpFile_CreateReadChannel(Client,
                                              &Path,
                                              Offset,
                                              &FileChannel,
                                              &FileSize);
        }
        if (NT_SUCCESS(Status))
        {
            *ResponseLength = 3 * sizeof(ULONGLONG);
            *Response = Mem_Alloc(*ResponseLength);
            Status = *Response == NULL ? STATUS_NO_MEMORY :
                         ZpFile_EncodeOpenReadResponse(
                             FileChannel->ChannelId,
                             FileSize,
                             Offset,
                             *Response,
                             *ResponseLength,
                             ResponseLength);
            if (NT_SUCCESS(Status))
            {
                *Channel = FileChannel;
            }
            else
            {
                Mem_Free(*Response);
                *Response = NULL;
                ZpFile_CommitChannel(FileChannel, FALSE);
            }
        }
        return Status;

    case ZP_FILE_OPERATION_OPEN_WRITE:
        if (Client == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpFile_DecodeOpenWriteRequest(Request,
                                               RequestLength,
                                               &Path,
                                               &FileSize,
                                               &Disposition);
        if (NT_SUCCESS(Status))
        {
            Status = ZpFile_CreateWriteChannel(Client,
                                               &Path,
                                               FileSize,
                                               Disposition,
                                               &FileChannel);
        }
        if (NT_SUCCESS(Status))
        {
            *ResponseLength = 2 * sizeof(ULONGLONG);
            *Response = Mem_Alloc(*ResponseLength);
            Status = *Response == NULL ? STATUS_NO_MEMORY :
                         ZpFile_EncodeOpenWriteResponse(
                             FileChannel->ChannelId,
                             FileSize,
                             *Response,
                             *ResponseLength,
                             ResponseLength);
            if (NT_SUCCESS(Status))
            {
                *Channel = FileChannel;
            }
            else
            {
                Mem_Free(*Response);
                *Response = NULL;
                ZpFile_CommitChannel(FileChannel, FALSE);
            }
        }
        return Status;

    default:
        return STATUS_NOT_SUPPORTED;
    }
}
