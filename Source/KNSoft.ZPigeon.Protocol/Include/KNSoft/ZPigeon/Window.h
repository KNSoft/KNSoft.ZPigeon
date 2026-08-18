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
#define ZP_WINDOW_CAPTURE_FLAGS_MASK ZP_WINDOW_CAPTURE_CURSOR
#define ZP_WINDOW_CAPTURE_DEFAULT_FRAME_RATE 12
#define ZP_WINDOW_CAPTURE_DEFAULT_QUALITY 85
#define ZP_WINDOW_CAPTURE_DEFAULT_MAX_DIMENSION 1280
#define ZP_WINDOW_CAPTURE_MAX_FRAME_RATE 60
#define ZP_WINDOW_CAPTURE_MAX_DIMENSION 7680

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
} ZP_WINDOW_CAPTURE_OPTIONS, *PZP_WINDOW_CAPTURE_OPTIONS;

typedef const ZP_WINDOW_CAPTURE_OPTIONS* PCZP_WINDOW_CAPTURE_OPTIONS;

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
ZpWindow_GetRecord(
    _In_ PCZP_WINDOW_LIST_VIEW List,
    _In_ ULONG Index,
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
ZpWindow_EncodeCaptureChannel(
    _In_ ULONGLONG ChannelId,
    _Out_writes_bytes_opt_(BufferSize) PVOID Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesWritten);

NTSTATUS
ZpWindow_DecodeCaptureChannel(
    _In_reads_bytes_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Out_ PULONGLONG ChannelId);

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
