#pragma once

#include <KNSoft/ZPigeon/File.h>

EXTERN_C_START

typedef struct _ZP_FILE_DOWNLOAD_SNAPSHOT ZP_FILE_DOWNLOAD_SNAPSHOT, *PZP_FILE_DOWNLOAD_SNAPSHOT;

NTSTATUS
ZpFileDownload_Start(_In_ ZP_FILE_DOWNLOAD_ENGINE Engine, _In_ BYTE Flags, _In_reads_(IdLength) PCWCH Id,
                     _In_ ULONG IdLength, _In_reads_(UrlLength) PCWCH Url, _In_ ULONG UrlLength,
                     _In_reads_(PathLength) PCWCH Path, _In_ ULONG PathLength);

NTSTATUS
ZpFileDownload_Cancel(_In_reads_(IdLength) PCWCH Id, _In_ ULONG IdLength);

NTSTATUS
ZpFileDownload_CreateSnapshot(_Outptr_ PZP_FILE_DOWNLOAD_SNAPSHOT* Snapshot,
                              _Outptr_result_buffer_(*Count) PCZP_FILE_DOWNLOAD_RECORD* Records, _Out_ PULONG Count);

VOID ZpFileDownload_DestroySnapshot(_In_ PZP_FILE_DOWNLOAD_SNAPSHOT Snapshot);

EXTERN_C_END
