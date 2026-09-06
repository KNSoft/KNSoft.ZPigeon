#pragma once

#include <KNSoft/NDK/NDK.h>
#include <winstring.h>

#ifdef __cplusplus
#include <string_view>
#include <winrt/Windows.Data.Json.h>
#endif

EXTERN_C_START

typedef BYTE ZP_JSON_TYPE, *PZP_JSON_TYPE;

#define ZpJsonNull ((ZP_JSON_TYPE)0)
#define ZpJsonBoolean ((ZP_JSON_TYPE)1)
#define ZpJsonNumber ((ZP_JSON_TYPE)2)
#define ZpJsonString ((ZP_JSON_TYPE)3)
#define ZpJsonArray ((ZP_JSON_TYPE)4)
#define ZpJsonObject ((ZP_JSON_TYPE)5)

typedef struct _ZP_JSON_VALUE ZP_JSON_VALUE, *PZP_JSON_VALUE;
typedef struct _ZP_JSON_ITERATOR ZP_JSON_ITERATOR, *PZP_JSON_ITERATOR;

NTSTATUS
ZpJson_ParseUtf8(
    _In_reads_bytes_(Length) const BYTE* Text,
    _In_ ULONG Length,
    _Outptr_ PZP_JSON_VALUE* Value);

NTSTATUS
ZpJson_ParseUtf8File(
    _In_ PCWSTR Path,
    _In_ ULONG MaximumSize,
    _Outptr_ PZP_JSON_VALUE* Value);

VOID
ZpJson_CloseValue(
    _In_opt_ PZP_JSON_VALUE Value);

NTSTATUS
ZpJson_GetType(
    _In_ PZP_JSON_VALUE Value,
    _Out_ PZP_JSON_TYPE Type);

NTSTATUS
ZpJson_GetSize(
    _In_ PZP_JSON_VALUE Value,
    _Out_ PULONG Size);

NTSTATUS
ZpJson_GetNamedValue(
    _In_ PZP_JSON_VALUE Value,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _Outptr_ PZP_JSON_VALUE* NamedValue);

NTSTATUS
ZpJson_CreateIterator(
    _In_ PZP_JSON_VALUE Value,
    _In_ ULONG StartIndex,
    _Outptr_ PZP_JSON_ITERATOR* Iterator);

NTSTATUS
ZpJson_IteratorNext(
    _Inout_ PZP_JSON_ITERATOR Iterator,
    _Out_ HSTRING* Name,
    _Outptr_ PZP_JSON_VALUE* Value);

VOID
ZpJson_CloseIterator(
    _In_opt_ PZP_JSON_ITERATOR Iterator);

NTSTATUS
ZpJson_GetString(
    _In_ PZP_JSON_VALUE Value,
    _Out_ HSTRING* Text);

NTSTATUS
ZpJson_Stringify(
    _In_ PZP_JSON_VALUE Value,
    _Out_ HSTRING* Text);

EXTERN_C_END

#ifdef __cplusplus
namespace ZpJson
{
    winrt::Windows::Data::Json::IJsonValue Parse(std::wstring_view Text);
}
#endif
