#include "Client.h"

#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <KNSoft/NDK/NT/Win32K/Win32KApi.h>

#pragma comment(lib, "KNSoft.NDK.Win32u.lib")

#define ZP_WINDOW_CLASS_CCH 256

typedef struct _ZP_WINDOW_ENTRY
{
    ZP_WINDOW_RECORD Record;
    WCHAR Caption[ZP_WINDOW_CAPTION_MAX_CCH];
    WCHAR ClassName[ZP_WINDOW_CLASS_CCH];
} ZP_WINDOW_ENTRY, *PZP_WINDOW_ENTRY;

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
    NTSTATUS Status;
    ULONG Capacity = 512, Count, Index, RecordCount = 0, Length;

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
    if (Count > MAXSIZE_T / (sizeof(*Entries) + sizeof(*Records)))
    {
        Mem_Free(Windows);
        return STATUS_INTEGER_OVERFLOW;
    }
    Entries = Mem_Alloc((SIZE_T)Count * (sizeof(*Entries) + sizeof(*Records)));
    if (Entries == NULL)
    {
        Mem_Free(Windows);
        return STATUS_NO_MEMORY;
    }
    Records = (PZP_WINDOW_RECORD)(Entries + Count);
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
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _Outptr_result_bytebuffer_(*PayloadLength) PBYTE* Payload,
    _Out_ PULONG PayloadLength)
{
    RECT Rect;
    HWND Window;
    HDC WindowDC, MemoryDC;
    HBITMAP Bitmap, OriginalBitmap;
    PBYTE Buffer;
    ULONG Length;
    W32ERROR Error;
    ZP_STATUS Status;

    Status = ZpWindow_ValidateIdentity(Handle, ProcessId, ThreadId, &Window);
    if (!ZpStatus_IsSuccess(Status)) return Status;
    if (!GetClientRect(Window, &Rect)) return ZpWindow_StatusFromLastError();
    if (Rect.right <= 0 || Rect.bottom <= 0)
    {
        return ZpStatus_FromCode(ZpStatusWin32, ERROR_INVALID_DATA);
    }
    if ((ULONGLONG)(ULONG)Rect.right * (ULONG)Rect.bottom >
        (ZP_FRAME_MAX_BODY_SIZE - 16) / sizeof(RGBQUAD))
    {
        return ZpStatus_FromCode(ZpStatusWin32, ERROR_FILE_TOO_LARGE);
    }
    WindowDC = GetDC(Window);
    if (WindowDC == NULL) return ZpWindow_StatusFromLastError();
    MemoryDC = CreateCompatibleDC(WindowDC);
    if (MemoryDC == NULL)
    {
        Status = ZpWindow_StatusFromLastError();
        goto ExitWindowDC;
    }
    Bitmap = CreateCompatibleBitmap(WindowDC, Rect.right, Rect.bottom);
    if (Bitmap == NULL)
    {
        Status = ZpWindow_StatusFromLastError();
        goto ExitMemoryDC;
    }
    OriginalBitmap = SelectObject(MemoryDC, Bitmap);
    if (OriginalBitmap == NULL)
    {
        Status = ZpWindow_StatusFromLastError();
        goto ExitBitmap;
    }
    Error = UI_SendMessageTimeout(Window,
                                  WM_PRINTCLIENT,
                                  (WPARAM)MemoryDC,
                                  PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND,
                                  SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                                  1000,
                                  NULL);
    if (Error != ERROR_SUCCESS &&
        !BitBlt(MemoryDC, 0, 0, Rect.right, Rect.bottom, WindowDC, 0, 0, SRCCOPY | CAPTUREBLT))
    {
        Status = ZpWindow_StatusFromLastError();
        SelectObject(MemoryDC, OriginalBitmap);
        goto ExitBitmap;
    }
    SelectObject(MemoryDC, OriginalBitmap);
    Error = UI_WriteBitmapFileData(MemoryDC, Bitmap, NULL, 0, &Length);
    if (Error == ERROR_SUCCESS && Length > ZP_FRAME_MAX_BODY_SIZE - 16)
    {
        Error = ERROR_FILE_TOO_LARGE;
    }
    Buffer = Error == ERROR_SUCCESS ? Mem_Alloc(Length) : NULL;
    if (Error == ERROR_SUCCESS && Buffer == NULL) Error = ERROR_NOT_ENOUGH_MEMORY;
    if (Error == ERROR_SUCCESS)
    {
        Error = UI_WriteBitmapFileData(MemoryDC, Bitmap, Buffer, Length, &Length);
    }
    if (Error != ERROR_SUCCESS)
    {
        Mem_Free(Buffer);
        Status = ZpStatus_FromCode(ZpStatusWin32, Error);
        goto ExitBitmap;
    }
    *Payload = Buffer;
    *PayloadLength = Length;
    Status = ZpStatus_Make(ZpStatusNone, 0);

ExitBitmap:
    DeleteObject(Bitmap);
ExitMemoryDC:
    DeleteDC(MemoryDC);
ExitWindowDC:
    ReleaseDC(Window, WindowDC);
    return Status;
}

ZP_STATUS
ZpWindow_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_maybenull_ PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ULONGLONG Handle;
    ULONG ProcessId, ThreadId;
    ZP_WINDOW_CONTROL Control;
    ZP_WINDOW_UPDATE_VIEW Update;
    NTSTATUS Status;
    ZP_STATUS Result;

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
            Status = ZpWindow_DecodeIdentity(Request,
                                             RequestLength,
                                             &Handle,
                                             &ProcessId,
                                             &ThreadId);
            return NT_SUCCESS(Status) ?
                       ZpWindow_Capture(Handle, ProcessId, ThreadId, Response, ResponseLength) :
                       ZpStatus_FromNtStatus(Status);

        default:
            return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
}
