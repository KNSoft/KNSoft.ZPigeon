#include "Client.h"

#include "Download.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Account.h"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Security.h"
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <Bcrypt.h>
#include <Winsvc.h>

#pragma comment(lib, "Bcrypt.lib")

#define ZP_FILE_HASH_BUFFER_SIZE 0x00100000UL
#define ZP_FILE_CHANNEL_CHUNK_SIZE 0x00010000UL
#define ZP_FILE_WRITE_WINDOW_SIZE 0x00100000UL
#define ZP_FILE_ENUMERATION_MAX_COUNT 16
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
    LIST_ENTRY ListEntry;
    ULONG Id;
    HANDLE Directory;
    FILE_FIND Find;
    PUNICODE_STRING Filter;
    PFILE_DIRECTORY_INFORMATION Current;
    WCHAR Group;
    BOOLEAN FindInitialized;
    BOOLEAN QueryChildren;
} ZP_FILE_ENUMERATION, *PZP_FILE_ENUMERATION;

typedef union _ZP_FILE_DIRECTORY_BUFFER
{
    ULONG_PTR Alignment;
    BYTE Buffer[4096];
} ZP_FILE_DIRECTORY_BUFFER, *PZP_FILE_DIRECTORY_BUFFER;

typedef struct _ZP_FILE_OWNER_ALLOCATION
{
    PUNICODE_STRING ImagePath;
    PUNICODE_STRING CommandLine;
    PWSTR ServiceNames;
} ZP_FILE_OWNER_ALLOCATION, *PZP_FILE_OWNER_ALLOCATION;

typedef BYTE ZP_CLIENT_FILE_CHANNEL_TYPE;

#define ZpClientFileChannelRead ((ZP_CLIENT_FILE_CHANNEL_TYPE)0)
#define ZpClientFileChannelWrite ((ZP_CLIENT_FILE_CHANNEL_TYPE)1)

struct _ZP_CLIENT_FILE_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    BOOLEAN WorkerActive;
    ZP_CLIENT_FILE_CHANNEL_TYPE Type;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    ULONGLONG RemainingBytes;
    ULONGLONG Offset;
    HANDLE File;
    PUNICODE_STRING FinalPath;
    PUNICODE_STRING TemporaryPath;
    ZP_FILE_CREATE_DISPOSITION Disposition;
    BOOLEAN OwnsTemporaryPath;
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
    return ZpClient_SendLocked(Object,
                               MessageType == ZpMessageChannelData ?
                                   ZP_SEND_FLAG_COMPRESSIBLE | ZP_SEND_FLAG_BULK :
                                   ZP_SEND_FLAG_BULK,
                               MessageType,
                               Body,
                               BodyLength,
                               NULL,
                               0);
}

static
NTSTATUS
ZpFile_SendCloseLocked(
    _In_ PZP_CLIENT_FILE_CHANNEL Channel,
    _In_ NTSTATUS CloseStatus)
{
    BYTE Body[sizeof(ULONG) + ZP_STATUS_MAX_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->Header.ChannelId,
                                          ZpStatus_FromNtStatus(CloseStatus),
                                          Body,
                                          sizeof(Body),
                                          &BodyLength);
    return NT_SUCCESS(Status) ?
               ZpFile_SendLocked(Channel->Header.Owner,
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
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
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
_Success_(NT_SUCCESS(return))
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
            ZpFile_CommitChannel,
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
_Success_(NT_SUCCESS(return))
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
            ZpFile_CommitChannel,
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
    BYTE Body[2 * sizeof(ULONG)];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelWindow(Channel->Header.ChannelId,
                                           CreditBytes,
                                           Body,
                                           sizeof(Body),
                                           &BodyLength);
    if (NT_SUCCESS(Status))
    {
        Channel->ReceiveCredit += CreditBytes;
        Status = ZpFile_SendLocked(Channel->Header.Owner,
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
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
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
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    PBYTE Body;
    LARGE_INTEGER Offset;
    ULONG ReadLength, BytesRead, BodyLength;
    NTSTATUS Status;
    LOGICAL Removed = FALSE;

    UNREFERENCED_PARAMETER(Instance);
    Body = Mem_Alloc(sizeof(ULONG) + ZP_FILE_CHANNEL_CHUNK_SIZE);
    if (Body == NULL)
    {
        ZpFile_FinishWorker(Channel, STATUS_NO_MEMORY, TRUE);
        return;
    }
    Status = ZpMessage_EncodeChannelDataHeader(Channel->Header.ChannelId, Body);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Body);
        ZpFile_FinishWorker(Channel, Status, TRUE);
        return;
    }
    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (!Channel->Header.Pending)
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
                             Add2Ptr(Body, sizeof(ULONG)),
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
        BodyLength = sizeof(ULONG) + BytesRead;
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (!Channel->Header.Pending)
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
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_FILE_CHANNEL Channel = (PZP_CLIENT_FILE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
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
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Queue = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || Channel->Type != ZpClientFileChannelRead ||
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
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    ULONG BytesWritten, CreditBytes;
    NTSTATUS Status;
    LOGICAL Removed = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || Channel->Type != ZpClientFileChannelWrite ||
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
        NTSTATUS CompletionStatus = Status;

        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        Status = ZpFile_SendCloseLocked(Channel, CompletionStatus);
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
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
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
        for (;;)
        {
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
    _In_ BOOLEAN QueryChildren,
    _Out_ PZP_FILE_INFO Info)
{
    Info->Attributes = Data->FileAttributes;
    // The directory enumeration already returned the size; do not reopen the file.
    Info->Size = Data->EndOfFile.QuadPart;
    Info->CreationTime = Data->CreationTime.QuadPart;
    Info->LastAccessTime = Data->LastAccessTime.QuadPart;
    Info->LastWriteTime = Data->LastWriteTime.QuadPart;
    Info->HasChildren = QueryChildren && ZpFile_HasChildDirectories(Parent, Data);
}

static
NTSTATUS
ZpFile_QueryDirectoryEntry(
    _In_ PCZP_STRING_VIEW Path,
    _Out_ PZP_FILE_INFO Info)
{
    FILE_FIND Find;
    PFILE_DIRECTORY_INFORMATION Data;
    PUNICODE_STRING String;
    UNICODE_STRING NativePath, DirectoryPath, Name, Candidate;
    HANDLE Directory;
    ULONG CharacterCount, Index;
    NTSTATUS Status;

    String = ZpFile_CopyPath(Path);
    if (String == NULL) return STATUS_NO_MEMORY;
    Status = NT_Win32PathToNtPath(String->Buffer, NULL, &NativePath);
    NT_FreeStringW(String);
    if (!NT_SUCCESS(Status)) return Status;
    CharacterCount = NativePath.Length / sizeof(WCHAR);
    for (Index = CharacterCount; Index != 0 && NativePath.Buffer[Index - 1] != L'\\'; Index--)
    {
    }
    if (Index <= 1 || Index == CharacterCount)
    {
        NT_FreeNtPath(&NativePath);
        return STATUS_OBJECT_NAME_INVALID;
    }
    DirectoryPath.Buffer = NativePath.Buffer;
    DirectoryPath.Length = (USHORT)(Index * sizeof(WCHAR));
    DirectoryPath.MaximumLength = DirectoryPath.Length;
    Name.Buffer = NativePath.Buffer + Index;
    Name.Length = (USHORT)(NativePath.Length - Index * sizeof(WCHAR));
    Name.MaximumLength = Name.Length;
    Status = IO_OpenDirectory(&Directory,
                              &DirectoryPath,
                              FILE_LIST_DIRECTORY | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (NT_SUCCESS(Status))
    {
        Status = IO_BeginFindFile(&Find,
                                  Directory,
                                  &Name,
                                  FileDirectoryInformation);
        if (NT_SUCCESS(Status))
        {
            if (Find.HasData)
            {
                Data = Find.Buffer;
                Candidate.Buffer = Data->FileName;
                Candidate.Length = (USHORT)Data->FileNameLength;
                Candidate.MaximumLength = Candidate.Length;
                if (RtlEqualUnicodeString(&Name, &Candidate, TRUE))
                {
                    ZpFile_SetInfo(Directory, Data, FALSE, Info);
                }
                else
                {
                    Status = STATUS_OBJECT_NAME_NOT_FOUND;
                }
            }
            else
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }
            IO_EndFindFile(&Find);
        }
        NtClose(Directory);
    }
    NT_FreeNtPath(&NativePath);
    return Status;
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
    if (NT_SUCCESS(Status))
    {
        Info.Attributes = Data.FileAttributes;
        Info.Size = Data.EndOfFile.QuadPart;
        Info.CreationTime = Data.CreationTime.QuadPart;
        Info.LastAccessTime = Data.LastAccessTime.QuadPart;
        Info.LastWriteTime = Data.LastWriteTime.QuadPart;
        Info.HasChildren = FALSE;
    }
    else
    {
        Status = ZpFile_QueryDirectoryEntry(Path, &Info);
    }
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpFile_EncodeInfo(&Info, NULL, 0, ResponseLength);
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(*ResponseLength);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
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
ZpFile_WriteRange(
    _In_ PCZP_FILE_WRITE_RANGE_VIEW Request)
{
    FILE_STANDARD_INFORMATION Information;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Offset;
    HANDLE File;
    ULONG BytesWritten;
    NTSTATUS Status;

    Status = ZpFile_OpenForControl(&Request->Path,
                                   FILE_READ_ATTRIBUTES | FILE_WRITE_DATA,
                                   &File);
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryInformationFile(File,
                                    &IoStatusBlock,
                                    &Information,
                                    sizeof(Information),
                                    FileStandardInformation);
    if (NT_SUCCESS(Status) &&
        (Request->Offset > (ULONGLONG)Information.EndOfFile.QuadPart ||
         Request->Data.Length > (ULONGLONG)Information.EndOfFile.QuadPart - Request->Offset))
    {
        Status = STATUS_END_OF_FILE;
    }
    if (NT_SUCCESS(Status))
    {
        Offset.QuadPart = Request->Offset;
        Status = IO_WriteFile(File,
                              &Offset,
                              (PVOID)Request->Data.Buffer,
                              Request->Data.Length,
                              &BytesWritten);
        if (NT_SUCCESS(Status) && BytesWritten != Request->Data.Length) Status = STATUS_UNSUCCESSFUL;
    }
    NtClose(File);
    return Status;
}

static
NTSTATUS
ZpFile_EncodeStringResponse(
    _In_ PCUNICODE_STRING Value,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    NTSTATUS Status;

    Status = ZpFile_EncodePath(Value->Buffer,
                               Value->Length / sizeof(WCHAR),
                               NULL,
                               0,
                               ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    *Response = Mem_Alloc(*ResponseLength);
    if (*Response == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    return ZpFile_EncodePath(Value->Buffer,
                             Value->Length / sizeof(WCHAR),
                             *Response,
                             *ResponseLength,
                             ResponseLength);
}

static
NTSTATUS
ZpFile_QuerySecurity(
    _In_ PCZP_STRING_VIEW Path,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PUNICODE_STRING Sddl;
    HANDLE File;
    BOOLEAN DaclProtected;
    NTSTATUS Status;

    Status = ZpFile_OpenForControl(Path, READ_CONTROL, &File);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpSecurity_QueryDacl(File, &Sddl, &DaclProtected);
    NtClose(File);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = ZpFile_EncodeSecurityDescriptor(Sddl->Buffer,
                                              Sddl->Length / sizeof(WCHAR),
                                              DaclProtected,
                                              NULL,
                                              0,
                                              ResponseLength);
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(*ResponseLength);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeSecurityDescriptor(Sddl->Buffer,
                                                  Sddl->Length / sizeof(WCHAR),
                                                  DaclProtected,
                                                  *Response,
                                                  *ResponseLength,
                                                  ResponseLength);
    }
    NT_FreeStringW(Sddl);
    return Status;
}

static
NTSTATUS
ZpFile_SetSecurity(
    _In_ PCZP_FILE_SECURITY_REQUEST_VIEW Request)
{
    PUNICODE_STRING Value;
    HANDLE File;
    NTSTATUS Status;

    Value = ZpFile_CopyPath(&Request->Sddl);
    if (Value == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = ZpFile_OpenForControl(&Request->Path, READ_CONTROL | WRITE_DAC, &File);
    if (NT_SUCCESS(Status))
    {
        Status = ZpSecurity_SetDacl(File,
                                    SE_FILE_OBJECT,
                                    Value->Buffer,
                                    Request->DaclProtected);
        NtClose(File);
    }
    NT_FreeStringW(Value);
    return Status;
}

static
NTSTATUS
ZpFile_ResolveAccount(
    _In_ PCZP_STRING_VIEW Input,
    _In_ BOOLEAN SidToName,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PUNICODE_STRING Result, Value;
    NTSTATUS Status;

    Value = ZpFile_CopyPath(Input);
    if (Value == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = SidToName ? ZpAccount_QueryStringSidName(Value->Buffer, &Result) :
                         ZpAccount_QueryNameSid(Value->Buffer, &Result);
    NT_FreeStringW(Value);
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeStringResponse(Result, Response, ResponseLength);
        NT_FreeStringW(Result);
    }
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
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(Length);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status)) Status = ZpFile_EncodeVolumeInfo(&Info, *Response, Length, ResponseLength);
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
    if (Enumeration->Filter != NULL)
    {
        NT_FreeStringW(Enumeration->Filter);
    }
    Mem_Free(Enumeration);
}

static
VOID
ZpFile_ResetArchiveEnumerations(
    _Inout_ PZP_CLIENT_OBJECT Client);

VOID
ZpFile_ResetEnumeration(
    _Inout_ PZP_CLIENT_OBJECT Client)
{
    LIST_ENTRY Enumerations;
    PLIST_ENTRY Entry;
    PZP_FILE_ENUMERATION Enumeration;

    InitializeListHead(&Enumerations);
    RtlAcquireSRWLockExclusive(&Client->FileEnumerationLock);
    while (!IsListEmpty(&Client->FileEnumerations))
    {
        Entry = RemoveHeadList(&Client->FileEnumerations);
        InsertTailList(&Enumerations, Entry);
    }
    Client->FileEnumerationCount = 0;
    RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
    while (!IsListEmpty(&Enumerations))
    {
        Enumeration = CONTAINING_RECORD(RemoveHeadList(&Enumerations),
                                        ZP_FILE_ENUMERATION,
                                        ListEntry);
        ZpFile_DestroyEnumeration(Enumeration);
    }
    ZpFile_ResetArchiveEnumerations(Client);
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
        Enumeration->Current = Add2Ptr(Data, Data->NextEntryOffset);
        return STATUS_SUCCESS;
    }
    Status = IO_ContinueFindFileFind(&Enumeration->Find);
    Enumeration->Current = NT_SUCCESS(Status) && Enumeration->Find.HasData ?
                               Enumeration->Find.Buffer : NULL;
    return Status;
}

static
BOOLEAN
ZpFile_MatchesGroup(
    _In_ PZP_FILE_ENUMERATION Enumeration)
{
    WCHAR Character;

    if (Enumeration->Group == UNICODE_NULL)
    {
        return TRUE;
    }
    if (Enumeration->Current->FileNameLength == 0)
    {
        return Enumeration->Group == L'#';
    }
    Character = Enumeration->Current->FileName[0];
    if (Character >= L'a' && Character <= L'z')
    {
        Character -= L'a' - L'A';
    }
    return Enumeration->Group == L'#' ? Character < L'A' || Character > L'Z' :
                                        Character == Enumeration->Group;
}

static
NTSTATUS
ZpFile_SkipExcludedEntries(
    _Inout_ PZP_FILE_ENUMERATION Enumeration)
{
    NTSTATUS Status;

    while (Enumeration->Current != NULL)
    {
        if (!ZpFile_IsDotDirectory(Enumeration->Current) &&
            ZpFile_MatchesGroup(Enumeration))
        {
            return STATUS_SUCCESS;
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
    _In_opt_ PCZP_STRING_VIEW Filter,
    _In_ WCHAR Group,
    _In_ BOOLEAN QueryChildren,
    _In_ ULONG Id,
    _Outptr_ PZP_FILE_ENUMERATION* Result)
{
    PZP_FILE_ENUMERATION Enumeration;
    PUNICODE_STRING PathString;
    UNICODE_STRING NativePath;
    NTSTATUS Status;

    PathString = ZpFile_CopyPath(Path);
    if (PathString == NULL)
    {
        return STATUS_NO_MEMORY;
    }
    Status = NT_Win32PathToNtPath(PathString->Buffer, NULL, &NativePath);
    NT_FreeStringW(PathString);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Enumeration = Mem_Alloc(sizeof(*Enumeration));
    if (Enumeration == NULL)
    {
        NT_FreeNtPath(&NativePath);
        return STATUS_NO_MEMORY;
    }
    Enumeration->Id = Id;
    Enumeration->Directory = NULL;
    Enumeration->Filter = Filter != NULL && Filter->Length != 0 ? ZpFile_CopyPath(Filter) : NULL;
    Enumeration->Group = Group;
    Enumeration->FindInitialized = FALSE;
    Enumeration->QueryChildren = QueryChildren;
    if (Filter != NULL && Filter->Length != 0 && Enumeration->Filter == NULL)
    {
        NT_FreeNtPath(&NativePath);
        Mem_Free(Enumeration);
        return STATUS_NO_MEMORY;
    }
    Status = IO_OpenDirectory(&Enumeration->Directory,
                              &NativePath,
                              FILE_LIST_DIRECTORY | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    NT_FreeNtPath(&NativePath);
    if (NT_SUCCESS(Status))
    {
        Status = IO_BeginFindFile(&Enumeration->Find,
                                  Enumeration->Directory,
                                  Enumeration->Filter,
                                  FileDirectoryInformation);
        Enumeration->FindInitialized = NT_SUCCESS(Status);
    }
    if (NT_SUCCESS(Status))
    {
        Enumeration->Current = Enumeration->Find.HasData ?
                                   Enumeration->Find.Buffer : NULL;
        Status = ZpFile_SkipExcludedEntries(Enumeration);
    }
    if (!NT_SUCCESS(Status))
    {
        ZpFile_DestroyEnumeration(Enumeration);
        return Status;
    }
    *Result = Enumeration;
    return STATUS_SUCCESS;
}

static
INT
__cdecl
ZpFile_CompareEnumerationEntries(
    _In_opt_ PVOID Context,
    _In_ const VOID* Left,
    _In_ const VOID* Right)
{
    PZP_FILE_ENTRY LeftEntry = *(PZP_FILE_ENTRY*)Left;
    PZP_FILE_ENTRY RightEntry = *(PZP_FILE_ENTRY*)Right;
    INT Result;

    UNREFERENCED_PARAMETER(Context);
    Result = CompareStringEx(LOCALE_NAME_USER_DEFAULT,
                             NORM_IGNORECASE | SORT_DIGITSASNUMBERS,
                             LeftEntry->Name,
                             LeftEntry->NameLength,
                             RightEntry->Name,
                             RightEntry->NameLength,
                             NULL,
                             NULL,
                             0);
    return Result == 0 ? 0 : Result - CSTR_EQUAL;
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
                       Enumeration->QueryChildren,
                       &Entry->Info);
        Entry->NameLength =
            Enumeration->Current->FileNameLength / sizeof(WCHAR);
        RtlCopyMemory(Entry->Name,
                      Enumeration->Current->FileName,
                      Enumeration->Current->FileNameLength);
        Entries[Count] = Entry;
        Count++;
        Status = ZpFile_MoveEnumeration(Enumeration);
        if (NT_SUCCESS(Status))
        {
            Status = ZpFile_SkipExcludedEntries(Enumeration);
        }
        if (!NT_SUCCESS(Status))
        {
            break;
        }
    }
    if (NT_SUCCESS(Status))
    {
        qsort_s(Entries,
                Count,
                sizeof(Entries[0]),
                ZpFile_CompareEnumerationEntries,
                NULL);
        for (Index = 0; Index < Count; Index++)
        {
            Records[Index].Info = Entries[Index]->Info;
            Records[Index].Name = Entries[Index]->Name;
            Records[Index].NameLength = Entries[Index]->NameLength;
        }
        Status = ZpFile_EncodePage(Records,
                                   Count,
                                   Enumeration->Current != NULL ?
                                       Enumeration->Id : 0,
                                   NULL,
                                   0,
                                   ResponseLength);
        if (NT_SUCCESS(Status))
        {
            *Response = Mem_Alloc(*ResponseLength);
            if (*Response == NULL) Status = STATUS_NO_MEMORY;
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
    PROCESS_DEVICEMAP_INFORMATION DeviceMap;
    ZP_FILE_RECORD Records[26];
    WCHAR Names[26][3];
    ULONG Count = 0, Index;
    NTSTATUS Status;

    Status = NtQueryInformationProcess(NtCurrentProcess(),
                                       ProcessDeviceMap,
                                       &DeviceMap.Query,
                                       sizeof(DeviceMap.Query),
                                       NULL);
    if (!NT_SUCCESS(Status)) return Status;
    for (Index = 0; Index < 26; Index++)
    {
        if (!FlagOn(DeviceMap.Query.DriveMap, 1UL << Index)) continue;
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
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(*ResponseLength);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
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
    _In_opt_ PCZP_STRING_VIEW Filter,
    _In_ WCHAR Group,
    _In_ ULONG EnumerationId,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PLIST_ENTRY Entry;
    PZP_FILE_ENUMERATION Enumeration;
    ULONG Index;
    NTSTATUS Status;

    if (Filter != NULL)
    {
        for (Index = 0; Index < Filter->Length; Index++)
        {
            if (Filter->Buffer[Index] == L'\\' || Filter->Buffer[Index] == L'/' ||
                Filter->Buffer[Index] == L':')
            {
                return STATUS_INVALID_PARAMETER;
            }
        }
    }
    RtlAcquireSRWLockShared(&Client->Lock);
    if (Client->State != ZpClientStateReady)
    {
        RtlReleaseSRWLockShared(&Client->Lock);
        return STATUS_CONNECTION_DISCONNECTED;
    }
    RtlAcquireSRWLockExclusive(&Client->FileEnumerationLock);
    RtlReleaseSRWLockShared(&Client->Lock);
    if (EnumerationId == 0 && Path->Length == 0)
    {
        RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
        return ZpFile_EnumerateDrives(Response, ResponseLength);
    }
    if (EnumerationId == 0)
    {
        if (Client->FileEnumerationCount >= ZP_FILE_ENUMERATION_MAX_COUNT)
        {
            RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
            return STATUS_QUOTA_EXCEEDED;
        }
        EnumerationId = Client->NextFileEnumerationId++;
        if (Client->NextFileEnumerationId == 0)
        {
            Client->NextFileEnumerationId = 1;
        }
        Status = ZpFile_CreateEnumeration(Path,
                                          Filter,
                                          Group,
                                          Filter == NULL,
                                          EnumerationId,
                                          &Enumeration);
        if (!NT_SUCCESS(Status))
        {
            RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
            return Status;
        }
        InsertTailList(&Client->FileEnumerations, &Enumeration->ListEntry);
        Client->FileEnumerationCount++;
    }
    else
    {
        Enumeration = NULL;
        for (Entry = Client->FileEnumerations.Flink;
             Entry != &Client->FileEnumerations;
             Entry = Entry->Flink)
        {
            Enumeration = CONTAINING_RECORD(Entry,
                                            ZP_FILE_ENUMERATION,
                                            ListEntry);
            if (Enumeration->Id == EnumerationId)
            {
                break;
            }
            Enumeration = NULL;
        }
        if (Enumeration == NULL)
        {
            RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
            return STATUS_INVALID_HANDLE;
        }
    }
    Status = ZpFile_ReadEnumerationPage(Enumeration,
                                        Response,
                                        ResponseLength);
    if (!NT_SUCCESS(Status) || Enumeration->Current == NULL)
    {
        RemoveEntryList(&Enumeration->ListEntry);
        Client->FileEnumerationCount--;
        ZpFile_DestroyEnumeration(Enumeration);
    }
    RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
    return Status;
}

static
NTSTATUS
ZpFile_CloseEnumeration(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ ULONG EnumerationId)
{
    PLIST_ENTRY Entry;
    PZP_FILE_ENUMERATION Enumeration = NULL;

    if (EnumerationId == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    RtlAcquireSRWLockExclusive(&Client->FileEnumerationLock);
    for (Entry = Client->FileEnumerations.Flink;
         Entry != &Client->FileEnumerations;
         Entry = Entry->Flink)
    {
        Enumeration = CONTAINING_RECORD(Entry, ZP_FILE_ENUMERATION, ListEntry);
        if (Enumeration->Id == EnumerationId)
        {
            RemoveEntryList(Entry);
            Client->FileEnumerationCount--;
            break;
        }
        Enumeration = NULL;
    }
    RtlReleaseSRWLockExclusive(&Client->FileEnumerationLock);
    if (Enumeration == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    ZpFile_DestroyEnumeration(Enumeration);
    return STATUS_SUCCESS;
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
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(*ResponseLength);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
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

static
NTSTATUS
ZpFile_QueryProcessString(
    _In_ HANDLE Process,
    _In_ PROCESSINFOCLASS InformationClass,
    _Outptr_ PUNICODE_STRING* String)
{
    PUNICODE_STRING Buffer;
    ULONG Length = sizeof(UNICODE_STRING) + MAX_PATH * sizeof(WCHAR);
    NTSTATUS Status;

    Buffer = Mem_Alloc(Length);
    if (Buffer == NULL) return STATUS_NO_MEMORY;
    Status = NtQueryInformationProcess(Process, InformationClass, Buffer, Length, &Length);
    if (Status == STATUS_INFO_LENGTH_MISMATCH)
    {
        Mem_Free(Buffer);
        Buffer = Mem_Alloc(Length);
        if (Buffer == NULL) return STATUS_NO_MEMORY;
        Status = NtQueryInformationProcess(Process, InformationClass, Buffer, Length, NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *String = Buffer;
    return STATUS_SUCCESS;
}

static
PSYSTEM_PROCESS_INFORMATION
ZpFile_FindProcess(
    _In_ PVOID SystemProcesses,
    _In_ ULONG ProcessId)
{
    PSYSTEM_PROCESS_INFORMATION Process = SystemProcesses;

    for (;;)
    {
        if ((ULONG)(ULONG_PTR)Process->UniqueProcessId == ProcessId) return Process;
        if (Process->NextEntryOffset == 0) return NULL;
        Process = Add2Ptr(Process, Process->NextEntryOffset);
    }
}

static
NTSTATUS
ZpFile_QueryOwnerServices(
    _Inout_updates_(OwnerCount) PZP_FILE_OWNER_RECORD Owners,
    _Inout_updates_(OwnerCount) PZP_FILE_OWNER_ALLOCATION Allocations,
    _In_ ULONG OwnerCount)
{
    LPENUM_SERVICE_STATUS_PROCESSW Services;
    SC_HANDLE Manager;
    DWORD Error, Length = 0, ServiceCount = 0, Resume = 0;
    ULONG OwnerIndex, ServiceIndex;

    Manager = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
    if (Manager == NULL) return NTSTATUS_FROM_WIN32(GetLastError());
    if (EnumServicesStatusExW(Manager,
                              SC_ENUM_PROCESS_INFO,
                              SERVICE_WIN32,
                              SERVICE_STATE_ALL,
                              NULL,
                              0,
                              &Length,
                              &ServiceCount,
                              &Resume,
                              NULL))
    {
        CloseServiceHandle(Manager);
        return STATUS_SUCCESS;
    }
    Error = GetLastError();
    if (Error != ERROR_MORE_DATA)
    {
        CloseServiceHandle(Manager);
        return Error == ERROR_SUCCESS ? STATUS_SUCCESS : NTSTATUS_FROM_WIN32(Error);
    }
    Services = Mem_Alloc(Length);
    if (Services == NULL)
    {
        CloseServiceHandle(Manager);
        return STATUS_NO_MEMORY;
    }
    Resume = 0;
    if (!EnumServicesStatusExW(Manager,
                               SC_ENUM_PROCESS_INFO,
                               SERVICE_WIN32,
                               SERVICE_STATE_ALL,
                               (PBYTE)Services,
                               Length,
                               &Length,
                               &ServiceCount,
                               &Resume,
                               NULL))
    {
        Error = GetLastError();
        Mem_Free(Services);
        CloseServiceHandle(Manager);
        return NTSTATUS_FROM_WIN32(Error);
    }
    for (OwnerIndex = 0; OwnerIndex < OwnerCount; OwnerIndex++)
    {
        SIZE_T NameLength = 0;
        PWSTR Cursor;

        for (ServiceIndex = 0; ServiceIndex < ServiceCount; ServiceIndex++)
        {
            if (Services[ServiceIndex].ServiceStatusProcess.dwProcessId == Owners[OwnerIndex].ProcessId)
            {
                NameLength += wcslen(Services[ServiceIndex].lpServiceName) + (NameLength != 0);
            }
        }
        if (NameLength == 0) continue;
        Allocations[OwnerIndex].ServiceNames = Mem_Alloc((NameLength + 1) * sizeof(WCHAR));
        if (Allocations[OwnerIndex].ServiceNames == NULL)
        {
            Mem_Free(Services);
            CloseServiceHandle(Manager);
            return STATUS_NO_MEMORY;
        }
        Cursor = Allocations[OwnerIndex].ServiceNames;
        for (ServiceIndex = 0; ServiceIndex < ServiceCount; ServiceIndex++)
        {
            PCWSTR Name = Services[ServiceIndex].lpServiceName;
            SIZE_T CurrentLength;

            if (Services[ServiceIndex].ServiceStatusProcess.dwProcessId != Owners[OwnerIndex].ProcessId) continue;
            if (Cursor != Allocations[OwnerIndex].ServiceNames) *Cursor++ = UNICODE_NULL;
            CurrentLength = wcslen(Name);
            RtlCopyMemory(Cursor, Name, CurrentLength * sizeof(WCHAR));
            Cursor += CurrentLength;
        }
        *Cursor = UNICODE_NULL;
        Owners[OwnerIndex].ServiceNames = Allocations[OwnerIndex].ServiceNames;
        Owners[OwnerIndex].ServiceNamesLength = (ULONG)NameLength;
    }
    Mem_Free(Services);
    CloseServiceHandle(Manager);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFile_QueryOwnerProcessIds(
    _In_ HANDLE File,
    _Outptr_ PFILE_PROCESS_IDS_USING_FILE_INFORMATION* Information)
{
    PFILE_PROCESS_IDS_USING_FILE_INFORMATION Buffer;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG Length = FIELD_OFFSET(FILE_PROCESS_IDS_USING_FILE_INFORMATION, ProcessIdList) + 64 * sizeof(HANDLE);
    NTSTATUS Status;

    for (;;)
    {
        Buffer = Mem_Alloc(Length);
        if (Buffer == NULL) return STATUS_NO_MEMORY;
        Status = NtQueryInformationFile(File,
                                        &IoStatusBlock,
                                        Buffer,
                                        Length,
                                        FileProcessIdsUsingFileInformation);
        if (Status != STATUS_INFO_LENGTH_MISMATCH && Status != STATUS_BUFFER_OVERFLOW &&
            Status != STATUS_BUFFER_TOO_SMALL)
        {
            break;
        }
        Mem_Free(Buffer);
        if (Length > ZP_RESPONSE_MAX_PAYLOAD_SIZE / 2) return STATUS_BUFFER_OVERFLOW;
        Length *= 2;
    }
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Information = Buffer;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpFile_QueryOwners(
    _In_ PCZP_STRING_VIEW Path,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PFILE_PROCESS_IDS_USING_FILE_INFORMATION ProcessIds;
    PZP_FILE_OWNER_ALLOCATION Allocations;
    PZP_FILE_OWNER_RECORD Owners;
    PVOID SystemProcesses;
    HANDLE File, Process;
    ULONG Index, Length;
    NTSTATUS Status;

    Status = ZpFile_OpenForControl(Path, FILE_READ_ATTRIBUTES, &File);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpFile_QueryOwnerProcessIds(File, &ProcessIds);
    NtClose(File);
    if (!NT_SUCCESS(Status)) return Status;
    if (ProcessIds->NumberOfProcessIdsInList == 0)
    {
        Status = ZpFile_EncodeOwnerList(NULL, 0, NULL, 0, &Length);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(ProcessIds);
            return Status;
        }
        *Response = Mem_Alloc(Length);
        if (*Response == NULL)
        {
            Mem_Free(ProcessIds);
            return STATUS_NO_MEMORY;
        }
        Status = ZpFile_EncodeOwnerList(NULL, 0, *Response, Length, ResponseLength);
        Mem_Free(ProcessIds);
        return Status;
    }
    Status = Sys_QueryDynamicInfo(SystemProcessInformation, &SystemProcesses);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(ProcessIds);
        return Status;
    }
    Owners = Mem_Alloc((SIZE_T)ProcessIds->NumberOfProcessIdsInList * sizeof(*Owners));
    Allocations = Mem_Alloc((SIZE_T)ProcessIds->NumberOfProcessIdsInList * sizeof(*Allocations));
    if (Owners == NULL || Allocations == NULL)
    {
        Mem_Free(Owners);
        Mem_Free(Allocations);
        Sys_FreeInfo(SystemProcesses);
        Mem_Free(ProcessIds);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Owners, (SIZE_T)ProcessIds->NumberOfProcessIdsInList * sizeof(*Owners));
    RtlZeroMemory(Allocations, (SIZE_T)ProcessIds->NumberOfProcessIdsInList * sizeof(*Allocations));
    for (Index = 0; Index < ProcessIds->NumberOfProcessIdsInList; Index++)
    {
        PSYSTEM_PROCESS_INFORMATION Entry;

        Owners[Index].ProcessId = (ULONG)(ULONG_PTR)ProcessIds->ProcessIdList[Index];
        Entry = ZpFile_FindProcess(SystemProcesses, Owners[Index].ProcessId);
        if (Entry != NULL)
        {
            Owners[Index].ImageName = Entry->ImageName.Buffer;
            Owners[Index].ImageNameLength = Entry->ImageName.Length / sizeof(WCHAR);
        }
        Owners[Index].ImagePathStatus = PS_OpenProcess(&Process,
                                                       PROCESS_QUERY_LIMITED_INFORMATION,
                                                       Owners[Index].ProcessId);
        Owners[Index].CommandLineStatus = Owners[Index].ImagePathStatus;
        if (!NT_SUCCESS(Owners[Index].ImagePathStatus)) continue;
        Owners[Index].ImagePathStatus = ZpFile_QueryProcessString(Process,
                                                                  ProcessImageFileNameWin32,
                                                                  &Allocations[Index].ImagePath);
        if (NT_SUCCESS(Owners[Index].ImagePathStatus))
        {
            Owners[Index].ImagePath = Allocations[Index].ImagePath->Buffer;
            Owners[Index].ImagePathLength = Allocations[Index].ImagePath->Length / sizeof(WCHAR);
        }
        Owners[Index].CommandLineStatus = ZpFile_QueryProcessString(Process,
                                                                    ProcessCommandLineInformation,
                                                                    &Allocations[Index].CommandLine);
        if (NT_SUCCESS(Owners[Index].CommandLineStatus))
        {
            Owners[Index].CommandLine = Allocations[Index].CommandLine->Buffer;
            Owners[Index].CommandLineLength = Allocations[Index].CommandLine->Length / sizeof(WCHAR);
        }
        NtClose(Process);
    }
    Status = ZpFile_QueryOwnerServices(Owners, Allocations, ProcessIds->NumberOfProcessIdsInList);
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeOwnerList(Owners,
                                        ProcessIds->NumberOfProcessIdsInList,
                                        NULL,
                                        0,
                                        &Length);
    }
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(Length);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeOwnerList(Owners,
                                        ProcessIds->NumberOfProcessIdsInList,
                                        *Response,
                                        Length,
                                        ResponseLength);
    }
    for (Index = 0; Index < ProcessIds->NumberOfProcessIdsInList; Index++)
    {
        Mem_Free(Allocations[Index].ImagePath);
        Mem_Free(Allocations[Index].CommandLine);
        Mem_Free(Allocations[Index].ServiceNames);
    }
    Mem_Free(Allocations);
    Mem_Free(Owners);
    Sys_FreeInfo(SystemProcesses);
    Mem_Free(ProcessIds);
    return Status;
}

static
NTSTATUS
ZpFile_QueryId(
    _In_ HANDLE File,
    _Out_ PFILE_ID_INFORMATION Information)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    Status = NtQueryInformationFile(File,
                                    &IoStatusBlock,
                                    Information,
                                    sizeof(*Information),
                                    FileIdInformation);
    if (Status == STATUS_PENDING)
    {
        Status = NtWaitForSingleObject(File, FALSE, NULL);
        if (NT_SUCCESS(Status)) Status = IoStatusBlock.Status;
    }
    return Status;
}

static
USHORT
ZpFile_FindObjectTypeIndex(
    _In_ PSYSTEM_HANDLE_INFORMATION_EX Handles,
    _In_ HANDLE Handle)
{
    ULONG_PTR Index;

    for (Index = 0; Index < Handles->NumberOfHandles; Index++)
    {
        if ((ULONG)(ULONG_PTR)Handles->Handles[Index].UniqueProcessId == GetCurrentProcessId() &&
            Handles->Handles[Index].HandleValue == Handle)
        {
            return Handles->Handles[Index].ObjectTypeIndex;
        }
    }
    return 0;
}

static
NTSTATUS
ZpFile_CloseOwnerHandles(
    _In_ ULONG ProcessId,
    _In_ const FILE_ID_INFORMATION* FileId,
    _In_ USHORT FileTypeIndex,
    _In_ PSYSTEM_HANDLE_INFORMATION_EX Handles,
    _Out_ PULONG ClosedCount)
{
    FILE_ID_INFORMATION CandidateId;
    HANDLE Process, Candidate;
    ULONG_PTR Index;
    NTSTATUS Status, Result = STATUS_NOT_FOUND;

    if (ProcessId == GetCurrentProcessId()) return STATUS_ACCESS_DENIED;
    Status = PS_OpenProcess(&Process, PROCESS_DUP_HANDLE, ProcessId);
    if (!NT_SUCCESS(Status)) return Status;
    *ClosedCount = 0;
    for (Index = 0; Index < Handles->NumberOfHandles; Index++)
    {
        PSYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Entry = &Handles->Handles[Index];

        if ((ULONG)(ULONG_PTR)Entry->UniqueProcessId != ProcessId || Entry->ObjectTypeIndex != FileTypeIndex) continue;
        Status = NtDuplicateObject(Process,
                                   Entry->HandleValue,
                                   NtCurrentProcess(),
                                   &Candidate,
                                   SYNCHRONIZE,
                                   0,
                                   0);
        if (!NT_SUCCESS(Status)) continue;
        Status = ZpFile_QueryId(Candidate, &CandidateId);
        if (NT_SUCCESS(Status) && RtlEqualMemory(&CandidateId, FileId, sizeof(CandidateId)))
        {
            Status = NtDuplicateObject(Process,
                                       Entry->HandleValue,
                                       NULL,
                                       NULL,
                                       0,
                                       0,
                                       DUPLICATE_CLOSE_SOURCE);
            if (NT_SUCCESS(Status))
            {
                (*ClosedCount)++;
                if (Result == STATUS_NOT_FOUND) Result = STATUS_SUCCESS;
            }
            else Result = Status;
        }
        NtClose(Candidate);
    }
    NtClose(Process);
    return Result;
}

static
NTSTATUS
ZpFile_ControlOwners(
    _In_ PCZP_FILE_OWNER_CONTROL_REQUEST_VIEW Request,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_FILE_OWNER_CONTROL_RESULT Results;
    PSYSTEM_HANDLE_INFORMATION_EX Handles = NULL;
    FILE_ID_INFORMATION FileId;
    HANDLE File = NULL, Process;
    USHORT FileTypeIndex = 0;
    ULONG Index, Length;
    NTSTATUS Status;

    Results = Mem_Alloc((SIZE_T)Request->ProcessCount * sizeof(*Results));
    if (Results == NULL) return STATUS_NO_MEMORY;
    if (Request->Control == ZpFileOwnerCloseHandles)
    {
        Status = ZpFile_OpenForControl(&Request->Path, FILE_READ_ATTRIBUTES, &File);
        if (NT_SUCCESS(Status)) Status = ZpFile_QueryId(File, &FileId);
        if (NT_SUCCESS(Status)) Status = Sys_QueryDynamicInfo(SystemExtendedHandleInformation, (PVOID*)&Handles);
        if (NT_SUCCESS(Status)) FileTypeIndex = ZpFile_FindObjectTypeIndex(Handles, File);
        if (NT_SUCCESS(Status) && FileTypeIndex == 0) Status = STATUS_OBJECT_TYPE_MISMATCH;
        if (File != NULL) NtClose(File);
        if (!NT_SUCCESS(Status))
        {
            Mem_Free(Results);
            Sys_FreeInfo(Handles);
            return Status;
        }
    }
    for (Index = 0; Index < Request->ProcessCount; Index++)
    {
        Status = ZpFile_GetOwnerControlProcessId(Request, Index, &Results[Index].ProcessId);
        Results[Index].AffectedHandleCount = 0;
        if (!NT_SUCCESS(Status)) break;
        if (Request->Control == ZpFileOwnerTerminate)
        {
            if (Results[Index].ProcessId == GetCurrentProcessId())
            {
                Results[Index].Status = STATUS_ACCESS_DENIED;
            }
            else
            {
                Results[Index].Status = PS_OpenProcess(&Process, PROCESS_TERMINATE, Results[Index].ProcessId);
                if (NT_SUCCESS(Results[Index].Status))
                {
                    Results[Index].Status = NtTerminateProcess(Process, STATUS_SUCCESS);
                    NtClose(Process);
                    if (NT_SUCCESS(Results[Index].Status)) Results[Index].AffectedHandleCount = 1;
                }
            }
        }
        else
        {
            Results[Index].Status = ZpFile_CloseOwnerHandles(Results[Index].ProcessId,
                                                             &FileId,
                                                             FileTypeIndex,
                                                             Handles,
                                                             &Results[Index].AffectedHandleCount);
        }
    }
    Sys_FreeInfo(Handles);
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeOwnerControlResults(Results, Request->ProcessCount, NULL, 0, &Length);
    }
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(Length);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeOwnerControlResults(Results,
                                                  Request->ProcessCount,
                                                  *Response,
                                                  Length,
                                                  ResponseLength);
    }
    Mem_Free(Results);
    return Status;
}

static
NTSTATUS
ZpFile_EnumerateDownloads(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    PZP_FILE_DOWNLOAD_SNAPSHOT Snapshot;
    PCZP_FILE_DOWNLOAD_RECORD Records;
    ULONG Count;
    NTSTATUS Status;

    Status = ZpFileDownload_CreateSnapshot(&Snapshot, &Records, &Count);
    if (!NT_SUCCESS(Status)) return Status;
    Status = ZpFile_EncodeDownloadRecords(Records, Count, NULL, 0, ResponseLength);
    if (NT_SUCCESS(Status))
    {
        *Response = Mem_Alloc(*ResponseLength);
        if (*Response == NULL) Status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodeDownloadRecords(Records,
                                              Count,
                                              *Response,
                                              *ResponseLength,
                                              ResponseLength);
    }
    ZpFileDownload_DestroySnapshot(Snapshot);
    return Status;
}

#include "Archive.inl"
#include "Image.inl"
#include "Shortcut.inl"

NTSTATUS
ZpFile_Execute(
    _Inout_opt_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _In_ volatile LONG* Pending,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_LOCAL_CHANNEL* Channel)
{
    ZP_STRING_VIEW Path, NewPath, Filter;
    ZP_FILE_SECURITY_REQUEST_VIEW Security;
    ZP_FILE_WRITE_RANGE_VIEW WriteRange;
    ZP_FILE_HASH_ALGORITHM Algorithm;
    ZP_FILE_OWNER_CONTROL_REQUEST_VIEW OwnerControl;
    ZP_FILE_DOWNLOAD_REQUEST_VIEW Download;
    GUID DownloadId;
    PZP_CLIENT_FILE_CHANNEL FileChannel;
    ULONGLONG FileSize, Offset;
    ULONG Attributes, EnumerationId;
    WCHAR Group;
    ZP_FILE_IMAGE_PREVIEW_QUALITY ImageQuality;
    ZP_FILE_CREATE_DISPOSITION Disposition;
    NTSTATUS Status;

    if (OperationId == ZP_FILE_OPERATION_QUERY)
    {
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ? ZpFile_Query(&Path, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_ENUMERATE_PAGE)
    {
        Status = ZpFile_DecodeEnumeratePageRequest(Request,
                                                   RequestLength,
                                                   &Path,
                                                   &EnumerationId);
        return NT_SUCCESS(Status) ?
                   ZpFile_EnumeratePage(Client,
                                        &Path,
                                        NULL,
                                        UNICODE_NULL,
                                        EnumerationId,
                                        Response,
                                        ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_ENUMERATE_FILTERED_PAGE)
    {
        Status = ZpFile_DecodeFilteredPageRequest(Request,
                                                  RequestLength,
                                                  &Path,
                                                  &Filter,
                                                  &Group,
                                                  &EnumerationId);
        return NT_SUCCESS(Status) ?
                   ZpFile_EnumeratePage(Client,
                                        &Path,
                                        &Filter,
                                        Group,
                                        EnumerationId,
                                        Response,
                                        ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_CLOSE_ENUMERATION)
    {
        Status = ZpFile_DecodeEnumeratePageRequest(Request,
                                                   RequestLength,
                                                   &Path,
                                                   &EnumerationId);
        return NT_SUCCESS(Status) ? ZpFile_CloseEnumeration(Client, EnumerationId) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_HASH)
    {
        Status = ZpFile_DecodeHashRequest(Request, RequestLength, &Algorithm, &Path);
        return NT_SUCCESS(Status) ?
                   ZpFile_Hash(&Path, Algorithm, Pending, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_DELETE)
    {
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ? ZpFile_Delete(&Path) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_RENAME)
    {
        Status = ZpFile_DecodeRenameRequest(Request, RequestLength, &Path, &NewPath);
        return NT_SUCCESS(Status) ? ZpFile_Rename(&Path, &NewPath) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_SET_ATTRIBUTES)
    {
        Status = ZpFile_DecodeSetAttributesRequest(Request,
                                                   RequestLength,
                                                   &Path,
                                                   &Attributes);
        return NT_SUCCESS(Status) ? ZpFile_SetAttributes(&Path, Attributes) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_WRITE_RANGE)
    {
        Status = ZpFile_DecodeWriteRangeRequest(Request, RequestLength, &WriteRange);
        return NT_SUCCESS(Status) ? ZpFile_WriteRange(&WriteRange) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_QUERY_OWNERS)
    {
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ? ZpFile_QueryOwners(&Path, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_CONTROL_OWNERS)
    {
        Status = ZpFile_DecodeOwnerControlRequest(Request, RequestLength, &OwnerControl);
        return NT_SUCCESS(Status) ? ZpFile_ControlOwners(&OwnerControl, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_START_DOWNLOAD)
    {
        Status = ZpFile_DecodeDownloadRequest(Request, RequestLength, &Download);
        return NT_SUCCESS(Status) ?
                   ZpFileDownload_Start(Download.Engine,
                                        Download.Flags,
                                        &Download.Id,
                                        Download.Url.Buffer,
                                        Download.Url.Length,
                                        Download.Path.Buffer,
                                        Download.Path.Length) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_ENUMERATE_DOWNLOADS)
    {
        return RequestLength == 0 ?
                   ZpFile_EnumerateDownloads(Response, ResponseLength) : STATUS_DATA_ERROR;
    }
    else if (OperationId == ZP_FILE_OPERATION_CANCEL_DOWNLOAD)
    {
        ZP_CODEC_READER Reader;

        if (RequestLength != ZP_FILE_DOWNLOAD_ID_SIZE) return STATUS_DATA_ERROR;
        ZpCodec_InitializeReader(&Reader, Request, RequestLength);
        Status = ZpCodec_ReadGuid(&Reader, &DownloadId);
        return NT_SUCCESS(Status) ? ZpFileDownload_Cancel(&DownloadId) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_ENUMERATE_ARCHIVE_PAGE)
    {
        Status = ZpFile_DecodeEnumeratePageRequest(Request,
                                                   RequestLength,
                                                   &Path,
                                                   &EnumerationId);
        return NT_SUCCESS(Status) ?
                   ZpFile_EnumerateArchivePage(Client,
                                               &Path,
                                               EnumerationId,
                                               Pending,
                                               Response,
                                               ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_QUERY_SHORTCUT)
    {
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ? ZpFile_QueryShortcut(&Path, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_PREVIEW_IMAGE)
    {
        Status = ZpFile_DecodeImagePreviewRequest(Request,
                                                  RequestLength,
                                                  &Path,
                                                  &ImageQuality);
        return NT_SUCCESS(Status) ?
                   ZpFile_PreviewImage(&Path, ImageQuality, Pending, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_QUERY_VOLUME)
    {
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ? ZpFile_QueryVolume(&Path, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_SET_VOLUME_LABEL)
    {
        Status = ZpFile_DecodeRenameRequest(Request, RequestLength, &Path, &NewPath);
        return NT_SUCCESS(Status) ? ZpFile_SetVolumeLabel(&Path, &NewPath) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_QUERY_SECURITY)
    {
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ? ZpFile_QuerySecurity(&Path, Response, ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_SET_SECURITY)
    {
        Status = ZpFile_DecodeSecurityRequest(Request, RequestLength, &Security);
        return NT_SUCCESS(Status) ? ZpFile_SetSecurity(&Security) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_RESOLVE_ACCOUNT ||
             OperationId == ZP_FILE_OPERATION_RESOLVE_SID)
    {
        Status = ZpFile_DecodePath(Request, RequestLength, &Path);
        return NT_SUCCESS(Status) ?
                   ZpFile_ResolveAccount(&Path,
                                         OperationId == ZP_FILE_OPERATION_RESOLVE_SID,
                                         Response,
                                         ResponseLength) : Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_OPEN_READ)
    {
        Status = ZpFile_DecodeOpenReadRequest(Request,
                                              RequestLength,
                                              &Path,
                                              &Offset);
        if (!NT_SUCCESS(Status)) return Status;
        Status = ZpFile_CreateReadChannel(Client,
                                          &Path,
                                          Offset,
                                          &FileChannel,
                                          &FileSize);
        if (!NT_SUCCESS(Status)) return Status;
        *ResponseLength = 3 * sizeof(ULONGLONG);
        *Response = Mem_Alloc(*ResponseLength);
        Status = *Response == NULL ? STATUS_NO_MEMORY :
                     ZpFile_EncodeOpenReadResponse(FileChannel->Header.ChannelId,
                                                   FileSize,
                                                   Offset,
                                                   *Response,
                                                   *ResponseLength,
                                                   ResponseLength);
        if (NT_SUCCESS(Status))
        {
            *Channel = &FileChannel->Header;
        }
        else ZpFile_CommitChannel(&FileChannel->Header, FALSE);
        return Status;
    }
    else if (OperationId == ZP_FILE_OPERATION_OPEN_WRITE)
    {
        Status = ZpFile_DecodeOpenWriteRequest(Request,
                                               RequestLength,
                                               &Path,
                                               &FileSize,
                                               &Disposition);
        if (!NT_SUCCESS(Status)) return Status;
        Status = ZpFile_CreateWriteChannel(Client,
                                           &Path,
                                           FileSize,
                                           Disposition,
                                           &FileChannel);
        if (!NT_SUCCESS(Status)) return Status;
        *ResponseLength = 2 * sizeof(ULONGLONG);
        *Response = Mem_Alloc(*ResponseLength);
        Status = *Response == NULL ? STATUS_NO_MEMORY :
                     ZpFile_EncodeOpenWriteResponse(FileChannel->Header.ChannelId,
                                                    FileSize,
                                                    *Response,
                                                    *ResponseLength,
                                                    ResponseLength);
        if (NT_SUCCESS(Status))
        {
            *Channel = &FileChannel->Header;
        }
        else ZpFile_CommitChannel(&FileChannel->Header, FALSE);
        return Status;
    }
    return STATUS_NOT_SUPPORTED;
}
