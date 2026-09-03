#include "Client.h"

#include "Capture.h"
#include "Encoder.h"
#include "Shared.h"
#include "../Rtc/Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <KNSoft/NDK/NT/Win32K/Win32KApi.h>

#pragma comment(lib, "KNSoft.NDK.Win32u.lib")

#define ZP_WINDOW_CLASS_CCH 256
#define ZP_WINDOW_CAPTURE_CHUNK_SIZE 0x00040000UL
#define ZP_WINDOW_PRESSED_KEY_COUNT 512

typedef struct _ZP_WINDOW_ENTRY
{
    ZP_WINDOW_RECORD Record;
    WCHAR Caption[ZP_WINDOW_CAPTION_MAX_CCH];
    WCHAR ClassName[ZP_WINDOW_CLASS_CCH];
} ZP_WINDOW_ENTRY, *PZP_WINDOW_ENTRY;

struct _ZP_CLIENT_WINDOW_CAPTURE_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    BOOLEAN WorkerActive;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    HANDLE CreditEvent;
    HANDLE RenderEvent;
    HANDLE WorkerThread;
    PZP_WINDOW_SHARED_CAPTURE Capture;
    ULONG PendingFrameSequence;
    ULONG AcknowledgedFrameSequence;
    ULONG FrameSequence;
    BYTE FrameAckFlags;
    BYTE VideoCodecs;
    ULONG VideoCodecWidth;
    ULONG VideoCodecHeight;
    BOOLEAN VideoCodecsChanged;
    BYTE PressedKeys[ZP_WINDOW_PRESSED_KEY_COUNT / 8];
    BYTE PressedMouseButtons;
    ZP_WINDOW_CAPTURE_OPTIONS Options;
    RECT MonitorRect;
    RECT VirtualRect;
};

typedef struct _ZP_WINDOW_VIDEO_STATE
{
    PZP_WINDOW_VIDEO_ENCODER Encoder;
    ULONG Width;
    ULONG Height;
    ULONG BitRate;
    ULONG HighChangeFrames;
    ULONG LowChangeFrames;
    BYTE FrameRate;
    BYTE Codec;
    BYTE RequestedCodec;
    BOOLEAN Active;
    BOOLEAN Unavailable;
    BOOLEAN ForceKeyFrame;
} ZP_WINDOW_VIDEO_STATE, *PZP_WINDOW_VIDEO_STATE;

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
_Success_(return != FALSE)
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

typedef struct _ZP_WINDOW_MONITOR_ENUMERATION
{
    PZP_WINDOW_MONITOR Monitors;
    PWCHAR Devices;
    ULONG Capacity;
    ULONG Count;
    NTSTATUS Status;
} ZP_WINDOW_MONITOR_ENUMERATION, *PZP_WINDOW_MONITOR_ENUMERATION;

static
BOOL
CALLBACK
ZpWindow_EnumerateMonitorCallback(
    _In_ HMONITOR Monitor,
    _In_ HDC DeviceContext,
    _In_ PRECT MonitorRect,
    _In_ LPARAM Parameter)
{
    PZP_WINDOW_MONITOR_ENUMERATION Enumeration = (PVOID)Parameter;
    MONITORINFOEXW Info = { sizeof(Info) };
    PZP_WINDOW_MONITOR Record;

    UNREFERENCED_PARAMETER(DeviceContext);
    UNREFERENCED_PARAMETER(MonitorRect);
    if (Enumeration->Count == Enumeration->Capacity)
    {
        Enumeration->Status = STATUS_BUFFER_TOO_SMALL;
        return FALSE;
    }
    if (!GetMonitorInfoW(Monitor, (LPMONITORINFO)&Info))
    {
        Enumeration->Status = NTSTATUS_FROM_WIN32(GetLastError());
        return FALSE;
    }
    Record = &Enumeration->Monitors[Enumeration->Count];
    Record->Index = Enumeration->Count;
    Record->Flags = FlagOn(Info.dwFlags, MONITORINFOF_PRIMARY) ?
                        ZP_WINDOW_MONITOR_FLAG_PRIMARY :
                        0;
    Record->Left = Info.rcMonitor.left;
    Record->Top = Info.rcMonitor.top;
    Record->Right = Info.rcMonitor.right;
    Record->Bottom = Info.rcMonitor.bottom;
    Record->WorkLeft = Info.rcWork.left;
    Record->WorkTop = Info.rcWork.top;
    Record->WorkRight = Info.rcWork.right;
    Record->WorkBottom = Info.rcWork.bottom;
    Record->Device = Enumeration->Devices + (SIZE_T)Enumeration->Count * CCHDEVICENAME;
    RtlCopyMemory((PVOID)Record->Device, Info.szDevice, sizeof(Info.szDevice));
    Record->DeviceLength = (ULONG)wcslen(Record->Device);
    Enumeration->Count++;
    return TRUE;
}

static
NTSTATUS
ZpWindow_EnumerateMonitors(
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    ZP_WINDOW_MONITOR_ENUMERATION Enumeration;
    SIZE_T AllocationSize;
    PBYTE Buffer;
    ULONG Length;
    NTSTATUS Status;

    Enumeration.Capacity = (ULONG)GetSystemMetrics(SM_CMONITORS);
    if (Enumeration.Capacity == 0) return STATUS_NOT_FOUND;
    if (Enumeration.Capacity > ZP_CODEC_MAX_ELEMENT_COUNT ||
        (SIZE_T)Enumeration.Capacity > MAXSIZE_T /
            (sizeof(*Enumeration.Monitors) + CCHDEVICENAME * sizeof(WCHAR)))
    {
        return STATUS_INTEGER_OVERFLOW;
    }
    AllocationSize = (SIZE_T)Enumeration.Capacity *
                     (sizeof(*Enumeration.Monitors) + CCHDEVICENAME * sizeof(WCHAR));
    Enumeration.Monitors = Mem_Alloc(AllocationSize);
    if (Enumeration.Monitors == NULL) return STATUS_NO_MEMORY;
    Enumeration.Devices = (PWCHAR)(Enumeration.Monitors + Enumeration.Capacity);
    Enumeration.Count = 0;
    Enumeration.Status = STATUS_SUCCESS;
    if (!EnumDisplayMonitors(NULL, NULL, ZpWindow_EnumerateMonitorCallback, (LPARAM)&Enumeration) &&
        NT_SUCCESS(Enumeration.Status))
    {
        Enumeration.Status = NTSTATUS_FROM_WIN32(GetLastError());
    }
    Status = Enumeration.Status;
    if (NT_SUCCESS(Status))
    {
        Status = ZpWindow_EncodeMonitorList(Enumeration.Monitors,
                                           Enumeration.Count,
                                           NULL,
                                           0,
                                           &Length);
    }
    Buffer = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
    if (NT_SUCCESS(Status) && Buffer == NULL) Status = STATUS_NO_MEMORY;
    if (NT_SUCCESS(Status))
    {
        Status = ZpWindow_EncodeMonitorList(Enumeration.Monitors,
                                           Enumeration.Count,
                                           Buffer,
                                           Length,
                                           &Length);
    }
    Mem_Free(Enumeration.Monitors);
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
ZpWindow_ValidateCaptureIdentity(
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_ HWND* Window)
{
    if (FlagOn(Options->Flags, ZP_WINDOW_CAPTURE_DESKTOP))
    {
        if (Options->Handle != 0 || Options->ProcessId != 0 || Options->ThreadId != 0)
        {
            return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
        }
        *Window = NULL;
        return ZpStatus_Make(ZpStatusNone, 0);
    }
    return ZpWindow_ValidateIdentity(Options->Handle,
                                     Options->ProcessId,
                                     Options->ThreadId,
                                     Window);
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

    Status = ZpWindow_ValidateCaptureIdentity(Options, &Window);
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
    _In_ ULONG BodyLength,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength)
{
    return ZpClient_SendLocked(Object,
                               ZP_SEND_FLAG_INTERACTIVE,
                               MessageType,
                               Body,
                               BodyLength,
                               Payload,
                               PayloadLength);
}

static
NTSTATUS
ZpWindowCapture_SendWindowLocked(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
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
        Status = ZpWindowCapture_SendLocked(Channel->Header.Owner,
                                            ZpMessageChannelWindow,
                                            Body,
                                            BodyLength,
                                            NULL,
                                            0);
        if (!NT_SUCCESS(Status)) Channel->ReceiveCredit -= CreditBytes;
    }
    return Status;
}

static
NTSTATUS
ZpWindowCapture_SendCloseLocked(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_ ZP_STATUS CloseStatus)
{
    BYTE Body[sizeof(ULONG) + ZP_STATUS_MAX_WIRE_SIZE];
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
                                          BodyLength,
                                          NULL,
                                          0) :
               Status;
}

static
NTSTATUS
ZpWindowCapture_CompleteInput(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_ ULONG Length,
    _In_ NTSTATUS Status)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;

    if (!NT_SUCCESS(Status)) return Status;
    RtlAcquireSRWLockExclusive(&Object->Lock);
    Status = Channel->Header.Pending ?
                 ZpWindowCapture_SendWindowLocked(Channel, Length) : STATUS_CANCELLED;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return Status;
}

static
NTSTATUS
ZpWindowCapture_SendBytes(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_reads_bytes_(Length) const VOID* Data,
    _In_ ULONG Length)
{
    BYTE Header[sizeof(ULONG)];
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    ULONG Offset = 0, ChunkLength;
    NTSTATUS Status = STATUS_SUCCESS;
    LOGICAL Pending, Removed;

    if (Channel->Options.DirectStreamId != 0)
    {
        return ZpRtc_Send(Object, Channel->Options.DirectStreamId, Data, Length);
    }
    Status = ZpMessage_EncodeChannelDataHeader(Channel->Header.ChannelId, Header);
    if (!NT_SUCCESS(Status)) return Status;
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
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Pending = Channel->Header.Pending;
        Status = Pending ?
                     ZpWindowCapture_SendLocked(Object,
                                                ZpMessageChannelData,
                                                Header,
                                                sizeof(Header),
                                                Add2Ptr(Data, Offset),
                                                ChunkLength) :
                     STATUS_CANCELLED;
        Removed = !NT_SUCCESS(Status) && Pending ?
                      ZpClientLocalChannel_RemoveLocked(&Channel->Header) : FALSE;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
        if (!NT_SUCCESS(Status)) break;
        Offset += ChunkLength;
    }
    return Status;
}

static
NTSTATUS
ZpWindowCapture_BeginFrame(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_ ULONG Sequence)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    NTSTATUS Status;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending)
    {
        Status = STATUS_CANCELLED;
    }
    else if (Channel->PendingFrameSequence != 0)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    else
    {
        NtClearEvent(Channel->RenderEvent);
        Channel->PendingFrameSequence = Sequence;
        Channel->AcknowledgedFrameSequence = 0;
        Channel->FrameAckFlags = 0;
        Status = STATUS_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return Status;
}

static
NTSTATUS
ZpWindowCapture_WaitForFrame(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _Out_ PBYTE Flags)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    NTSTATUS Status;

    for (;;)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (!Channel->Header.Pending)
        {
            Status = STATUS_CANCELLED;
        }
        else if (Channel->AcknowledgedFrameSequence == Channel->PendingFrameSequence)
        {
            *Flags = Channel->FrameAckFlags;
            Channel->PendingFrameSequence = 0;
            Status = STATUS_SUCCESS;
        }
        else
        {
            NtClearEvent(Channel->RenderEvent);
            Status = STATUS_PENDING;
        }
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (Status != STATUS_PENDING) return Status;
        Status = NtWaitForSingleObject(Channel->RenderEvent, FALSE, NULL);
        if (!NT_SUCCESS(Status)) return Status;
    }
}

static
VOID
ZpWindowCapture_ReleaseInput(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel);

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
    ZpWindowCapture_ReleaseInput(Channel);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
VOID
ZpWindowCapture_GetOptions(
    _In_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _Out_ PZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_ PZP_CONNECTION_POLICY ConnectionPolicy,
    _Out_ PBYTE VideoCodecs,
    _Out_ PULONG VideoCodecWidth,
    _Out_ PULONG VideoCodecHeight,
    _Out_ PBOOLEAN VideoCodecsChanged)
{
    RtlAcquireSRWLockExclusive(&Channel->Header.Owner->Lock);
    *Options = Channel->Options;
    *ConnectionPolicy = Channel->Header.Owner->ConnectionPolicy;
    *VideoCodecs = Channel->VideoCodecs;
    *VideoCodecWidth = Channel->VideoCodecWidth;
    *VideoCodecHeight = Channel->VideoCodecHeight;
    *VideoCodecsChanged = Channel->VideoCodecsChanged;
    Channel->VideoCodecsChanged = FALSE;
    RtlReleaseSRWLockExclusive(&Channel->Header.Owner->Lock);
}

static
BOOLEAN
ZpWindowCapture_IsVideoCodecSupported(
    _In_ BYTE VideoCodecs,
    _In_ ZP_WINDOW_VIDEO_CODEC Codec)
{
    return FlagOn(VideoCodecs, 1U << Codec);
}

static
ULONG
ZpWindowCapture_GetVideoBitRate(
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _In_ PCZP_CONNECTION_POLICY ConnectionPolicy,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ZP_WINDOW_VIDEO_CODEC Codec)
{
    static const ULONG Limits[] = { 1500000, 5000000, 16000000, 50000000, 100000000 };
    BYTE QualityClass = min(ConnectionPolicy->SpeedClass, ConnectionPolicy->LatencyClass);
    ULONGLONG BitRate = (ULONGLONG)Width * Height * Options->FrameRate *
                        (Options->Quality + 40) / 1000;

    if (Codec == ZpWindowVideoCodecH265) BitRate = BitRate * 7 / 10;
    return (ULONG)max(256000, min(BitRate, Limits[QualityClass]));
}

static
VOID
ZpWindowCapture_UpdateAutomaticMode(
    _Inout_ PZP_WINDOW_VIDEO_STATE State,
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _In_ PCZP_CONNECTION_POLICY ConnectionPolicy,
    _In_ BYTE VideoCodecs,
    _In_ BYTE ChangeRate)
{
    static const BYTE EnterThresholds[] = { 12, 18, 25, 32, 40 };
    BYTE QualityClass = min(ConnectionPolicy->SpeedClass, ConnectionPolicy->LatencyClass);
    ULONG EnterFrames = max(3, Options->FrameRate / 2);
    ULONG ExitFrames = max(6, Options->FrameRate * 3);

    if (!State->Active)
    {
        State->HighChangeFrames = ChangeRate >= EnterThresholds[QualityClass] ?
                                      State->HighChangeFrames + 1 : 0;
        if (State->HighChangeFrames >= EnterFrames && !State->Unavailable && VideoCodecs != 0)
        {
            State->Active = TRUE;
            State->HighChangeFrames = 0;
            State->LowChangeFrames = 0;
        }
        return;
    }
    State->LowChangeFrames = ChangeRate <= EnterThresholds[QualityClass] / 3 ?
                                 State->LowChangeFrames + 1 : 0;
    if (State->LowChangeFrames >= ExitFrames)
    {
        State->Active = FALSE;
        State->HighChangeFrames = 0;
        State->LowChangeFrames = 0;
    }
}

static
NTSTATUS
ZpWindowCapture_SendFrame(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _Inout_ PZP_WINDOW_CAPTURE_RECORD Record,
    _In_reads_bytes_(Record->DataLength) const VOID* Data,
    _Out_ PBYTE AckFlags)
{
    BYTE Header[ZP_WINDOW_CAPTURE_RECORD_WIRE_SIZE];
    ULONG HeaderLength;
    NTSTATUS Status;

    if (++Channel->FrameSequence == 0) Channel->FrameSequence++;
    Record->Sequence = Channel->FrameSequence;
    Status = ZpWindowCapture_BeginFrame(Channel, Record->Sequence);
    if (NT_SUCCESS(Status))
    {
        Status = ZpWindow_EncodeCaptureRecord(Record, Header, sizeof(Header), &HeaderLength);
    }
    if (NT_SUCCESS(Status)) Status = ZpWindowCapture_SendBytes(Channel, Header, HeaderLength);
    if (NT_SUCCESS(Status)) Status = ZpWindowCapture_SendBytes(Channel, Data, Record->DataLength);
    if (NT_SUCCESS(Status)) Status = ZpWindowCapture_WaitForFrame(Channel, AckFlags);
    return Status;
}

static
_Function_class_(USER_THREAD_START_ROUTINE)
NTSTATUS
NTAPI
ZpWindowCapture_Worker(
    _In_ PVOID Context)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel = Context;
    PZP_WINDOW_SHARED_CAPTURE Capture = NULL;
    ZP_WINDOW_VIDEO_STATE Video = { 0 };
    ZP_WINDOW_CAPTURE_OPTIONS Options;
    ZP_CONNECTION_POLICY ConnectionPolicy;
    ZP_WINDOW_CAPTURE_RECORD Record;
    BYTE AckFlags, ChangeRate, VideoCodecs;
    HWND Window;
    ULONG Width, Height, BitRate, VideoCodecWidth, VideoCodecHeight;
    LOGICAL WasVideo;
    BOOLEAN VideoCodecsChanged, VideoCodecsMatch;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    ZP_STATUS CompletionStatus;

    CompletionStatus = ZpWindow_ValidateCaptureIdentity(&Channel->Options, &Window);
    Result = ZpStatus_IsSuccess(CompletionStatus) ?
                 ZpWindowShared_Open(Window, &Channel->Options, &Capture) : E_HANDLE;
    if (SUCCEEDED(Result))
    {
        RtlAcquireSRWLockExclusive(&Channel->Header.Owner->Lock);
        Channel->Capture = Capture;
        RtlReleaseSRWLockExclusive(&Channel->Header.Owner->Lock);
    }
    while (SUCCEEDED(Result))
    {
        ZpWindowCapture_GetOptions(Channel,
                                   &Options,
                                   &ConnectionPolicy,
                                   &VideoCodecs,
                                   &VideoCodecWidth,
                                   &VideoCodecHeight,
                                   &VideoCodecsChanged);
        CompletionStatus = ZpWindow_ValidateCaptureIdentity(&Options, &Window);
        if (!ZpStatus_IsSuccess(CompletionStatus)) break;
        ZpWindowShared_GetFormat(Capture, &Width, &Height);
        VideoCodecsMatch = VideoCodecWidth == Width && VideoCodecHeight == Height;
        if (Video.RequestedCodec != Options.Encoding.Codec)
        {
            Video.Unavailable = FALSE;
            Video.RequestedCodec = Options.Encoding.Codec;
            if (Video.Encoder != NULL)
            {
                ZpWindowVideoEncoder_Close(Video.Encoder);
                Video.Encoder = NULL;
            }
        }
        if (VideoCodecsChanged && VideoCodecsMatch) Video.Unavailable = VideoCodecs == 0;
        if (Options.Encoding.Mode == ZpWindowCaptureModeImage) Video.Active = FALSE;
        else if (Options.Encoding.Mode == ZpWindowCaptureModeVideo)
        {
            if (VideoCodecsMatch &&
                !ZpWindowCapture_IsVideoCodecSupported(VideoCodecs, Options.Encoding.Codec))
            {
                CompletionStatus = ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
                break;
            }
            Video.Active = TRUE;
        }
        else if (!VideoCodecsMatch || VideoCodecs == 0) Video.Active = FALSE;
        if (!Video.Active && Video.Encoder != NULL)
        {
            ZpWindowVideoEncoder_Close(Video.Encoder);
            Video.Encoder = NULL;
            ZpWindowShared_RequestKeyFrame(Capture);
        }
        else if (Video.Encoder != NULL && VideoCodecsMatch &&
                 !ZpWindowCapture_IsVideoCodecSupported(VideoCodecs, Video.Codec))
        {
            ZpWindowVideoEncoder_Close(Video.Encoder);
            Video.Encoder = NULL;
        }
        if (Video.Active)
        {
            if (Video.Encoder != NULL)
            {
                BitRate = ZpWindowCapture_GetVideoBitRate(&Options,
                                                           &ConnectionPolicy,
                                                           Width,
                                                           Height,
                                                           Video.Codec);
                if (Video.Width != Width || Video.Height != Height ||
                    Video.BitRate != BitRate || Video.FrameRate != Options.FrameRate)
                {
                    ZpWindowVideoEncoder_Close(Video.Encoder);
                    Video.Encoder = NULL;
                }
            }
        }
        if (Video.Active && Video.Encoder == NULL)
        {
            ZP_WINDOW_VIDEO_CODEC AlternativeCodec;

            Video.Codec = Options.Encoding.Codec;
            if (Options.Encoding.Mode == ZpWindowCaptureModeAuto &&
                !ZpWindowCapture_IsVideoCodecSupported(VideoCodecs, Video.Codec))
            {
                Video.Codec ^= 1;
            }
            BitRate = ZpWindowCapture_GetVideoBitRate(&Options,
                                                       &ConnectionPolicy,
                                                       Width,
                                                       Height,
                                                       Video.Codec);
            Result = ZpWindowVideoEncoder_Create(Video.Codec,
                                                 Width,
                                                 Height,
                                                 Options.FrameRate,
                                                 BitRate,
                                                 ZpWindowShared_GetDeviceManager(Capture),
                                                 &Video.Encoder);
            AlternativeCodec = Video.Codec ^ 1;
            if (FAILED(Result) && Options.Encoding.Mode == ZpWindowCaptureModeAuto &&
                ZpWindowCapture_IsVideoCodecSupported(VideoCodecs, AlternativeCodec))
            {
                Video.Codec = AlternativeCodec;
                BitRate = ZpWindowCapture_GetVideoBitRate(&Options,
                                                           &ConnectionPolicy,
                                                           Width,
                                                           Height,
                                                           Video.Codec);
                Result = ZpWindowVideoEncoder_Create(Video.Codec,
                                                     Width,
                                                     Height,
                                                     Options.FrameRate,
                                                     BitRate,
                                                     ZpWindowShared_GetDeviceManager(Capture),
                                                     &Video.Encoder);
            }
            if (FAILED(Result) && Options.Encoding.Mode == ZpWindowCaptureModeAuto)
            {
                Video.Active = FALSE;
                Video.Unavailable = TRUE;
                Result = S_OK;
            }
            if (FAILED(Result)) break;
            if (Video.Encoder != NULL)
            {
                Video.Width = Width;
                Video.Height = Height;
                Video.BitRate = BitRate;
                Video.FrameRate = Options.FrameRate;
                Video.ForceKeyFrame = TRUE;
            }
        }
        WasVideo = Video.Active;
        if (WasVideo)
        {
            ZP_WINDOW_VIDEO_FRAME VideoFrame;
            IMFSample* Sample;
            ULONGLONG Timestamp;

            Result = ZpWindowShared_NextSample(Capture,
                                               1000,
                                               &Sample,
                                               &Timestamp,
                                               &ChangeRate);
            if (Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT) || Result == S_FALSE)
            {
                Result = S_OK;
                continue;
            }
            if (FAILED(Result)) break;
            Result = ZpWindowVideoEncoder_Encode(Video.Encoder,
                                                 Sample,
                                                 Timestamp,
                                                 Video.ForceKeyFrame,
                                                 &VideoFrame);
            Sample->lpVtbl->Release(Sample);
            Video.ForceKeyFrame = FALSE;
            if (Result == S_FALSE) continue;
            if (FAILED(Result) && Options.Encoding.Mode == ZpWindowCaptureModeAuto)
            {
                ZpWindowVideoEncoder_Close(Video.Encoder);
                Video.Encoder = NULL;
                Video.Active = FALSE;
                Video.Unavailable = TRUE;
                ZpWindowShared_RequestKeyFrame(Capture);
                Result = S_OK;
                continue;
            }
            if (FAILED(Result)) break;
            Record.Type = Video.Codec == ZpWindowVideoCodecH264 ?
                              (VideoFrame.KeyFrame ? ZpWindowCaptureRecordH264KeyFrame :
                                                     ZpWindowCaptureRecordH264Delta) :
                              (VideoFrame.KeyFrame ? ZpWindowCaptureRecordH265KeyFrame :
                                                     ZpWindowCaptureRecordH265Delta);
            Record.CanvasWidth = Record.Width = Video.Width;
            Record.CanvasHeight = Record.Height = Video.Height;
            Record.Left = Record.Top = 0;
            Record.DataLength = VideoFrame.Length;
            Status = ZpWindowCapture_SendFrame(Channel, &Record, VideoFrame.Data, &AckFlags);
            ZpWindowVideoEncoder_FreeFrame(&VideoFrame);
        }
        else
        {
            PZP_WINDOW_SHARED_IMAGE SharedImage;
            PZP_WINDOW_CAPTURE_IMAGE Image;

            Result = ZpWindowShared_Next(Capture, 1000, &SharedImage);
            if (Result == HRESULT_FROM_WIN32(ERROR_TIMEOUT) || Result == S_FALSE)
            {
                Result = S_OK;
                continue;
            }
            if (FAILED(Result)) break;
            Image = &SharedImage->Value;
            Record = Image->Record;
            ChangeRate = Record.Type == ZpWindowCaptureRecordKeyFrame ? 100 :
                             (BYTE)min(100,
                                       (ULONGLONG)Record.Width * Record.Height * 100 /
                                           ((ULONGLONG)Record.CanvasWidth * Record.CanvasHeight));
            Status = ZpWindowCapture_SendFrame(Channel, &Record, Image->Data, &AckFlags);
            ZpWindowShared_ReleaseImage(SharedImage);
        }
        if (NT_SUCCESS(Status) && FlagOn(AckFlags, ZP_WINDOW_FRAME_ACK_KEYFRAME))
        {
            if (WasVideo)
            {
                ZpWindowVideoEncoder_Close(Video.Encoder);
                Video.Encoder = NULL;
            }
            ZpWindowShared_RequestKeyFrame(Capture);
        }
        if (!NT_SUCCESS(Status)) break;
        if (Options.Encoding.Mode == ZpWindowCaptureModeAuto)
        {
            ZpWindowCapture_UpdateAutomaticMode(&Video,
                                                 &Options,
                                                 &ConnectionPolicy,
                                                 VideoCodecsMatch ? VideoCodecs : 0,
                                                 ChangeRate);
            if (WasVideo && !Video.Active) ZpWindowShared_RequestKeyFrame(Capture);
        }
    }
    RtlAcquireSRWLockExclusive(&Channel->Header.Owner->Lock);
    Channel->Capture = NULL;
    RtlReleaseSRWLockExclusive(&Channel->Header.Owner->Lock);
    ZpWindowVideoEncoder_Close(Video.Encoder);
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
USHORT
ZpWindowCapture_ReadUInt16(
    _In_reads_bytes_(sizeof(USHORT)) const BYTE* Data)
{
    USHORT Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static
ULONG
ZpWindowCapture_ReadUInt32(
    _In_reads_bytes_(sizeof(ULONG)) const BYTE* Data)
{
    ULONG Value;

    RtlCopyMemory(&Value, Data, sizeof(Value));
    return Value;
}

static
VOID
ZpWindowCapture_TrackKey(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_ USHORT ScanCode,
    _In_ USHORT Flags)
{
    ULONG Index = ScanCode * 2 + (FlagOn(Flags, ZP_WINDOW_KEY_EXTENDED) ? 1 : 0);
    BYTE Mask = (BYTE)(1U << (Index & 7));

    if (FlagOn(Flags, ZP_WINDOW_KEY_UP)) Channel->PressedKeys[Index / 8] &= ~Mask;
    else Channel->PressedKeys[Index / 8] |= Mask;
}

static
VOID
ZpWindowCapture_TrackMouse(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _In_ USHORT Flags)
{
    if (FlagOn(Flags, ZP_WINDOW_MOUSE_LEFT_DOWN)) Channel->PressedMouseButtons |= 1;
    if (FlagOn(Flags, ZP_WINDOW_MOUSE_LEFT_UP)) Channel->PressedMouseButtons &= ~1;
    if (FlagOn(Flags, ZP_WINDOW_MOUSE_RIGHT_DOWN)) Channel->PressedMouseButtons |= 2;
    if (FlagOn(Flags, ZP_WINDOW_MOUSE_RIGHT_UP)) Channel->PressedMouseButtons &= ~2;
    if (FlagOn(Flags, ZP_WINDOW_MOUSE_MIDDLE_DOWN)) Channel->PressedMouseButtons |= 4;
    if (FlagOn(Flags, ZP_WINDOW_MOUSE_MIDDLE_UP)) Channel->PressedMouseButtons &= ~4;
}

static
VOID
ZpWindowCapture_ReleaseInput(
    _Inout_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    INPUT Inputs[32] = { 0 };
    BYTE PressedKeys[sizeof(Channel->PressedKeys)], PressedMouseButtons;
    ULONG Count = 0, Index;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    RtlCopyMemory(PressedKeys, Channel->PressedKeys, sizeof(PressedKeys));
    RtlZeroMemory(Channel->PressedKeys, sizeof(Channel->PressedKeys));
    PressedMouseButtons = Channel->PressedMouseButtons;
    Channel->PressedMouseButtons = 0;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    for (Index = 0; Index < ZP_WINDOW_PRESSED_KEY_COUNT; Index++)
    {
        if (!FlagOn(PressedKeys[Index / 8], 1U << (Index & 7))) continue;
        Inputs[Count].type = INPUT_KEYBOARD;
        Inputs[Count].ki.wScan = (WORD)(Index / 2);
        Inputs[Count].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP |
                                  ((Index & 1) != 0 ? KEYEVENTF_EXTENDEDKEY : 0);
        if (++Count == ARRAYSIZE(Inputs))
        {
            NtUserSendInput(Count, Inputs, sizeof(INPUT));
            Count = 0;
        }
    }
    if (Count != 0) NtUserSendInput(Count, Inputs, sizeof(INPUT));
    if (PressedMouseButtons != 0)
    {
        INPUT Input = { 0 };

        Input.type = INPUT_MOUSE;
        Input.mi.dwFlags = (FlagOn(PressedMouseButtons, 1) ? MOUSEEVENTF_LEFTUP : 0) |
                           (FlagOn(PressedMouseButtons, 2) ? MOUSEEVENTF_RIGHTUP : 0) |
                           (FlagOn(PressedMouseButtons, 4) ? MOUSEEVENTF_MIDDLEUP : 0);
        NtUserSendInput(1, &Input, sizeof(Input));
    }
}

static
NTSTATUS
ZpWindowCapture_SetClipboard(
    _In_reads_bytes_(Length) const BYTE* Data,
    _In_ ULONG Length)
{
    HGLOBAL Memory;
    PWCHAR Text;

    if (Length > 0x00100000 || (Length & 1) != 0) return STATUS_INVALID_PARAMETER;
    Memory = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)Length + sizeof(WCHAR));
    if (Memory == NULL) return STATUS_SUCCESS;
    Text = GlobalLock(Memory);
    if (Text != NULL)
    {
        RtlCopyMemory(Text, Data, Length);
        Text[Length / sizeof(WCHAR)] = UNICODE_NULL;
        GlobalUnlock(Memory);
    }
    if (Text != NULL && OpenClipboard(NULL))
    {
        if (EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, Memory) != NULL) Memory = NULL;
        CloseClipboard();
    }
    if (Memory != NULL) GlobalFree(Memory);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpWindowCapture_MapPointer(
    _In_ PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel,
    _Inout_ PLONG X,
    _Inout_ PLONG Y)
{
    LONG PixelX, PixelY;

    PixelX = Channel->MonitorRect.left +
             MulDiv(*X, Channel->MonitorRect.right - Channel->MonitorRect.left - 1, MAXUSHORT);
    PixelY = Channel->MonitorRect.top +
             MulDiv(*Y, Channel->MonitorRect.bottom - Channel->MonitorRect.top - 1, MAXUSHORT);
    *X = MulDiv(PixelX - Channel->VirtualRect.left,
                MAXUSHORT,
                Channel->VirtualRect.right - Channel->VirtualRect.left - 1);
    *Y = MulDiv(PixelY - Channel->VirtualRect.top,
                MAXUSHORT,
                Channel->VirtualRect.bottom - Channel->VirtualRect.top - 1);
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpWindowCapture_ChannelData(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel = (PZP_CLIENT_WINDOW_CAPTURE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    const BYTE* Data = Message->Data.Buffer;
    ULONG Length = Message->Data.Length;
    INPUT Input = { 0 };
    BYTE Type;
    USHORT Flags;
    NTSTATUS Status;

    if (Length < sizeof(BYTE)) return STATUS_PROTOCOL_UNREACHABLE;
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || Length > Channel->ReceiveCredit)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    Channel->ReceiveCredit -= Length;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    Type = Data[0];
    if (Type == ZpWindowInputFrameAck)
    {
        const BYTE* Cursor = Data + sizeof(BYTE);
        ULONG Sequence;
        BYTE AckFlags;

        if (Length != 2 * sizeof(BYTE) + sizeof(ULONG)) return STATUS_PROTOCOL_UNREACHABLE;
        AckFlags = *Cursor++;
        Sequence = ZpWindowCapture_ReadUInt32(Cursor);
        RtlAcquireSRWLockExclusive(&Object->Lock);
        if (FlagOn(AckFlags, ~ZP_WINDOW_FRAME_ACK_FLAGS_MASK) || Sequence == 0 ||
            Sequence != Channel->PendingFrameSequence || Channel->AcknowledgedFrameSequence != 0)
        {
            Status = STATUS_PROTOCOL_UNREACHABLE;
        }
        else
        {
            Channel->AcknowledgedFrameSequence = Sequence;
            Channel->FrameAckFlags = AckFlags;
            NtSetEvent(Channel->RenderEvent, NULL);
            Status = STATUS_SUCCESS;
        }
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return ZpWindowCapture_CompleteInput(Channel, Length, Status);
    }
    if (Type == ZpWindowInputVideoCodecs)
    {
        ULONG VideoWidth, VideoHeight;

        if (Length != 2 * sizeof(BYTE) + 2 * sizeof(USHORT) ||
            FlagOn(Data[1], ~ZP_WINDOW_VIDEO_CODECS_MASK))
        {
            return STATUS_PROTOCOL_UNREACHABLE;
        }
        VideoWidth = ZpWindowCapture_ReadUInt16(Data + 2 * sizeof(BYTE));
        VideoHeight = ZpWindowCapture_ReadUInt16(Data + 2 * sizeof(BYTE) + sizeof(USHORT));
        if (VideoWidth < ZP_WINDOW_CAPTURE_MIN_VIDEO_DIMENSION ||
            VideoWidth > ZP_WINDOW_CAPTURE_MAX_DIMENSION ||
            VideoHeight < ZP_WINDOW_CAPTURE_MIN_VIDEO_DIMENSION ||
            VideoHeight > ZP_WINDOW_CAPTURE_MAX_DIMENSION)
        {
            return STATUS_PROTOCOL_UNREACHABLE;
        }
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Channel->VideoCodecs = Data[1];
        Channel->VideoCodecWidth = VideoWidth;
        Channel->VideoCodecHeight = VideoHeight;
        Channel->VideoCodecsChanged = TRUE;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return ZpWindowCapture_CompleteInput(Channel, Length, STATUS_SUCCESS);
    }
    if (Type == ZpWindowInputOptions)
    {
        const BYTE* Cursor = Data + sizeof(BYTE);
        PZP_WINDOW_SHARED_CAPTURE Capture;
        USHORT MaxDimension;
        BYTE FrameRate, Quality, Encoding;
        HRESULT Result;

        if (Length != sizeof(BYTE) + sizeof(USHORT) + 3 * sizeof(BYTE))
        {
            return STATUS_PROTOCOL_UNREACHABLE;
        }
        MaxDimension = ZpWindowCapture_ReadUInt16(Cursor);
        Cursor += sizeof(USHORT);
        FrameRate = *Cursor++;
        Quality = *Cursor++;
        Encoding = *Cursor;
        if (MaxDimension == 0 || MaxDimension > ZP_WINDOW_CAPTURE_MAX_DIMENSION ||
            FrameRate == 0 || FrameRate > ZP_WINDOW_CAPTURE_MAX_FRAME_RATE ||
            Quality == 0 || Quality > 100 || (Encoding & 3) > ZpWindowCaptureModeVideo ||
            ((Encoding >> 2) & 1) > ZpWindowVideoCodecH265 || (Encoding >> 3) != 0)
        {
            return STATUS_PROTOCOL_UNREACHABLE;
        }
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Channel->Options.MaxDimension = MaxDimension;
        Channel->Options.FrameRate = FrameRate;
        Channel->Options.Quality = Quality;
        Channel->Options.Encoding.Mode = Encoding & 3;
        Channel->Options.Encoding.Codec = (Encoding >> 2) & 1;
        Capture = Channel->Capture;
        Result = Capture != NULL ?
                     ZpWindowShared_Update(Capture, MaxDimension, FrameRate, Quality) : S_OK;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Status = SUCCEEDED(Result) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
        return ZpWindowCapture_CompleteInput(Channel, Length, Status);
    }
    if (!FlagOn(Channel->Options.Flags, ZP_WINDOW_CAPTURE_DESKTOP))
    {
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    if (Type == ZpWindowInputClipboard)
    {
        Status = ZpWindowCapture_SetClipboard(Data + sizeof(BYTE), Length - sizeof(BYTE));
        return ZpWindowCapture_CompleteInput(Channel, Length, Status);
    }
    if (Type == ZpWindowInputKeyboard)
    {
        if (Length != sizeof(BYTE) + 2 * sizeof(USHORT)) return STATUS_PROTOCOL_UNREACHABLE;
        Flags = ZpWindowCapture_ReadUInt16(Data + sizeof(BYTE));
        if (FlagOn(Flags, ~ZP_WINDOW_KEY_FLAGS_MASK)) return STATUS_PROTOCOL_UNREACHABLE;
        Input.type = INPUT_KEYBOARD;
        Input.ki.wScan = ZpWindowCapture_ReadUInt16(Data + sizeof(BYTE) + sizeof(USHORT));
        if (Input.ki.wScan == 0 || Input.ki.wScan >= ZP_WINDOW_PRESSED_KEY_COUNT / 2)
        {
            return STATUS_PROTOCOL_UNREACHABLE;
        }
        Input.ki.dwFlags = KEYEVENTF_SCANCODE |
                           (FlagOn(Flags, ZP_WINDOW_KEY_UP) ? KEYEVENTF_KEYUP : 0) |
                            (FlagOn(Flags, ZP_WINDOW_KEY_EXTENDED) ? KEYEVENTF_EXTENDEDKEY : 0);
    }
    else if (Type == ZpWindowInputMouse)
    {
        if (Length != sizeof(BYTE) + 4 * sizeof(USHORT)) return STATUS_PROTOCOL_UNREACHABLE;
        Flags = ZpWindowCapture_ReadUInt16(Data + sizeof(BYTE));
        if (Flags == 0 || FlagOn(Flags, ~ZP_WINDOW_MOUSE_FLAGS_MASK)) return STATUS_PROTOCOL_UNREACHABLE;
        Input.type = INPUT_MOUSE;
        Input.mi.dx = ZpWindowCapture_ReadUInt16(Data + sizeof(BYTE) + sizeof(USHORT));
        Input.mi.dy = ZpWindowCapture_ReadUInt16(Data + sizeof(BYTE) + 2 * sizeof(USHORT));
        Status = ZpWindowCapture_MapPointer(Channel, &Input.mi.dx, &Input.mi.dy);
        if (!NT_SUCCESS(Status)) return Status;
        Input.mi.mouseData = (ULONG)(LONG)(SHORT)ZpWindowCapture_ReadUInt16(
            Data + sizeof(BYTE) + 3 * sizeof(USHORT));
        Input.mi.dwFlags = (FlagOn(Flags, ZP_WINDOW_MOUSE_MOVE) ?
                                MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK : 0) |
                           (FlagOn(Flags, ZP_WINDOW_MOUSE_LEFT_DOWN) ? MOUSEEVENTF_LEFTDOWN : 0) |
                           (FlagOn(Flags, ZP_WINDOW_MOUSE_LEFT_UP) ? MOUSEEVENTF_LEFTUP : 0) |
                           (FlagOn(Flags, ZP_WINDOW_MOUSE_RIGHT_DOWN) ? MOUSEEVENTF_RIGHTDOWN : 0) |
                           (FlagOn(Flags, ZP_WINDOW_MOUSE_RIGHT_UP) ? MOUSEEVENTF_RIGHTUP : 0) |
                           (FlagOn(Flags, ZP_WINDOW_MOUSE_MIDDLE_DOWN) ? MOUSEEVENTF_MIDDLEDOWN : 0) |
                           (FlagOn(Flags, ZP_WINDOW_MOUSE_MIDDLE_UP) ? MOUSEEVENTF_MIDDLEUP : 0) |
                           (FlagOn(Flags, ZP_WINDOW_MOUSE_WHEEL) ? MOUSEEVENTF_WHEEL : 0) |
                            (FlagOn(Flags, ZP_WINDOW_MOUSE_HWHEEL) ? MOUSEEVENTF_HWHEEL : 0);
    }
    else
    {
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending)
    {
        Status = STATUS_PROTOCOL_UNREACHABLE;
    }
    else
    {
        NtUserSendInput(1, &Input, sizeof(Input));
        if (Type == ZpWindowInputKeyboard)
        {
            ZpWindowCapture_TrackKey(Channel, Input.ki.wScan, Flags);
        }
        else
        {
            ZpWindowCapture_TrackMouse(Channel, Flags);
        }
        Status = STATUS_SUCCESS;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    return ZpWindowCapture_CompleteInput(Channel, Length, Status);
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
    NtSetEvent(Channel->RenderEvent, NULL);
    ZpWindowCapture_ReleaseInput(Channel);
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
    NtSetEvent(Channel->RenderEvent, NULL);
    ZpWindowCapture_ReleaseInput(Channel);
}

static
VOID
ZpWindowCapture_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel = (PZP_CLIENT_WINDOW_CAPTURE_CHANNEL)LocalChannel;

    if (Channel->WorkerThread != NULL) NtClose(Channel->WorkerThread);
    if (Channel->RenderEvent != NULL) NtClose(Channel->RenderEvent);
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
    if (FlagOn(Options->Flags, ZP_WINDOW_CAPTURE_DESKTOP))
    {
        HMONITOR Monitor;
        HRESULT Result = ZpWindowCapture_ResolveMonitor(Options->MonitorIndex,
                                                        &Monitor,
                                                        &CaptureChannel->MonitorRect);

        CaptureChannel->VirtualRect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        CaptureChannel->VirtualRect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        CaptureChannel->VirtualRect.right = CaptureChannel->VirtualRect.left +
                                            GetSystemMetrics(SM_CXVIRTUALSCREEN);
        CaptureChannel->VirtualRect.bottom = CaptureChannel->VirtualRect.top +
                                             GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (FAILED(Result) || CaptureChannel->MonitorRect.right - CaptureChannel->MonitorRect.left < 2 ||
            CaptureChannel->MonitorRect.bottom - CaptureChannel->MonitorRect.top < 2 ||
            CaptureChannel->VirtualRect.right - CaptureChannel->VirtualRect.left < 2 ||
            CaptureChannel->VirtualRect.bottom - CaptureChannel->VirtualRect.top < 2)
        {
            Mem_Free(CaptureChannel);
            return FAILED(Result) && Result != E_INVALIDARG ? STATUS_NOT_FOUND : STATUS_INVALID_PARAMETER;
        }
    }
    Status = NtCreateEvent(&CaptureChannel->CreditEvent,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           NULL,
                           NotificationEvent,
                           FALSE);
    if (NT_SUCCESS(Status))
    {
        Status = NtCreateEvent(&CaptureChannel->RenderEvent,
                               EVENT_MODIFY_STATE | SYNCHRONIZE,
                               NULL,
                               NotificationEvent,
                               FALSE);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpClientLocalChannel_Insert(Object,
                                             &CaptureChannel->Header,
                                             ZP_WINDOW_MODULE_ID,
                                             ZpWindowCapture_ChannelData,
                                             ZpWindowCapture_ChannelWindow,
                                             ZpWindowCapture_ChannelClose,
                                             ZpWindow_CommitCaptureChannel,
                                             ZpWindowCapture_ChannelAbort,
                                             ZpWindowCapture_ChannelDestroy);
    }
    if (!NT_SUCCESS(Status))
    {
        if (CaptureChannel->RenderEvent != NULL) NtClose(CaptureChannel->RenderEvent);
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
    _Outptr_result_maybenull_ PZP_CLIENT_LOCAL_CHANNEL* Channel)
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

    if (OperationId == ZP_WINDOW_OPERATION_ENUMERATE)
    {
        Status = RequestLength == 0 ? ZpWindow_Enumerate(Response, ResponseLength) : STATUS_INVALID_PARAMETER;
    }
    else if (OperationId == ZP_WINDOW_OPERATION_ENUMERATE_MONITORS)
    {
        Status = RequestLength == 0 ? ZpWindow_EnumerateMonitors(Response, ResponseLength) :
                                      STATUS_INVALID_PARAMETER;
    }
    else if (OperationId == ZP_WINDOW_OPERATION_QUERY)
    {
        Status = ZpWindow_DecodeIdentity(Request, RequestLength, &Handle, &ProcessId, &ThreadId);
        if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
        return ZpWindow_Query(Handle, ProcessId, ThreadId, Response, ResponseLength);
    }
    else if (OperationId == ZP_WINDOW_OPERATION_CONTROL)
    {
        Status = ZpWindow_DecodeControl(Request, RequestLength, &Handle, &ProcessId, &ThreadId, &Control);
        if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
        return ZpWindow_Control(Handle, ProcessId, ThreadId, Control);
    }
    else if (OperationId == ZP_WINDOW_OPERATION_UPDATE)
    {
        Status = ZpWindow_DecodeUpdate(Request, RequestLength, &Update);
        if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
        return ZpWindow_Update(&Update);
    }
    else if (OperationId == ZP_WINDOW_OPERATION_CAPTURE)
    {
        Status = ZpWindow_DecodeCaptureRequest(Request, RequestLength, &CaptureOptions);
        if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
        return ZpWindow_Capture(&CaptureOptions, Response, ResponseLength);
    }
    else if (OperationId != ZP_WINDOW_OPERATION_OPEN_CAPTURE)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    else
    {
        Status = ZpWindow_DecodeCaptureRequest(Request, RequestLength, &CaptureOptions);
        if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
        Result = ZpWindow_ValidateCaptureIdentity(&CaptureOptions, &Window);
        if (!ZpStatus_IsSuccess(Result)) return Result;
        Status = ZpWindowCapture_CreateChannel(Client, &CaptureOptions, &CaptureChannel);
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
            ZpWindow_CommitCaptureChannel(&CaptureChannel->Header, FALSE);
            return ZpStatus_FromNtStatus(Status);
        }
        *Channel = &CaptureChannel->Header;
        return ZpStatus_Make(ZpStatusNone, 0);
    }
    return ZpStatus_FromNtStatus(Status);
}

VOID
ZpWindow_CommitCaptureChannel(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_WINDOW_CAPTURE_CHANNEL Channel = (PZP_CLIENT_WINDOW_CAPTURE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed = FALSE, StartWorker = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ResponseSent)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    }
    else
    {
        Status = ZpWindowCapture_SendWindowLocked(Channel,
                                                  ZP_CLIENT_DEFAULT_CHANNEL_WINDOW_SIZE);
        if (!NT_SUCCESS(Status)) Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        else
        {
            Channel->WorkerActive = TRUE;
            ZpClientLocalChannel_AddRef(&Channel->Header);
            Object->CallbackCount++;
            StartWorker = TRUE;
        }
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    if (!StartWorker) return;
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
