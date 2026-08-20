#include "Client.h"

#include "Capture.h"
#include "Shared.h"
#include "../Rtc/Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <KNSoft/NDK/NT/Win32K/Win32KApi.h>

#pragma comment(lib, "KNSoft.NDK.Win32u.lib")

#define ZP_WINDOW_CLASS_CCH 256
#define ZP_WINDOW_CAPTURE_CHUNK_SIZE 0x00040000UL

typedef struct _ZP_WINDOW_ENTRY
{
    ZP_WINDOW_RECORD Record;
    WCHAR Caption[ZP_WINDOW_CAPTION_MAX_CCH];
    WCHAR ClassName[ZP_WINDOW_CLASS_CCH];
} ZP_WINDOW_ENTRY, *PZP_WINDOW_ENTRY;

struct _ZP_CLIENT_WINDOW_CAPTURE_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    LOGICAL WorkerActive;
    ULONGLONG Credit;
    HANDLE CreditEvent;
    HANDLE WorkerThread;
    ZP_WINDOW_CAPTURE_OPTIONS Options;
};

static
ZP_STATUS
ZpWindow_StatusFromLastError(VOID)
{
    ULONG Error = GetLastError();

    return ZpStatus_FromCode(ZpStatusWin32,
                             Error != ERROR_SUCCESS ? Error : ERROR_INVALID_WINDOW_HANDLE);
}

static
ULONG
ZpWindow_GetFlags(
    _In_ HWND Window,
    _In_ const WINDOWINFO* Info)
{
    ULONG Flags = 0;

    if (FlagOn(Info->dwStyle, WS_VISIBLE)) Flags |= ZP_WINDOW_FLAG_VISIBLE;
    if (!FlagOn(Info->dwStyle, WS_DISABLED)) Flags |= ZP_WINDOW_FLAG_ENABLED;
    if (IsWindowUnicode(Window)) Flags |= ZP_WINDOW_FLAG_UNICODE;
    if (FlagOn(Info->dwStyle, WS_MINIMIZE)) Flags |= ZP_WINDOW_FLAG_MINIMIZED;
    if (FlagOn(Info->dwStyle, WS_MAXIMIZE)) Flags |= ZP_WINDOW_FLAG_MAXIMIZED;
    if (GetAncestor(Window, GA_ROOT) == Window) Flags |= ZP_WINDOW_FLAG_TOP_LEVEL;
    if (IsHungAppWindow(Window)) Flags |= ZP_WINDOW_FLAG_HUNG;
    if (FlagOn(Info->dwExStyle, WS_EX_TOPMOST)) Flags |= ZP_WINDOW_FLAG_TOPMOST;
    if (Window == GetDesktopWindow()) Flags |= ZP_WINDOW_FLAG_DESKTOP;
    return Flags;
}

static
LOGICAL
ZpWindow_FillRecord(
    _In_ HWND Window,
    _Out_ PZP_WINDOW_ENTRY Entry,
    _Out_opt_ PWINDOWINFO WindowInfo)
{
    WINDOWINFO LocalInfo = { sizeof(LocalInfo) };
    UNICODE_STRING ClassName;
    ULONG ProcessId, ThreadId, Length;

    ThreadId = GetWindowThreadProcessId(Window, &ProcessId);
    if (ProcessId == 0 || ThreadId == 0 || !GetWindowInfo(Window, &LocalInfo))
    {
        return FALSE;
    }
    Entry->Record.Handle = (ULONGLONG)(ULONG_PTR)Window;
    Entry->Record.ParentHandle = (ULONGLONG)(ULONG_PTR)GetAncestor(Window, GA_PARENT);
    Entry->Record.ProcessId = ProcessId;
    Entry->Record.ThreadId = ThreadId;
    Entry->Record.Style = LocalInfo.dwStyle;
    Entry->Record.ExStyle = LocalInfo.dwExStyle;
    Entry->Record.Flags = ZpWindow_GetFlags(Window, &LocalInfo);
    Length = UI_GetWindowTextExW(Window, Entry->Caption, ARRAYSIZE(Entry->Caption));
    Entry->Record.Caption = Length != 0 ? Entry->Caption : NULL;
    Entry->Record.CaptionLength = Length;
    RtlInitEmptyUnicodeString(&ClassName, Entry->ClassName, sizeof(Entry->ClassName));
    Length = GetClassNameW(Window, ClassName.Buffer, ClassName.MaximumLength / sizeof(WCHAR));
    Entry->Record.ClassName = Length != 0 ? Entry->ClassName : NULL;
    Entry->Record.ClassNameLength = Length;
    if (WindowInfo != NULL) *WindowInfo = LocalInfo;
    return TRUE;
}

static
NTSTATUS
ZpWindow_Enumerate(
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    HWND* Windows;
    PZP_WINDOW_ENTRY Entries;
    PZP_WINDOW_RECORD Records;
    PBYTE Buffer;
    HWND DesktopWindow;
    NTSTATUS Status;
    ULONG Capacity = 512, Count, EntryCount, Index, RecordCount = 0, Length;

    Windows = Mem_Alloc((SIZE_T)Capacity * sizeof(*Windows));
    if (Windows == NULL) return STATUS_NO_MEMORY;
    for (;;)
    {
        Status = NtUserBuildHwndList(NULL,
                                     GetDesktopWindow(),
                                     TRUE,
                                     FALSE,
                                     0,
                                     Capacity,
                                     Windows,
                                     &Count);
        if (Status != STATUS_BUFFER_TOO_SMALL) break;
        Mem_Free(Windows);
        Capacity = Count;
        Windows = Mem_Alloc((SIZE_T)Capacity * sizeof(*Windows));
        if (Windows == NULL) return STATUS_NO_MEMORY;
    }
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Windows);
        return Status;
    }
    if (Count == MAXULONG ||
        (SIZE_T)Count + 1 > MAXSIZE_T / (sizeof(*Entries) + sizeof(*Records)))
    {
        Mem_Free(Windows);
        return STATUS_INTEGER_OVERFLOW;
    }
    EntryCount = Count + 1;
    Entries = Mem_Alloc((SIZE_T)EntryCount * (sizeof(*Entries) + sizeof(*Records)));
    if (Entries == NULL)
    {
        Mem_Free(Windows);
        return STATUS_NO_MEMORY;
    }
    Records = (PZP_WINDOW_RECORD)(Entries + EntryCount);
    DesktopWindow = GetDesktopWindow();
    if (ZpWindow_FillRecord(DesktopWindow, &Entries[RecordCount], NULL)) RecordCount++;
    for (Index = 0; Index < Count; Index++)
    {
        if (ZpWindow_FillRecord(Windows[Index], &Entries[RecordCount], NULL)) RecordCount++;
    }
    Mem_Free(Windows);
    for (Index = 0; Index < RecordCount; Index++)
    {
        Records[Index] = Entries[Index].Record;
    }
    Status = ZpWindow_EncodeList(Records, RecordCount, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpWindow_EncodeList(Records, RecordCount, Buffer, Length, &Length);
    }
    Mem_Free(Entries);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return Status;
    }
    *Payload = Buffer;
    *PayloadLength = Length;
    return STATUS_SUCCESS;
}

static
ZP_STATUS
ZpWindow_ValidateIdentity(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _Out_ HWND* Window)
{
    HWND LocalWindow = (HWND)(ULONG_PTR)Handle;

    ULONG LocalProcessId;

    if (GetWindowThreadProcessId(LocalWindow, &LocalProcessId) != ThreadId ||
        LocalProcessId != ProcessId)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_FOUND);
    }
    *Window = LocalWindow;
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpWindow_Query(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    ZP_WINDOW_ENTRY Entry;
    ZP_WINDOW_INFO Info;
    WINDOWINFO WindowInfo;
    MONITORINFOEXW MonitorInfo = { sizeof(MonitorInfo) };
    HWND Window;
    HMONITOR Monitor;
    PBYTE Buffer;
    ULONG Length;
    NTSTATUS Status;
    ZP_STATUS Result;

    Result = ZpWindow_ValidateIdentity(Handle, ProcessId, ThreadId, &Window);
    if (!ZpStatus_IsSuccess(Result)) return Result;
    if (!ZpWindow_FillRecord(Window, &Entry, &WindowInfo))
    {
        return ZpWindow_StatusFromLastError();
    }
    Info.Record = Entry.Record;
    Info.OwnerHandle = (ULONGLONG)(ULONG_PTR)GetWindow(Window, GW_OWNER);
    Info.WindowLeft = WindowInfo.rcWindow.left;
    Info.WindowTop = WindowInfo.rcWindow.top;
    Info.WindowRight = WindowInfo.rcWindow.right;
    Info.WindowBottom = WindowInfo.rcWindow.bottom;
    Info.ClientLeft = WindowInfo.rcClient.left;
    Info.ClientTop = WindowInfo.rcClient.top;
    Info.ClientRight = WindowInfo.rcClient.right;
    Info.ClientBottom = WindowInfo.rcClient.bottom;
    Info.WindowStatus = WindowInfo.dwWindowStatus;
    Info.BorderWidth = WindowInfo.cxWindowBorders;
    Info.BorderHeight = WindowInfo.cyWindowBorders;
    Info.ClassAtom = WindowInfo.atomWindowType;
    Info.CreatorVersion = WindowInfo.wCreatorVersion;
    Info.PreviousHandle = (ULONGLONG)(ULONG_PTR)GetWindow(Window, GW_HWNDPREV);
    Info.NextHandle = (ULONGLONG)(ULONG_PTR)GetWindow(Window, GW_HWNDNEXT);
    Info.FirstChildHandle = (ULONGLONG)(ULONG_PTR)GetWindow(Window, GW_CHILD);
    Info.FirstSiblingHandle = (ULONGLONG)(ULONG_PTR)GetWindow(Window, GW_HWNDFIRST);
    Info.LastSiblingHandle = (ULONGLONG)(ULONG_PTR)GetWindow(Window, GW_HWNDLAST);
    Monitor = MonitorFromWindow(Window, MONITOR_DEFAULTTONULL);
    if (Monitor != NULL)
    {
        if (!GetMonitorInfoW(Monitor, (LPMONITORINFO)&MonitorInfo))
        {
            return ZpWindow_StatusFromLastError();
        }
        Info.MonitorLeft = MonitorInfo.rcMonitor.left;
        Info.MonitorTop = MonitorInfo.rcMonitor.top;
        Info.MonitorRight = MonitorInfo.rcMonitor.right;
        Info.MonitorBottom = MonitorInfo.rcMonitor.bottom;
        Info.MonitorDevice = MonitorInfo.szDevice;
        Info.MonitorDeviceLength = (ULONG)wcslen(MonitorInfo.szDevice);
    } else
    {
        Info.MonitorLeft = Info.MonitorTop = Info.MonitorRight = Info.MonitorBottom = 0;
        Info.MonitorDevice = NULL;
        Info.MonitorDeviceLength = 0;
    }
    Status = ZpWindow_EncodeInfo(&Info, NULL, 0, &Length);
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status)) Status = ZpWindow_EncodeInfo(&Info, Buffer, Length, &Length);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(Buffer);
        return ZpStatus_FromNtStatus(Status);
    }
    *Payload = Buffer;
    *PayloadLength = Length;
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpWindow_Control(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ZP_WINDOW_CONTROL Control)
{
    HWND Window;
    HWND RootWindow;
    FLASHWINFO FlashInfo;
    LONG Command;
    ZP_STATUS Status;

    Status = ZpWindow_ValidateIdentity(Handle, ProcessId, ThreadId, &Window);
    if (!ZpStatus_IsSuccess(Status)) return Status;
    switch (Control)
    {
        case ZpWindowControlShow: Command = SW_SHOW; break;
        case ZpWindowControlHide: Command = SW_HIDE; break;
        case ZpWindowControlMinimize: Command = SW_MINIMIZE; break;
        case ZpWindowControlMaximize: Command = SW_MAXIMIZE; break;
        case ZpWindowControlRestore: Command = SW_RESTORE; break;
        case ZpWindowControlForeground:
            return NtUserSwitchToThisWindow(Window, TRUE) ?
                       ZpStatus_Make(ZpStatusNone, 0) :
                       ZpWindow_StatusFromLastError();
        case ZpWindowControlClose:
            return PostMessageW(Window, WM_CLOSE, 0, 0) ?
                       ZpStatus_Make(ZpStatusNone, 0) :
                       ZpWindow_StatusFromLastError();
        case ZpWindowControlHighlight:
            RootWindow = NtUserGetAncestor(Window, GA_ROOT);
            if (RootWindow == NULL ||
                !NtUserSetWindowPos(RootWindow,
                                    HWND_TOP,
                                    0,
                                    0,
                                    0,
                                    0,
                                    SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE))
            {
                return ZpWindow_StatusFromLastError();
            }
            FlashInfo.cbSize = sizeof(FlashInfo);
            FlashInfo.hwnd = RootWindow;
            FlashInfo.dwFlags = FLASHW_ALL;
            FlashInfo.uCount = 3;
            FlashInfo.dwTimeout = 0;
            return NtUserFlashWindowEx(&FlashInfo) ?
                       ZpStatus_Make(ZpStatusNone, 0) :
                       ZpWindow_StatusFromLastError();
        case ZpWindowControlEnable:
            /* Returns the previous disabled state, not operation success. */
            NtUserEnableWindow(Window, TRUE);
            return ZpStatus_Make(ZpStatusNone, 0);
        case ZpWindowControlDisable:
            /* Returns the previous disabled state, not operation success. */
            NtUserEnableWindow(Window, FALSE);
            return ZpStatus_Make(ZpStatusNone, 0);
        case ZpWindowControlTopmost:
        case ZpWindowControlNotTopmost:
            return NtUserSetWindowPos(Window,
                                      Control == ZpWindowControlTopmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                                      0,
                                      0,
                                      0,
                                      0,
                                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) ?
                       ZpStatus_Make(ZpStatusNone, 0) :
                       ZpWindow_StatusFromLastError();
        default:
            return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    /* Returns the previous visibility state, not operation success. */
    NtUserShowWindowAsync(Window, Command);
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpWindow_Update(
    _In_ const ZP_WINDOW_UPDATE_VIEW* Update)
{
    WCHAR Caption[ZP_WINDOW_CAPTION_MAX_CCH + 1];
    HWND Window;
    W32ERROR Error;
    ZP_STATUS Status;

    Status = ZpWindow_ValidateIdentity(Update->Handle,
                                       Update->ProcessId,
                                       Update->ThreadId,
                                       &Window);
    if (!ZpStatus_IsSuccess(Status)) return Status;
    if (FlagOn(Update->Fields, ZP_WINDOW_UPDATE_CAPTION))
    {
        if (Update->Caption.Length != 0)
        {
            memcpy(Caption, Update->Caption.Buffer, (SIZE_T)Update->Caption.Length * sizeof(WCHAR));
        }
        Caption[Update->Caption.Length] = UNICODE_NULL;
        Error = UI_SendMessageTimeout(Window,
                                      WM_SETTEXT,
                                      0,
                                      (LPARAM)Caption,
                                      SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                                      1000,
                                      NULL);
        if (Error != ERROR_SUCCESS) return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (FlagOn(Update->Fields, ZP_WINDOW_UPDATE_STYLE))
    {
        Error = UI_SetWindowLong(Window, GWL_STYLE, Update->Style);
        if (Error != ERROR_SUCCESS) return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (FlagOn(Update->Fields, ZP_WINDOW_UPDATE_EXSTYLE))
    {
        Error = UI_SetWindowLong(Window, GWL_EXSTYLE, Update->ExStyle);
        if (Error != ERROR_SUCCESS) return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    if (FlagOn(Update->Fields, ZP_WINDOW_UPDATE_STYLE | ZP_WINDOW_UPDATE_EXSTYLE) &&
        !NtUserSetWindowPos(Window,
                            NULL,
                            0,
                            0,
                            0,
                            0,
                            SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE))
    {
        return ZpWindow_StatusFromLastError();
    }
    if (FlagOn(Update->Fields, ZP_WINDOW_UPDATE_RECT) &&
        !NtUserMoveWindow(Window,
                          Update->Left,
                          Update->Top,
                          Update->Right - Update->Left,
                          Update->Bottom - Update->Top,
                          TRUE))
    {
        return ZpWindow_StatusFromLastError();
    }
    return ZpStatus_Make(ZpStatusNone, 0);
}

static
ZP_STATUS
ZpWindow_Capture(
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    HWND Window;
    PZP_WINDOW_CAPTURE Capture = NULL;
    ZP_WINDOW_CAPTURE_IMAGE Image;
    HRESULT Result;
    ZP_STATUS Status;

    Status = ZpWindow_ValidateIdentity(Options->Handle,
                                       Options->ProcessId,
                                       Options->ThreadId,
                                       &Window);
    if (!ZpStatus_IsSuccess(Status)) return Status;
    Result = ZpWindowCapture_Create(Window, Options, &Capture);
    do
    {
        if (SUCCEEDED(Result)) Result = ZpWindowCapture_Next(Capture, 5000, &Image);
    } while (Result == S_FALSE);
    if (Result == S_OK)
    {
        *Payload = Image.Data;
        *PayloadLength = Image.Record.DataLength;
    }
    ZpWindowCapture_Close(Capture);
    return Result == S_OK ?
               ZpStatus_Make(ZpStatusNone, 0) :
               ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result);
}

static
NTSTATUS
ZpWindowCapture_SendLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PCZP_TRANSPORT_OPERATIONS Operations = Object->TransportOperations[Object->ActiveTransport];

    return Object->State == ZpClientStateReady && Operations->Send != NULL ?
               Operations->Send(Object->TransportContexts[Object->ActiveTransport],
                                MessageType,
                                Body,
                                BodyLength) :
               STATUS_CONNECTION_DISCONNECTED;
}

static
NTSTATUS
ZpWindowCapture_SendCloseLocked(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_ ZP_STATUS CloseStatus)
{
    BYTE Body[sizeof(ULONG) + ZP_STATUS_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->Header.ChannelId,
                                          CloseStatus,
                                          Body,
                                          sizeof(Body),
                                          &BodyLength);
    return NT_SUCCESS(Status) ?
               ZpWindowCapture_SendLocked(Channel->Header.Owner,
                                          ZpMessageChannelClose,
                                          Body,
                                          BodyLength) :
               Status;
}

static
NTSTATUS
ZpWindowCapture_SendBytes(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    PBYTE Body;
    ULONG Offset = 0, ChunkLength, BodyLength;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Pending, Removed;

    if (Channel->Options.DirectStreamId != 0)
    {
        return ZpRtc_Send(Object, Channel->Options.DirectStreamId, Data, Length);
    }
    Body = Mem_Alloc(sizeof(ULONG) + ZP_WINDOW_CAPTURE_CHUNK_SIZE);
    if (Body == NULL) return STATUS_NO_MEMORY;
    while (Offset < Length)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        if (Pending && Channel->Credit == 0)
        {
            NtClearEvent(Channel->CreditEvent);
            RtlReleaseSRWLockExclusive(&Object->Lock);
            Status = NtWaitForSingleObject(Channel->CreditEvent, FALSE, NULL);
            if (!NT_SUCCESS(Status)) break;
            continue;
        }
        if (!Pending)
        {
            RtlReleaseSRWLockExclusive(&Object->Lock);
            Status = STATUS_CANCELLED;
            break;
        }
        ChunkLength = min(Length - Offset,
                          (ULONG)min(Channel->Credit,
                                     ZP_WINDOW_CAPTURE_CHUNK_SIZE));
        Channel->Credit -= ChunkLength;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Status = ZpMessage_EncodeChannelData(Channel->Header.ChannelId,
                                             Add2Ptr(Data, Offset),
                                             ChunkLength,
                                             Body,
                                             sizeof(ULONG) + ZP_WINDOW_CAPTURE_CHUNK_SIZE,
                                             &BodyLength);
        if (!NT_SUCCESS(Status)) break;
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        Status = Pending ?
                     ZpWindowCapture_SendLocked(Object,
                                                ZpMessageChannelData,
                                                Body,
                                                BodyLength) :
                     STATUS_CANCELLED;
        Removed = !NT_SUCCESS(Status) && Pending ?
                      ZpClientLocalChannel_RemoveLocked(&Channel->Header) : FALSE;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
        if (!NT_SUCCESS(Status)) break;
        Offset += ChunkLength;
    }
    Mem_Free(Body);
    return Status;
}

static
VOID
ZpWindowCapture_FinishWorker(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    if (Removed) ZpWindowCapture_SendCloseLocked(Channel, Status);
    Channel->WorkerActive = FALSE;
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
NTSTATUS
NTAPI
ZpWindowCapture_Worker(
    _In_ PVOID Context)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel = Context;
    PZP_WINDOW_SHARED_CAPTURE Capture = NULL;
    ZP_WINDOW_CAPTURE_IMAGE Image;
    BYTE Header[sizeof(USHORT) + 8 * sizeof(ULONG)];
    HWND Window;
    ULONG HeaderLength;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    ZP_STATUS CompletionStatus;

    CompletionStatus = ZpWindow_ValidateIdentity(Channel->Options.Handle,
                                                  Channel->Options.ProcessId,
                                                  Channel->Options.ThreadId,
                                                  &Window);
    Result = ZpStatus_IsSuccess(CompletionStatus) ?
                 ZpWindowShared_Open(Window, &Channel->Options, &Capture) : E_HANDLE;
    while (SUCCEEDED(Result))
    {
        CompletionStatus = ZpWindow_ValidateIdentity(Channel->Options.Handle,
                                                      Channel->Options.ProcessId,
                                                      Channel->Options.ThreadId,
                                                      &Window);
        if (!ZpStatus_IsSuccess(CompletionStatus)) break;
        Result = ZpWindowShared_Next(Capture, 1000, &Image);
        if (Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT) || Result == S_FALSE)
        {
            Result = S_OK;
            continue;
        }
        if (FAILED(Result)) break;
        Status = ZpWindow_EncodeCaptureRecord(&Image.Record,
                                               Header,
                                               sizeof(Header),
                                               &HeaderLength);
        if (NT_SUCCESS(Status))
        {
            Status = ZpWindowCapture_SendBytes(Channel, Header, HeaderLength);
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpWindowCapture_SendBytes(Channel,
                                                Image.Data,
                                                Image.Record.DataLength);
        }
        ZpWindowCapture_FreeImage(&Image);
        if (!NT_SUCCESS(Status)) break;
    }
    ZpWindowShared_Close(Capture);
    if (!ZpStatus_IsSuccess(CompletionStatus))
    {
        ZpWindowCapture_FinishWorker(Channel, CompletionStatus);
    }
    else if (FAILED(Result))
    {
        ZpWindowCapture_FinishWorker(
            Channel,
            ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result));
    }
    else
    {
        ZpWindowCapture_FinishWorker(Channel,
                                     ZpStatus_FromNtStatus(Status));
    }
    return Status;
}

static
NTSTATUS
ZpWindowCapture_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel = (PZP_CLIENT_WINDOW_CAPTURE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || MAXULONGLONG - Channel->Credit < CreditBytes)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->Credit += CreditBytes;
    NtSetEvent(Channel->CreditEvent, NULL);
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpWindowCapture_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel = (PZP_CLIENT_WINDOW_CAPTURE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    UNREFERENCED_PARAMETER(Status);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (!Removed) return STATUS_PROTOCOL_UNREACHABLE;
    NtSetEvent(Channel->CreditEvent, NULL);
    ZpClientLocalChannel_Release(&Channel->Header);
    return STATUS_SUCCESS;
}

static
VOID
ZpWindowCapture_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel = (PZP_CLIENT_WINDOW_CAPTURE_CHANNEL)LocalChannel;

    UNREFERENCED_PARAMETER(Status);
    NtSetEvent(Channel->CreditEvent, NULL);
}

static
VOID
ZpWindowCapture_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel = (PZP_CLIENT_WINDOW_CAPTURE_CHANNEL)LocalChannel;

    if (Channel->WorkerThread != NULL) NtClose(Channel->WorkerThread);
    if (Channel->CreditEvent != NULL) NtClose(Channel->CreditEvent);
    Mem_Free(Channel);
}

static
NTSTATUS
ZpWindowCapture_CreateChannel(
    _Inout_ PZP_CLIENT_OBJECT Object,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL* Channel)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL CaptureChannel;
    NTSTATUS Status;

    CaptureChannel = Mem_Alloc(sizeof(*CaptureChannel));
    if (CaptureChannel == NULL) return STATUS_NO_MEMORY;
    RtlZeroMemory(CaptureChannel, sizeof(*CaptureChannel));
    CaptureChannel->Options = *Options;
    Status = NtCreateEvent(&CaptureChannel->CreditEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (NT_SUCCESS(Status))
    {
        Status = ZpClientLocalChannel_Insert(Object,
                                             &CaptureChannel->Header,
                                             ZP_WINDOW_MODULE_ID,
                                             NULL,
                                             ZpWindowCapture_ChannelWindow,
                                             ZpWindowCapture_ChannelClose,
                                             ZpWindowCapture_ChannelAbort,
                                             ZpWindowCapture_ChannelDestroy);
    }
    if (!NT_SUCCESS(Status))
    {
        if (CaptureChannel->CreditEvent != NULL) NtClose(CaptureChannel->CreditEvent);
        Mem_Free(CaptureChannel);
        return Status;
    }
    *Channel = CaptureChannel;
    return STATUS_SUCCESS;
}

ZP_STATUS
ZpWindow_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL* Channel)
{
    ULONGLONG Handle;
    ULONG ProcessId, ThreadId;
    ZP_WINDOW_CONTROL Control;
    ZP_WINDOW_UPDATE_VIEW Update;
    ZP_WINDOW_CAPTURE_OPTIONS CaptureOptions;
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL CaptureChannel;
    HWND Window;
    NTSTATUS Status;
    ZP_STATUS Result;
    HRESULT CaptureResult;

    *Channel = NULL;
    switch (OperationId)
    {
        case ZP_WINDOW_OPERATION_ENUMERATE:
            Status = RequestLength == 0 ?
                         ZpWindow_Enumerate(Response, ResponseLength) :
                         STATUS_INVALID_PARAMETER;
            return ZpStatus_FromNtStatus(Status);

        case ZP_WINDOW_OPERATION_QUERY:
            Status = ZpWindow_DecodeIdentity(Request,
                                             RequestLength,
                                             &Handle,
                                             &ProcessId,
                                             &ThreadId);
            return NT_SUCCESS(Status) ?
                       ZpWindow_Query(Handle, ProcessId, ThreadId, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(Status);

        case ZP_WINDOW_OPERATION_CONTROL:
            Status = ZpWindow_DecodeControl(Request,
                                            RequestLength,
                                            &Handle,
                                            &ProcessId,
                                            &ThreadId,
                                            &Control);
            if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
            Result = ZpWindow_Control(Handle, ProcessId, ThreadId, Control);
            if (ZpStatus_IsSuccess(Result))
            {
                *Response = NULL;
                *ResponseLength = 0;
            }
            return Result;

        case ZP_WINDOW_OPERATION_UPDATE:
            Status = ZpWindow_DecodeUpdate(Request, RequestLength, &Update);
            if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
            Result = ZpWindow_Update(&Update);
            if (ZpStatus_IsSuccess(Result))
            {
                *Response = NULL;
                *ResponseLength = 0;
            }
            return Result;

        case ZP_WINDOW_OPERATION_CAPTURE:
            Status = ZpWindow_DecodeCaptureRequest(Request,
                                                   RequestLength,
                                                   &CaptureOptions);
            return NT_SUCCESS(Status) ?
                       ZpWindow_Capture(&CaptureOptions, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(Status);

        case ZP_WINDOW_OPERATION_OPEN_CAPTURE:
            Status = ZpWindow_DecodeCaptureRequest(Request,
                                                   RequestLength,
                                                   &CaptureOptions);
            if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
            CaptureResult = ZpWindowCapture_CheckSupport();
            if (FAILED(CaptureResult)) return ZpStatus_FromCode(ZpStatusHResult, (ULONG)CaptureResult);
            Result = ZpWindow_ValidateIdentity(CaptureOptions.Handle,
                                               CaptureOptions.ProcessId,
                                               CaptureOptions.ThreadId,
                                               &Window);
            if (!ZpStatus_IsSuccess(Result)) return Result;
            Status = ZpWindowCapture_CreateChannel(Client,
                                                    &CaptureOptions,
                                                    &CaptureChannel);
            if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
            *ResponseLength = sizeof(ULONGLONG);
            *Response = Mem_Alloc(*ResponseLength);
            Status = *Response == NULL ? STATUS_NO_MEMORY :
                         ZpWindow_EncodeCaptureChannel(CaptureChannel->Header.ChannelId,
                                                       *Response,
                                                       *ResponseLength,
                                                       ResponseLength);
            if (!NT_SUCCESS(Status))
            {
                Mem_Free(*Response);
                *Response = NULL;
                ZpWindow_CommitCaptureChannel(CaptureChannel, FALSE);
                return ZpStatus_FromNtStatus(Status);
            }
            *Channel = CaptureChannel;
            return ZpStatus_Make(ZpStatusNone, 0);

        default:
            return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
}

VOID
ZpWindow_CommitCaptureChannel(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ResponseSent)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    }
    else
    {
        Channel->WorkerActive = TRUE;
        ZpClientLocalChannel_AddRef(&Channel->Header);
        Object->CallbackCount++;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed)
    {
        ZpClientLocalChannel_Release(&Channel->Header);
        return;
    }
    if (!ResponseSent) return;
    Status = PS_CreateThread(NtCurrentProcess(),
                             TRUE,
                             ZpWindowCapture_Worker,
                             Channel,
                             &Channel->WorkerThread,
                             NULL);
    if (NT_SUCCESS(Status)) Status = NtResumeThread(Channel->WorkerThread, NULL);
    if (!NT_SUCCESS(Status))
    {
        if (Channel->WorkerThread != NULL) NtTerminateThread(Channel->WorkerThread, Status);
        ZpWindowCapture_FinishWorker(Channel,
                                     ZpStatus_FromNtStatus(Status));
    }
}
