#pragma once

#include <KNSoft/ZPigeon/Protocol.h>

EXTERN_C_START

#define ZP_WINDOW_MODULE_ID 8
#define ZP_WINDOW_MODULE_VERSION 1
#define ZP_WINDOW_OPERATION_ENUMERATE 1
#define ZP_WINDOW_OPERATION_QUERY 2
#define ZP_WINDOW_OPERATION_CONTROL 3
#define ZP_WINDOW_OPERATION_UPDATE 4
#define ZP_WINDOW_OPERATION_CAPTURE 5
#define ZP_WINDOW_OPERATION_OPEN_CAPTURE 6
#define ZP_WINDOW_OPERATION_ENUMERATE_MONITORS 7

typedef USHORT ZP_WINDOW_CONTROL, *PZP_WINDOW_CONTROL;

#define ZpWindowControlShow ((ZP_WINDOW_CONTROL)1)
#define ZpWindowControlHide ((ZP_WINDOW_CONTROL)2)
#define ZpWindowControlMinimize ((ZP_WINDOW_CONTROL)3)
#define ZpWindowControlMaximize ((ZP_WINDOW_CONTROL)4)
#define ZpWindowControlRestore ((ZP_WINDOW_CONTROL)5)
#define ZpWindowControlForeground ((ZP_WINDOW_CONTROL)6)
#define ZpWindowControlClose ((ZP_WINDOW_CONTROL)7)
#define ZpWindowControlHighlight ((ZP_WINDOW_CONTROL)8)
#define ZpWindowControlEnable ((ZP_WINDOW_CONTROL)9)
#define ZpWindowControlDisable ((ZP_WINDOW_CONTROL)10)
#define ZpWindowControlTopmost ((ZP_WINDOW_CONTROL)11)
#define ZpWindowControlNotTopmost ((ZP_WINDOW_CONTROL)12)

#define ZP_WINDOW_UPDATE_CAPTION 0x00000001UL
#define ZP_WINDOW_UPDATE_RECT 0x00000002UL
#define ZP_WINDOW_UPDATE_STYLE 0x00000004UL
#define ZP_WINDOW_UPDATE_EXSTYLE 0x00000008UL
#define ZP_WINDOW_UPDATE_MASK 0x0000000FUL
#define ZP_WINDOW_CAPTION_MAX_CCH 512

#define ZP_WINDOW_FLAG_VISIBLE 0x00000001UL
#define ZP_WINDOW_FLAG_ENABLED 0x00000002UL
#define ZP_WINDOW_FLAG_UNICODE 0x00000004UL
#define ZP_WINDOW_FLAG_MINIMIZED 0x00000008UL
#define ZP_WINDOW_FLAG_MAXIMIZED 0x00000010UL
#define ZP_WINDOW_FLAG_TOP_LEVEL 0x00000020UL
#define ZP_WINDOW_FLAG_HUNG 0x00000040UL
#define ZP_WINDOW_FLAG_TOPMOST 0x00000080UL
#define ZP_WINDOW_FLAG_DESKTOP 0x00000100UL

#define ZP_WINDOW_CAPTURE_CURSOR 0x00000001UL
#define ZP_WINDOW_CAPTURE_DESKTOP 0x00000002UL
#define ZP_WINDOW_CAPTURE_FLAGS_MASK (ZP_WINDOW_CAPTURE_CURSOR | ZP_WINDOW_CAPTURE_DESKTOP)

typedef USHORT ZP_WINDOW_INPUT_TYPE, *PZP_WINDOW_INPUT_TYPE;

#define ZpWindowInputMouse ((ZP_WINDOW_INPUT_TYPE)1)
#define ZpWindowInputKeyboard ((ZP_WINDOW_INPUT_TYPE)2)
#define ZpWindowInputClipboard ((ZP_WINDOW_INPUT_TYPE)3)

#define ZP_WINDOW_MOUSE_MOVE 0x0001
#define ZP_WINDOW_MOUSE_LEFT_DOWN 0x0002
#define ZP_WINDOW_MOUSE_LEFT_UP 0x0004
#define ZP_WINDOW_MOUSE_RIGHT_DOWN 0x0008
#define ZP_WINDOW_MOUSE_RIGHT_UP 0x0010
#define ZP_WINDOW_MOUSE_MIDDLE_DOWN 0x0020
#define ZP_WINDOW_MOUSE_MIDDLE_UP 0x0040
#define ZP_WINDOW_MOUSE_WHEEL 0x0080
#define ZP_WINDOW_MOUSE_HWHEEL 0x0100
#define ZP_WINDOW_MOUSE_FLAGS_MASK 0x01FF

#define ZP_WINDOW_KEY_UP 0x0001
#define ZP_WINDOW_KEY_EXTENDED 0x0002
#define ZP_WINDOW_KEY_FLAGS_MASK 0x0003
#define ZP_WINDOW_CAPTURE_DEFAULT_FRAME_RATE 12
#define ZP_WINDOW_CAPTURE_DEFAULT_QUALITY 85
#define ZP_WINDOW_CAPTURE_DEFAULT_MAX_DIMENSION 1280
#define ZP_WINDOW_CAPTURE_MAX_FRAME_RATE 60
#define ZP_WINDOW_CAPTURE_MAX_DIMENSION 7680
#define ZP_WINDOW_CAPTURE_PRIMARY_MONITOR MAXULONG
#define ZP_WINDOW_CAPTURE_REQUEST_WIRE_SIZE \
    (sizeof(ULONGLONG) + 6 * sizeof(ULONG) + 2 * sizeof(USHORT))

typedef USHORT ZP_WINDOW_CAPTURE_RECORD_TYPE, *PZP_WINDOW_CAPTURE_RECORD_TYPE;

#define ZpWindowCaptureRecordKeyFrame ((ZP_WINDOW_CAPTURE_RECORD_TYPE)1)
#define ZpWindowCaptureRecordPatch ((ZP_WINDOW_CAPTURE_RECORD_TYPE)2)

typedef struct _ZP_WINDOW_CAPTURE_OPTIONS
{
    ULONGLONG Handle;
    ULONG ProcessId;
    ULONG ThreadId;
    ULONG Flags;
    ULONG MaxDimension;
    USHORT FrameRate;
    USHORT Quality;
    ULONG DirectStreamId;
    ULONG MonitorIndex;
} ZP_WINDOW_CAPTURE_OPTIONS, *PZP_WINDOW_CAPTURE_OPTIONS;

typedef const ZP_WINDOW_CAPTURE_OPTIONS* PCZP_WINDOW_CAPTURE_OPTIONS;

typedef struct _ZP_WINDOW_MONITOR
{
    ULONG Index;
    ULONG Flags;
    LONG Left;
    LONG Top;
    LONG Right;
    LONG Bottom;
    LONG WorkLeft;
    LONG WorkTop;
    LONG WorkRight;
    LONG WorkBottom;
    PCWCH Device;
    ULONG DeviceLength;
} ZP_WINDOW_MONITOR, *PZP_WINDOW_MONITOR;

typedef const ZP_WINDOW_MONITOR* PCZP_WINDOW_MONITOR;

typedef struct _ZP_WINDOW_MONITOR_VIEW
{
    ULONG Index;
    ULONG Flags;
    LONG Left;
    LONG Top;
    LONG Right;
    LONG Bottom;
    LONG WorkLeft;
    LONG WorkTop;
    LONG WorkRight;
    LONG WorkBottom;
    ZP_STRING_VIEW Device;
} ZP_WINDOW_MONITOR_VIEW, *PZP_WINDOW_MONITOR_VIEW;

typedef struct _ZP_WINDOW_MONITOR_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_WINDOW_MONITOR_LIST_VIEW, *PZP_WINDOW_MONITOR_LIST_VIEW;

typedef const ZP_WINDOW_MONITOR_LIST_VIEW* PCZP_WINDOW_MONITOR_LIST_VIEW;

typedef struct _ZP_WINDOW_CAPTURE_RECORD
{
    ZP_WINDOW_CAPTURE_RECORD_TYPE Type;
    ULONG Sequence;
    ULONG CanvasWidth;
    ULONG CanvasHeight;
    ULONG Left;
    ULONG Top;
    ULONG Width;
    ULONG Height;
    ULONG DataLength;
} ZP_WINDOW_CAPTURE_RECORD, *PZP_WINDOW_CAPTURE_RECORD;

typedef const ZP_WINDOW_CAPTURE_RECORD* PCZP_WINDOW_CAPTURE_RECORD;

typedef struct _ZP_WINDOW_RECORD
{
    ULONGLONG Handle;
    ULONGLONG ParentHandle;
    ULONG ProcessId;
    ULONG ThreadId;
    ULONG Style;
    ULONG ExStyle;
    ULONG Flags;
    PCWCH Caption;
    ULONG CaptionLength;
    PCWCH ClassName;
    ULONG ClassNameLength;
} ZP_WINDOW_RECORD, *PZP_WINDOW_RECORD;

typedef const ZP_WINDOW_RECORD* PCZP_WINDOW_RECORD;

typedef struct _ZP_WINDOW_RECORD_VIEW
{
    ULONGLONG Handle;
    ULONGLONG ParentHandle;
    ULONG ProcessId;
    ULONG ThreadId;
    ULONG Style;
    ULONG ExStyle;
    ULONG Flags;
    ZP_STRING_VIEW Caption;
    ZP_STRING_VIEW ClassName;
} ZP_WINDOW_RECORD_VIEW, *PZP_WINDOW_RECORD_VIEW;

typedef struct _ZP_WINDOW_LIST_VIEW
{
    const BYTE* Buffer;
    ULONG Length;
    ULONG Count;
} ZP_WINDOW_LIST_VIEW, *PZP_WINDOW_LIST_VIEW;

typedef const ZP_WINDOW_LIST_VIEW* PCZP_WINDOW_LIST_VIEW;

typedef struct _ZP_WINDOW_INFO
{
    ZP_WINDOW_RECORD Record;
    ULONGLONG OwnerHandle;
    LONG WindowLeft;
    LONG WindowTop;
    LONG WindowRight;
    LONG WindowBottom;
    LONG ClientLeft;
    LONG ClientTop;
    LONG ClientRight;
    LONG ClientBottom;
    ULONG WindowStatus;
    ULONG BorderWidth;
    ULONG BorderHeight;
    USHORT ClassAtom;
    USHORT CreatorVersion;
    ULONGLONG PreviousHandle;
    ULONGLONG NextHandle;
    ULONGLONG FirstChildHandle;
    ULONGLONG FirstSiblingHandle;
    ULONGLONG LastSiblingHandle;
    LONG MonitorLeft;
    LONG MonitorTop;
    LONG MonitorRight;
    LONG MonitorBottom;
    PCWCH MonitorDevice;
    ULONG MonitorDeviceLength;
} ZP_WINDOW_INFO, *PZP_WINDOW_INFO;

typedef const ZP_WINDOW_INFO* PCZP_WINDOW_INFO;

typedef struct _ZP_WINDOW_INFO_VIEW
{
    ZP_WINDOW_RECORD_VIEW Record;
    ULONGLONG OwnerHandle;
    LONG WindowLeft;
    LONG WindowTop;
    LONG WindowRight;
    LONG WindowBottom;
    LONG ClientLeft;
    LONG ClientTop;
    LONG ClientRight;
    LONG ClientBottom;
    ULONG WindowStatus;
    ULONG BorderWidth;
    ULONG BorderHeight;
    USHORT ClassAtom;
    USHORT CreatorVersion;
    ULONGLONG PreviousHandle;
    ULONGLONG NextHandle;
    ULONGLONG FirstChildHandle;
    ULONGLONG FirstSiblingHandle;
    ULONGLONG LastSiblingHandle;
    LONG MonitorLeft;
    LONG MonitorTop;
    LONG MonitorRight;
    LONG MonitorBottom;
    ZP_STRING_VIEW MonitorDevice;
} ZP_WINDOW_INFO_VIEW, *PZP_WINDOW_INFO_VIEW;

typedef struct _ZP_WINDOW_UPDATE
{
    ULONGLONG Handle;
    ULONG ProcessId;
    ULONG ThreadId;
    ULONG Fields;
    PCWCH Caption;
    ULONG CaptionLength;
    LONG Left;
    LONG Top;
    LONG Right;
    LONG Bottom;
    ULONG Style;
    ULONG ExStyle;
} ZP_WINDOW_UPDATE, *PZP_WINDOW_UPDATE;

typedef const ZP_WINDOW_UPDATE* PCZP_WINDOW_UPDATE;

typedef struct _ZP_WINDOW_UPDATE_VIEW
{
    ULONGLONG Handle;
    ULONG ProcessId;
    ULONG ThreadId;
    ULONG Fields;
    ZP_STRING_VIEW Caption;
    LONG Left;
    LONG Top;
    LONG Right;
    LONG Bottom;
    ULONG Style;
    ULONG ExStyle;
} ZP_WINDOW_UPDATE_VIEW, *PZP_WINDOW_UPDATE_VIEW;

NTSTATUS
ZpWindow_EncodeList(
    _In_reads_opt_(WindowCount) PCZP_WINDOW_RECORD Windows,
    _In_ ULONG WindowCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_DecodeList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WINDOW_LIST_VIEW View);

NTSTATUS
ZpWindow_GetNextRecord(
    _In_ PCZP_WINDOW_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_WINDOW_RECORD_VIEW Record);

NTSTATUS
ZpWindow_EncodeIdentity(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_DecodeIdentity(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG Handle,
    _Out_ PULONG ProcessId,
    _Out_ PULONG ThreadId);

NTSTATUS
ZpWindow_EncodeCaptureRequest(
    _In_ PCZP_WINDOW_CAPTURE_OPTIONS Options,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_DecodeCaptureRequest(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WINDOW_CAPTURE_OPTIONS Options);

NTSTATUS
ZpWindow_EncodeMonitorList(
    _In_reads_opt_(MonitorCount) PCZP_WINDOW_MONITOR Monitors,
    _In_ ULONG MonitorCount,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_DecodeMonitorList(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WINDOW_MONITOR_LIST_VIEW View);

NTSTATUS
ZpWindow_GetNextMonitor(
    _In_ PCZP_WINDOW_MONITOR_LIST_VIEW List,
    _Inout_ PULONG Offset,
    _Out_ PZP_WINDOW_MONITOR_VIEW Monitor);

NTSTATUS
ZpWindow_EncodeCaptureChannel(
    _In_ ULONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_DecodeCaptureChannel(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONG ChannelId);

NTSTATUS
ZpWindow_EncodeCaptureRecord(
    _In_ PCZP_WINDOW_CAPTURE_RECORD Record,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_EncodeControl(
    _In_ ULONGLONG Handle,
    _In_ ULONG ProcessId,
    _In_ ULONG ThreadId,
    _In_ ZP_WINDOW_CONTROL Control,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_DecodeControl(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG Handle,
    _Out_ PULONG ProcessId,
    _Out_ PULONG ThreadId,
    _Out_ PZP_WINDOW_CONTROL Control);

NTSTATUS
ZpWindow_EncodeInfo(
    _In_ PCZP_WINDOW_INFO Info,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_DecodeInfo(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WINDOW_INFO_VIEW View);

NTSTATUS
ZpWindow_EncodeUpdate(
    _In_ PCZP_WINDOW_UPDATE Update,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_DecodeUpdate(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PZP_WINDOW_UPDATE_VIEW View);

EXTERN_C_END
