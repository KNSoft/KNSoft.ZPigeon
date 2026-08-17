#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <Bcrypt.h>

#pragma comment(lib, "Bcrypt.lib")

#define ZP_FILE_HASH_BUFFER_SIZE 0x00010000UL
#define ZP_FILE_CHANNEL_CHUNK_SIZE 0x00010000UL
#define ZP_FILE_WRITE_WINDOW_SIZE 0x00100000UL
#define ZP_FILE_SETTABLE_ATTRIBUTES \
    (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM | \
     FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_OFFLINE | \
     FILE_ATTRIBUTE_NOT_CONTENT_INDEXED)
typedef struct _ZP_FILE_ENTRY
{
    ZP_FILE_INFO Info;
    ULONG NameLength;
    WCHAR Name[ANYSIZE_ARRAY];
} ZP_FILE_ENTRY, *PZP_FILE_ENTRY;

typedef struct _ZP_FILE_ENUMERATION
{
    ULONGLONG Id;
    HANDLE Directory;
    FILE_FIND Find;
    PFILE_DIRECTORY_INFORMATION Current;
    ULONG Remaining;
    LOGICAL FindInitialized;
} ZP_FILE_ENUMERATION, *PZP_FILE_ENUMERATION;

typedef union _ZP_FILE_DIRECTORY_BUFFER
{
    ULONG_PTR Alignment;
    BYTE Buffer[4096];
} ZP_FILE_DIRECTORY_BUFFER, *PZP_FILE_DIRECTORY_BUFFER;

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
LOGICAL
ZpFile_IsDotDirectory(_In_ PFILE_DIRECTORY_INFORMATION Data)
{
    return (Data->FileNameLength == sizeof(WCHAR) &&
            Data->FileName[0] == L'.') ||
           (Data->FileNameLength == 2 * sizeof(WCHAR) &&
            Data->FileName[0] == L'.' &&
            Data->FileName[1] == L'.');
}

static
BOOLEAN
ZpFile_HasChildDirectories(
    _In_ HANDLE Parent,
    _In_ PFILE_DIRECTORY_INFORMATION Data)
{
    FILE_FIND Find;
    PFILE_DIRECTORY_INFORMATION Child;
    OBJECT_ATTRIBUTES Object;
    IO_STATUS_BLOCK IoStatusBlock;
    UNICODE_STRING Name;
    HANDLE Directory;
    ULONG Remaining;
    NTSTATUS Status;
    BOOLEAN HasChildren = FALSE;
    LOGICAL FindInitialized = FALSE;

    if (!(Data->FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        Data->FileNameLength > MAXUSHORT)
    {
        return FALSE;
    }
    Name.Buffer = Data->FileName;
    Name.Length = (USHORT)Data->FileNameLength;
    Name.MaximumLength = Name.Length;
    InitializeObjectAttributes(&Object,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               Parent,
                               NULL);
    Status = NtOpenFile(&Directory,
                        FILE_LIST_DIRECTORY | SYNCHRONIZE,
                        &Object,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status))
    {
        return FALSE;
    }
    Status = IO_BeginFindFile(&Find,
                              Directory,
                              NULL,
                              FileDirectoryInformation);
    FindInitialized = NT_SUCCESS(Status);
    while (NT_SUCCESS(Status) && Find.HasData && !HasChildren)
    {
        Child = Find.Buffer;
        Remaining = Find.Length;
        for (;;)
        {
            if (Remaining < UFIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) ||
                Child->FileNameLength >
                    Remaining - UFIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) ||
                (Child->FileNameLength & (sizeof(WCHAR) - 1)) != 0)
            {
                Status = STATUS_DATA_ERROR;
                break;
            }
            if (!ZpFile_IsDotDirectory(Child) &&
                (Child->FileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                HasChildren = TRUE;
                break;
            }
            if (Child->NextEntryOffset == 0)
            {
                break;
            }
            if (Child->NextEntryOffset > Remaining ||
                Child->NextEntryOffset <
                    UFIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName))
            {
                Status = STATUS_DATA_ERROR;
                break;
            }
            Remaining -= Child->NextEntryOffset;
            Child = Add2Ptr(Child, Child->NextEntryOffset);
        }
        if (NT_SUCCESS(Status) && !HasChildren)
        {
            Status = IO_ContinueFindFileFind(&Find);
        }
    }
    if (FindInitialized)
    {
        IO_EndFindFile(&Find);
    }
    NtClose(Directory);
    return HasChildren;
}

static
VOID
ZpFile_SetInfo(
    _In_ HANDLE Parent,
    _In_ PFILE_DIRECTORY_INFORMATION Data,
    _Out_ PZP_FILE_INFO Info)
{
    Info->Attributes = Data->FileAttributes;
    // The directory enumeration already returned the size; do not reopen the file.
    Info->Size = Data->EndOfFile.QuadPart;
    Info->CreationTime = Data->CreationTime.QuadPart;
    Info->LastAccessTime = Data->LastAccessTime.QuadPart;
    Info->LastWriteTime = Data->LastWriteTime.QuadPart;
    Info->HasChildren = ZpFile_HasChildDirectories(Parent, Data);
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
    Info.HasChildren = FALSE;
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
NTSTATUS
ZpFile_OpenForControl(
    _In_ PCZP_STRING_VIEW Path,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE File)
{
    PUNICODE_STRING String;
    OBJECT_ATTRIBUTES Object;
    UNICODE_STRING NativePath;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    String = ZpFile_CopyPath(Path);
    if (String == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = NT_InitWin32PathObject(&Object, String->Buffer, NULL, &NativePath);
    NT_FreeStringW(String);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = NtOpenFile(File,
                        DesiredAccess | SYNCHRONIZE,
                        &Object,
                        &IoStatusBlock,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_SYNCHRONOUS_IO_NONALERT);
    NT_FreeNtPath(&NativePath);
    return Status;
}

static
NTSTATUS
ZpFile_Delete(
    _In_ PCZP_STRING_VIEW Path)
{
    FILE_DISPOSITION_INFORMATION Disposition = { TRUE };
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE File;
    NTSTATUS Status;

    Status = ZpFile_OpenForControl(Path, DELETE, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtSetInformationFile(File,
                                      &IoStatusBlock,
                                      &Disposition,
                                      sizeof(Disposition),
                                      FileDispositionInformation);
        NtClose(File);
    }
    return Status;
}

static
NTSTATUS
ZpFile_Rename(
    _In_ PCZP_STRING_VIEW Path,
    _In_ PCZP_STRING_VIEW NewPath)
{
    PUNICODE_STRING NewPathString;
    PFILE_RENAME_INFORMATION_EX Rename;
    UNICODE_STRING NativePath;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE File;
    SIZE_T RenameSize;
    NTSTATUS Status;

    Status = ZpFile_OpenForControl(Path, DELETE, &File);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    NewPathString = ZpFile_CopyPath(NewPath);
    if (NewPathString == NULL)
    {
        NtClose(File);
        return STATUS_NO_MEMORY;
    }
    Status = NT_Win32PathToNtPath(NewPathString->Buffer, NULL, &NativePath);
    NT_FreeStringW(NewPathString);
    if (NT_SUCCESS(Status))
    {
        RenameSize = UFIELD_OFFSET(FILE_RENAME_INFORMATION_EX, FileName) + NativePath.Length;
        Rename = Mem_Alloc(RenameSize);
        if (Rename == NULL)
        {
            Status = STATUS_NO_MEMORY;
        }
        else
        {
            Rename->Flags = 0;
            Rename->RootDirectory = NULL;
            Rename->FileNameLength = NativePath.Length;
            RtlCopyMemory(Rename->FileName, NativePath.Buffer, NativePath.Length);
            Status = NtSetInformationFile(File,
                                          &IoStatusBlock,
                                          Rename,
                                          (ULONG)RenameSize,
                                          FileRenameInformationEx);
            Mem_Free(Rename);
        }
        NT_FreeNtPath(&NativePath);
    }
    NtClose(File);
    return Status;
}

static
NTSTATUS
ZpFile_SetAttributes(
    _In_ PCZP_STRING_VIEW Path,
    _In_ ULONG Attributes)
{
    FILE_BASIC_INFORMATION Basic;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE File;
    ULONG Mask;
    NTSTATUS Status;

    Status = ZpFile_OpenForControl(Path,
                                   FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                                   &File);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = NtQueryInformationFile(File,
                                    &IoStatusBlock,
                                    &Basic,
                                    sizeof(Basic),
                                    FileBasicInformation);
    if (NT_SUCCESS(Status))
    {
        Mask = FlagOn(Basic.FileAttributes, FILE_ATTRIBUTE_DIRECTORY) ?
                   FILE_ATTRIBUTE_HIDDEN : ZP_FILE_SETTABLE_ATTRIBUTES;
        if (FlagOn(Attributes, ~Mask))
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        else
        {
            Basic.FileAttributes &= ~(Mask | FILE_ATTRIBUTE_NORMAL);
            Basic.FileAttributes |= Attributes;
            if (Basic.FileAttributes == 0)
            {
                Basic.FileAttributes = FILE_ATTRIBUTE_NORMAL;
            }
            Status = NtSetInformationFile(File,
                                          &IoStatusBlock,
                                          &Basic,
                                          sizeof(Basic),
                                          FileBasicInformation);
        }
    }
    NtClose(File);
    return Status;
}

static
NTSTATUS
ZpFile_QueryVolume(
    _In_ PCZP_STRING_VIEW Path,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    BYTE VolumeBuffer[FIELD_OFFSET(FILE_FS_VOLUME_INFORMATION, VolumeLabel) +
                      (MAX_PATH + 1) * sizeof(WCHAR)];
    BYTE AttributeBuffer[FIELD_OFFSET(FILE_FS_ATTRIBUTE_INFORMATION, FileSystemName) +
                         (MAX_PATH + 1) * sizeof(WCHAR)];
    PFILE_FS_VOLUME_INFORMATION Volume = (PFILE_FS_VOLUME_INFORMATION)VolumeBuffer;
    PFILE_FS_ATTRIBUTE_INFORMATION Attribute = (PFILE_FS_ATTRIBUTE_INFORMATION)AttributeBuffer;
    FILE_FS_SIZE_INFORMATION Size;
    ZP_FILE_VOLUME_INFO Info;
    IO_STATUS_BLOCK IoStatusBlock;
    HANDLE File;
    ULONG Length;
    NTSTATUS Status;

    if (Path->Length != 3 || Path->Buffer[1] != L':' || Path->Buffer[2] != L'\\') return STATUS_INVALID_PARAMETER;
    Status = ZpFile_OpenForControl(Path, FILE_READ_ATTRIBUTES, &File);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryVolumeInformationFile(File,
                                          &IoStatusBlock,
                                          Volume,
                                          sizeof(VolumeBuffer),
                                          FileFsVolumeInformation);
    if (NT_SUCCESS(Status)) Status = NtQueryVolumeInformationFile(File,
                                                                  &IoStatusBlock,
                                                                  Attribute,
                                                                  sizeof(AttributeBuffer),
                                                                  FileFsAttributeInformation);
    if (NT_SUCCESS(Status)) Status = NtQueryVolumeInformationFile(File,
                                                                  &IoStatusBlock,
                                                                  &Size,
                                                                  sizeof(Size),
                                                                  FileFsSizeInformation);
    NtClose(File);
    if (!NT_SUCCESS(Status)) return Status;
    Info.TotalBytes = (ULONGLONG)Size.TotalAllocationUnits.QuadPart *
                      Size.SectorsPerAllocationUnit * Size.BytesPerSector;
    Info.FreeBytes = (ULONGLONG)Size.AvailableAllocationUnits.QuadPart *
                     Size.SectorsPerAllocationUnit * Size.BytesPerSector;
    Info.SerialNumber = Volume->VolumeSerialNumber;
    Info.MaximumComponentLength = Attribute->MaximumComponentNameLength;
    Info.FileSystemFlags = Attribute->FileSystemAttributes;
    Info.Label = Volume->VolumeLabel;
    Info.LabelLength = Volume->VolumeLabelLength / sizeof(WCHAR);
    Info.FileSystem = Attribute->FileSystemName;
    Info.FileSystemLength = Attribute->FileSystemNameLength / sizeof(WCHAR);
    Status = ZpFile_EncodeVolumeInfo(&Info, NULL, 0, &Length);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpFile_EncodeVolumeInfo(&Info, *Response, Length, ResponseLength);
    if (!NT_SUCCESS(Status)) Mem_Free(*Response);
    return Status;
}

static
NTSTATUS
ZpFile_SetVolumeLabel(
    _In_ PCZP_STRING_VIEW Path,
    _In_ PCZP_STRING_VIEW Label)
{
    PFILE_FS_LABEL_INFORMATION Information;
    IO_STATUS_BLOCK IoStatusBlock;
    SIZE_T Size;
    HANDLE File;
    NTSTATUS Status;

    if (Path->Length != 3 || Path->Buffer[1] != L':' || Path->Buffer[2] != L'\\') return STATUS_INVALID_PARAMETER;
    if (Label->Length > MAXUSHORT / sizeof(WCHAR)) return STATUS_NAME_TOO_LONG;
    Size = FIELD_OFFSET(FILE_FS_LABEL_INFORMATION, VolumeLabel) +
           (SIZE_T)Label->Length * sizeof(WCHAR);
    Information = Mem_Alloc(Size);
    if (Information == NULL) return STATUS_NO_MEMORY;
    Information->VolumeLabelLength = Label->Length * sizeof(WCHAR);
    RtlCopyMemory(Information->VolumeLabel,
                  Label->Buffer,
                  Information->VolumeLabelLength);
    Status = ZpFile_OpenForControl(Path, FILE_WRITE_DATA, &File);
    if (NT_SUCCESS(Status))
    {
        Status = NtSetVolumeInformationFile(File,
                                            &IoStatusBlock,
                                            Information,
                                            (ULONG)Size,
                                            FileFsLabelInformation);
        NtClose(File);
    }
    Mem_Free(Information);
    return Status;
}

static
VOID
ZpFile_DestroyEnumeration(
    _In_ PZP_FILE_ENUMERATION Enumeration)
{
    if (Enumeration->FindInitialized)
    {
        IO_EndFindFile(&Enumeration->Find);
    }
    if (Enumeration->Directory != NULL)
    {
        NtClose(Enumeration->Directory);
    }
    Mem_Free(Enumeration);
}

VOID
ZpFile_ResetEnumeration(
    _Inout_ PZP_CLIENT_OBJECT Client)
{
    PZP_FILE_ENUMERATION Enumeration;

    RtlAcquireSRWLockExclusive(&Client->FileEnumerationLock);
    Enumeration = Client->FileEnumeration;
    Client->FileEnumeration = NULL;
    RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
    if (Enumeration != NULL)
    {
        ZpFile_DestroyEnumeration(Enumeration);
    }
}

static
NTSTATUS
ZpFile_ValidateEnumerationEntry(
    _In_ PZP_FILE_ENUMERATION Enumeration)
{
    PFILE_DIRECTORY_INFORMATION Data = Enumeration->Current;

    return Enumeration->Remaining >=
               UFIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) &&
           Data->FileNameLength <=
               Enumeration->Remaining -
                   UFIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) &&
           (Data->FileNameLength & (sizeof(WCHAR) - 1)) == 0 ?
               STATUS_SUCCESS : STATUS_DATA_ERROR;
}

static
NTSTATUS
ZpFile_MoveEnumeration(
    _Inout_ PZP_FILE_ENUMERATION Enumeration)
{
    PFILE_DIRECTORY_INFORMATION Data = Enumeration->Current;
    NTSTATUS Status;

    if (Data->NextEntryOffset != 0)
    {
        if (Data->NextEntryOffset > Enumeration->Remaining ||
            Data->NextEntryOffset <
                UFIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) +
                    Data->FileNameLength)
        {
            return STATUS_DATA_ERROR;
        }
        Enumeration->Remaining -= Data->NextEntryOffset;
        Enumeration->Current = Add2Ptr(Data, Data->NextEntryOffset);
        return STATUS_SUCCESS;
    }
    Status = IO_ContinueFindFileFind(&Enumeration->Find);
    Enumeration->Current = NT_SUCCESS(Status) && Enumeration->Find.HasData ?
                               Enumeration->Find.Buffer : NULL;
    Enumeration->Remaining = Enumeration->Find.Length;
    return Status;
}

static
NTSTATUS
ZpFile_SkipDotDirectories(
    _Inout_ PZP_FILE_ENUMERATION Enumeration)
{
    NTSTATUS Status;

    while (Enumeration->Current != NULL)
    {
        Status = ZpFile_ValidateEnumerationEntry(Enumeration);
        if (!NT_SUCCESS(Status) ||
            !ZpFile_IsDotDirectory(Enumeration->Current))
        {
            return Status;
        }
        Status = ZpFile_MoveEnumeration(Enumeration);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFile_CreateEnumeration(
    _In_ PCZP_STRING_VIEW Path,
    _In_ ULONGLONG Id,
    _Outptr_ PZP_FILE_ENUMERATION* Result)
{
    PZP_FILE_ENUMERATION Enumeration;
    PUNICODE_STRING PathString;
    UNICODE_STRING NativePath = { 0 };
    NTSTATUS Status;

    PathString = ZpFile_CopyPath(Path);
    if (PathString == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = NT_Win32PathToNtPath(PathString->Buffer, NULL, &NativePath);
    NT_FreeStringW(PathString);
    Enumeration = NT_SUCCESS(Status) ? Mem_Alloc(sizeof(*Enumeration)) : NULL;
    if (NT_SUCCESS(Status) && Enumeration == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(Enumeration, sizeof(*Enumeration));
        Enumeration->Id = Id;
        Status = IO_OpenDirectory(&Enumeration->Directory,
                                  &NativePath,
                                  FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE |
                                      FILE_SHARE_DELETE);
    }
    if (NativePath.Buffer != NULL)
    {
        NT_FreeNtPath(&NativePath);
    }
    if (NT_SUCCESS(Status))
    {
        Status = IO_BeginFindFile(&Enumeration->Find,
                                  Enumeration->Directory,
                                  NULL,
                                  FileDirectoryInformation);
        Enumeration->FindInitialized = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        Enumeration->Current = Enumeration->Find.HasData ?
                                   Enumeration->Find.Buffer : NULL;
        Enumeration->Remaining = Enumeration->Find.Length;
        Status = ZpFile_SkipDotDirectories(Enumeration);
    }
    if (!NT_SUCCESS(Status))
    {
        if (Enumeration != NULL)
        {
            ZpFile_DestroyEnumeration(Enumeration);
            Enumeration = NULL;
        }
        Enumeration = NULL;
        return Status;
    }
    *Result = Enumeration;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFile_ReadEnumerationPage(
    _Inout_ PZP_FILE_ENUMERATION Enumeration,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_FILE_ENTRY Entries[ZP_FILE_PAGE_COUNT];
    ZP_FILE_RECORD Records[ZP_FILE_PAGE_COUNT];
    PZP_FILE_ENTRY Entry;
    SIZE_T AllocationSize;
    ULONG Count = 0, Index;
    NTSTATUS Status = STATUS_SUCCESS;

    while (Enumeration->Current != NULL && Count < ZP_FILE_PAGE_COUNT)
    {
        AllocationSize = UFIELD_OFFSET(ZP_FILE_ENTRY, Name) +
                         Enumeration->Current->FileNameLength;
        Entry = Mem_Alloc(AllocationSize);
        if (Entry == NULL)
        {
            Status = STATUS_NO_MEMORY;
            break;
        }
        ZpFile_SetInfo(Enumeration->Directory,
                       Enumeration->Current,
                       &Entry->Info);
        Entry->NameLength =
            Enumeration->Current->FileNameLength / sizeof(WCHAR);
        RtlCopyMemory(Entry->Name,
                      Enumeration->Current->FileName,
                      Enumeration->Current->FileNameLength);
        Entries[Count] = Entry;
        Records[Count].Info = Entry->Info;
        Records[Count].Name = Entry->Name;
        Records[Count].NameLength = Entry->NameLength;
        Count++;
        Status = ZpFile_MoveEnumeration(Enumeration);
        if (NT_SUCCESS(Status))
        {
            Status = ZpFile_SkipDotDirectories(Enumeration);
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodePage(Records,
                                   Count,
                                   Enumeration->Current != NULL ?
                                       Enumeration->Id : 0,
                                   NULL,
                                   0,
                                   ResponseLength);
        *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
        if (NT_SUCCESS(Status) && *Response == NULL)
        {
            Status = STATUS_NO_MEMORY;
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpFile_EncodePage(Records,
                                       Count,
                                       Enumeration->Current != NULL ?
                                           Enumeration->Id : 0,
                                       *Response,
                                       *ResponseLength,
                                       ResponseLength);
        }
    }
    for (Index = 0; Index < Count; Index++)
    {
        Mem_Free(Entries[Index]);
    }
    return Status;
}

static
NTSTATUS
ZpFile_EnumerateDrives(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static UNICODE_STRING DirectoryName = RTL_CONSTANT_STRING(L"\\GLOBAL??");
    static UNICODE_STRING SymbolicLinkName = RTL_CONSTANT_STRING(L"SymbolicLink");
    ZP_FILE_DIRECTORY_BUFFER Buffer;
    ZP_FILE_RECORD Records[26];
    WCHAR Names[26][3];
    BOOLEAN Drives[26] = { 0 };
    OBJECT_ATTRIBUTES ObjectAttributes;
    POBJECT_DIRECTORY_INFORMATION Information;
    HANDLE Directory;
    ULONG Context = 0, Count = 0, Index;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               &DirectoryName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenDirectoryObject(&Directory,
                                   DIRECTORY_QUERY,
                                   &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    for (;;)
    {
        Status = NtQueryDirectoryObject(Directory,
                                        Buffer.Buffer,
                                        sizeof(Buffer.Buffer),
                                        TRUE,
                                        Context == 0,
                                        &Context,
                                        NULL);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }
        Information = (POBJECT_DIRECTORY_INFORMATION)Buffer.Buffer;
        if (Information->Name.Length == 2 * sizeof(WCHAR) &&
            Information->Name.Buffer[1] == L':' &&
            RtlEqualUnicodeString(&Information->TypeName,
                                  &SymbolicLinkName,
                                  FALSE))
        {
            WCHAR Letter = RtlUpcaseUnicodeChar(Information->Name.Buffer[0]);

            if (Letter >= L'A' && Letter <= L'Z')
            {
                Drives[Letter - L'A'] = TRUE;
            }
        }
    }
    NtClose(Directory);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    for (Index = 0; Index < ARRAYSIZE(Drives); Index++)
    {
        if (!Drives[Index])
        {
            continue;
        }
        Names[Count][0] = (WCHAR)(L'A' + Index);
        Names[Count][1] = L':';
        Names[Count][2] = L'\\';
        RtlZeroMemory(&Records[Count].Info, sizeof(Records[Count].Info));
        Records[Count].Info.Attributes = FILE_ATTRIBUTE_DIRECTORY;
        Records[Count].Info.HasChildren = TRUE;
        Records[Count].Name = Names[Count];
        Records[Count].NameLength = ARRAYSIZE(Names[Count]);
        Count++;
    }
    Status = ZpFile_EncodePage(Records,
                               Count,
                               0,
                               NULL,
                               0,
                               ResponseLength);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (NT_SUCCESS(Status) && *Response == NULL)
    {
        Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodePage(Records,
                                   Count,
                                   0,
                                   *Response,
                                   *ResponseLength,
                                   ResponseLength);
    }
    return Status;
}

static
NTSTATUS
ZpFile_EnumeratePage(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_STRING_VIEW Path,
    _In_ ULONGLONG EnumerationId,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_FILE_ENUMERATION Enumeration;
    NTSTATUS Status;

    RtlAcquireSRWLockShared(&Client->Lock);
    if (Client->State != ZpClientStateReady)
    {
        RtlReleaseSRWLockShared(&Client->Lock);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    RtlAcquireSRWLockExclusive(&Client->FileEnumerationLock);
    RtlReleaseSRWLockShared(&Client->Lock);
    Enumeration = Client->FileEnumeration;
    if (EnumerationId == 0 && Path->Length == 0)
    {
        Client->FileEnumeration = NULL;
        if (Enumeration != NULL)
        {
            ZpFile_DestroyEnumeration(Enumeration);
        }
        Status = ZpFile_EnumerateDrives(Response, ResponseLength);
        RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
        return Status;
    }
    if (EnumerationId == 0)
    {
        Client->FileEnumeration = NULL;
        if (Enumeration != NULL)
        {
            ZpFile_DestroyEnumeration(Enumeration);
            Enumeration = NULL;
        }
        EnumerationId = Client->NextFileEnumerationId++;
        if (Client->NextFileEnumerationId == 0)
        {
            Client->NextFileEnumerationId = 1;
        }
        Status = ZpFile_CreateEnumeration(Path,
                                          EnumerationId,
                                          &Enumeration);
        if (NT_SUCCESS(Status))
        {
            Client->FileEnumeration = Enumeration;
        }
    }
    else
    {
        if (Enumeration == NULL || Enumeration->Id != EnumerationId)
        {
            RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
            return STATUS_INVALID_HANDLE;
        }
        Status = STATUS_SUCCESS;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_ReadEnumerationPage(Enumeration,
                                            Response,
                                            ResponseLength);
    }
    if (!NT_SUCCESS(Status) || Enumeration->Current == NULL)
    {
        if (Client->FileEnumeration == Enumeration)
        {
            Client->FileEnumeration = NULL;
        }
        if (Enumeration != NULL)
        {
            ZpFile_DestroyEnumeration(Enumeration);
        }
    }
    RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
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
    PCWSTR AlgorithmName;
    PUNICODE_STRING PathString;
    HANDLE File = NULL;
    PBYTE Buffer = NULL;
    BYTE Digest[ZP_FILE_SHA256_SIZE];
    ULONGLONG FileSize;
    ULONG BytesRead, Crc32 = 0, DigestLength;
    NTSTATUS Status;

    switch (Algorithm)
    {
    case ZpFileHashCrc32:
        AlgorithmName = NULL;
        DigestLength = ZP_FILE_CRC32_SIZE;
        break;
    case ZpFileHashMd5:
        AlgorithmName = BCRYPT_MD5_ALGORITHM;
        DigestLength = ZP_FILE_MD5_SIZE;
        break;
    case ZpFileHashSha1:
        AlgorithmName = BCRYPT_SHA1_ALGORITHM;
        DigestLength = ZP_FILE_SHA1_SIZE;
        break;
    case ZpFileHashSha256:
        AlgorithmName = BCRYPT_SHA256_ALGORITHM;
        DigestLength = ZP_FILE_SHA256_SIZE;
        break;
    default:
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
    if (NT_SUCCESS(Status) && AlgorithmName != NULL)
    {
        Status = BCryptOpenAlgorithmProvider(&AlgorithmHandle,
                                             AlgorithmName,
                                             NULL,
                                             0);
    }
    if (NT_SUCCESS(Status) && AlgorithmName != NULL)
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
        if (HashHandle != NULL)
        {
            Status = BCryptHashData(HashHandle, Buffer, BytesRead, 0);
        }
        else
        {
            Crc32 = RtlComputeCrc32(Crc32, Buffer, BytesRead);
        }
    }
    if (NT_SUCCESS(Status) && HashHandle != NULL)
    {
        Status = BCryptFinishHash(HashHandle,
                                  Digest,
                                  DigestLength,
                                  0);
    }
    else if (NT_SUCCESS(Status))
    {
        Digest[0] = (BYTE)(Crc32 >> 24);
        Digest[1] = (BYTE)(Crc32 >> 16);
        Digest[2] = (BYTE)(Crc32 >> 8);
        Digest[3] = (BYTE)Crc32;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeHashResponse(Algorithm,
                                           FileSize,
                                           Digest,
                                           DigestLength,
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
                                           DigestLength,
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
    ZP_STRING_VIEW Path, NewPath;
    ZP_FILE_HASH_ALGORITHM Algorithm;
    PZP_CLIENT_FILE_CHANNEL FileChannel = NULL;
    ULONGLONG FileSize, Offset, EnumerationId;
    ULONG Attributes;
    ZP_FILE_CREATE_DISPOSITION Disposition;
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

    case ZP_FILE_OPERATION_ENUMERATE_PAGE:
        if (Client == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Status = ZpFile_DecodeEnumeratePageRequest(Request,
                                                   RequestLength,
                                                   &Path,
                                                   &EnumerationId);
        return NT_SUCCESS(Status) ?
                    ZpFile_EnumeratePage(Client,
                                         &Path,
                                         EnumerationId,
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

    case ZP_FILE_OPERATION_DELETE:
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ? ZpFile_Delete(&Path) : Status;

    case ZP_FILE_OPERATION_RENAME:
        Status = ZpFile_DecodeRenameRequest(Request, RequestLength, &Path, &NewPath);
        return NT_SUCCESS(Status) ? ZpFile_Rename(&Path, &NewPath) : Status;

    case ZP_FILE_OPERATION_SET_ATTRIBUTES:
        Status = ZpFile_DecodeSetAttributesRequest(Request,
                                                   RequestLength,
                                                   &Path,
                                                   &Attributes);
        return NT_SUCCESS(Status) ?
                   ZpFile_SetAttributes(&Path, Attributes) : Status;

    case ZP_FILE_OPERATION_QUERY_VOLUME:
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ? ZpFile_QueryVolume(&Path, Response, ResponseLength) : Status;

    case ZP_FILE_OPERATION_SET_VOLUME_LABEL:
        Status = ZpFile_DecodeRenameRequest(Request, RequestLength, &Path, &NewPath);
        return NT_SUCCESS(Status) ? ZpFile_SetVolumeLabel(&Path, &NewPath) : Status;

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
