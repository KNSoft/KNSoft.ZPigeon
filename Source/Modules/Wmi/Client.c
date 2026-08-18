#define COBJMACROS

#include "Client.h"

#include <KNSoft/MakeLifeEasier/Memory/Core.h>
#include <wbemidl.h>
#include <wctype.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Wbemuuid.lib")

typedef struct _ZP_WMI_ROW_BUILDER
{
    PZP_WMI_CELL Cells;
    ULONG Count;
    ULONG Capacity;
} ZP_WMI_ROW_BUILDER, *PZP_WMI_ROW_BUILDER;

typedef struct _ZP_WMI_BUILDER
{
    PZP_WMI_ROW Rows;
    ULONG Count;
    ULONG Capacity;
} ZP_WMI_BUILDER, *PZP_WMI_BUILDER;

typedef struct _ZP_WMI_STRING_BUILDER
{
    PWSTR Buffer;
    ULONG Length;
    ULONG Capacity;
} ZP_WMI_STRING_BUILDER, *PZP_WMI_STRING_BUILDER;

static
LOGICAL
ZpWmi_IsNamespaceValid(
    _In_ PCZP_STRING_VIEW Namespace)
{
    PCWCH Buffer = (PCWCH)Namespace->Buffer;
    ULONG Index;

    if (Namespace->Length < 4 || towupper(Buffer[0]) != L'R' || towupper(Buffer[1]) != L'O' ||
        towupper(Buffer[2]) != L'O' || towupper(Buffer[3]) != L'T' ||
        (Namespace->Length != 4 && Buffer[4] != L'\\'))
    {
        return FALSE;
    }
    for (Index = 0; Index < Namespace->Length; Index++)
    {
        if (Buffer[Index] == UNICODE_NULL || Buffer[Index] == L':' || Buffer[Index] == L'/' ||
            (Index != 0 && Buffer[Index] == L'\\' && Buffer[Index - 1] == L'\\'))
        {
            return FALSE;
        }
    }
    return Buffer[Namespace->Length - 1] != L'\\';
}

static
LOGICAL
ZpWmi_IsQueryValid(
    _In_ PCZP_STRING_VIEW Query)
{
    PCWCH Buffer = (PCWCH)Query->Buffer;
    ULONG Index;

    for (Index = 0; Index < Query->Length; Index++)
    {
        if (Buffer[Index] == UNICODE_NULL) return FALSE;
    }
    return TRUE;
}

static
PWSTR
ZpWmi_CopyString(
    _In_reads_(Length) PCWCH Value,
    _In_ ULONG Length)
{
    PWSTR Copy;

    if (Length > ZP_WMI_MAX_CELL_LENGTH) return NULL;
    Copy = Mem_Alloc(((SIZE_T)Length + 1) * sizeof(WCHAR));
    if (Copy == NULL) return NULL;
    if (Length != 0) RtlCopyMemory(Copy, Value, (SIZE_T)Length * sizeof(WCHAR));
    Copy[Length] = UNICODE_NULL;
    return Copy;
}

static
VOID
ZpWmi_FreeRow(
    _Inout_ PZP_WMI_ROW_BUILDER Row)
{
    ULONG Index;

    for (Index = 0; Index < Row->Count; Index++)
    {
        Mem_Free((PVOID)Row->Cells[Index].Name);
        Mem_Free((PVOID)Row->Cells[Index].Value);
    }
    Mem_Free(Row->Cells);
}

static
VOID
ZpWmi_FreeBuilder(
    _Inout_ PZP_WMI_BUILDER Builder)
{
    ULONG Index;

    for (Index = 0; Index < Builder->Count; Index++)
    {
        ZP_WMI_ROW_BUILDER Row = {
            (PZP_WMI_CELL)Builder->Rows[Index].Cells,
            Builder->Rows[Index].CellCount,
            Builder->Rows[Index].CellCount
        };

        ZpWmi_FreeRow(&Row);
    }
    Mem_Free(Builder->Rows);
}

static
NTSTATUS
ZpWmi_AddCell(
    _Inout_ PZP_WMI_ROW_BUILDER Row,
    _In_ ULONG Type,
    _In_ PCWSTR Name,
    _In_ PCWSTR Value)
{
    ULONG NameLength = (ULONG)wcslen(Name), ValueLength = (ULONG)wcslen(Value);
    PZP_WMI_CELL Cells, Cell;
    PWSTR NameCopy, ValueCopy;

    if (Row->Count == ZP_WMI_MAX_CELLS || NameLength > ZP_WMI_MAX_CELL_LENGTH ||
        ValueLength > ZP_WMI_MAX_CELL_LENGTH)
    {
        return STATUS_QUOTA_EXCEEDED;
    }
    if (Row->Count == Row->Capacity)
    {
        ULONG Capacity = Row->Capacity == 0 ? 16 : min(Row->Capacity * 2, ZP_WMI_MAX_CELLS);

        Cells = Mem_ReAlloc(Row->Cells, (SIZE_T)Capacity * sizeof(*Cells));
        if (Cells == NULL) return STATUS_NO_MEMORY;
        Row->Cells = Cells;
        Row->Capacity = Capacity;
    }
    NameCopy = ZpWmi_CopyString(Name, NameLength);
    if (NameCopy == NULL) return STATUS_NO_MEMORY;
    ValueCopy = ZpWmi_CopyString(Value, ValueLength);
    if (ValueCopy == NULL)
    {
        Mem_Free(NameCopy);
        return STATUS_NO_MEMORY;
    }
    Cell = &Row->Cells[Row->Count++];
    Cell->Type = Type;
    Cell->Name = NameCopy;
    Cell->NameLength = NameLength;
    Cell->Value = ValueCopy;
    Cell->ValueLength = ValueLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpWmi_AddRow(
    _Inout_ PZP_WMI_BUILDER Builder,
    _Inout_ PZP_WMI_ROW_BUILDER Row)
{
    PZP_WMI_ROW Rows;

    if (Builder->Count == ZP_WMI_MAX_ROWS) return STATUS_QUOTA_EXCEEDED;
    if (Builder->Count == Builder->Capacity)
    {
        ULONG Capacity = Builder->Capacity == 0 ? 32 : min(Builder->Capacity * 2, ZP_WMI_MAX_ROWS);

        Rows = Mem_ReAlloc(Builder->Rows, (SIZE_T)Capacity * sizeof(*Rows));
        if (Rows == NULL) return STATUS_NO_MEMORY;
        Builder->Rows = Rows;
        Builder->Capacity = Capacity;
    }
    Builder->Rows[Builder->Count].Cells = Row->Cells;
    Builder->Rows[Builder->Count++].CellCount = Row->Count;
    Row->Cells = NULL;
    Row->Count = 0;
    Row->Capacity = 0;
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpWmi_AppendString(
    _Inout_ PZP_WMI_STRING_BUILDER Builder,
    _In_reads_(Length) PCWCH Value,
    _In_ ULONG Length)
{
    ULONG Required = Builder->Length + Length + 1;
    PWSTR Buffer;

    if (Required < Builder->Length || Required > ZP_WMI_MAX_CELL_LENGTH + 1) return STATUS_QUOTA_EXCEEDED;
    if (Required > Builder->Capacity)
    {
        ULONG Capacity = max(64, Builder->Capacity);

        while (Capacity < Required && Capacity <= (ZP_WMI_MAX_CELL_LENGTH + 1) / 2) Capacity *= 2;
        if (Capacity < Required) Capacity = Required;
        Buffer = Mem_ReAlloc(Builder->Buffer, (SIZE_T)Capacity * sizeof(WCHAR));
        if (Buffer == NULL) return STATUS_NO_MEMORY;
        Builder->Buffer = Buffer;
        Builder->Capacity = Capacity;
    }
    if (Length != 0) RtlCopyMemory(Builder->Buffer + Builder->Length, Value, (SIZE_T)Length * sizeof(WCHAR));
    Builder->Length += Length;
    Builder->Buffer[Builder->Length] = UNICODE_NULL;
    return STATUS_SUCCESS;
}

static
HRESULT
ZpWmi_ScalarToString(
    _In_ const VARIANT* Value,
    _Outptr_ PWSTR* Text)
{
    VARIANT Converted;
    PCWSTR Source;
    ULONG Length;
    HRESULT Result;

    if (V_VT(Value) == VT_EMPTY || V_VT(Value) == VT_NULL)
    {
        *Text = ZpWmi_CopyString(L"", 0);
        return *Text == NULL ? E_OUTOFMEMORY : S_OK;
    }
    VariantInit(&Converted);
    Result = VariantChangeType(&Converted, (VARIANT*)Value, VARIANT_ALPHABOOL, VT_BSTR);
    if (FAILED(Result)) return Result;
    Source = V_BSTR(&Converted) == NULL ? L"" : V_BSTR(&Converted);
    Length = (ULONG)wcslen(Source);
    *Text = ZpWmi_CopyString(Source, Length);
    VariantClear(&Converted);
    return *Text == NULL ? E_OUTOFMEMORY : S_OK;
}

static
HRESULT
ZpWmi_GetArrayElement(
    _In_ SAFEARRAY* Array,
    _In_ VARTYPE Type,
    _In_ LONG Index,
    _Out_ VARIANT* Value)
{
    HRESULT Result;

    VariantInit(Value);
    if (Type == VT_VARIANT) return SafeArrayGetElement(Array, &Index, Value);
    V_VT(Value) = Type;
    switch (Type)
    {
        case VT_BSTR: Result = SafeArrayGetElement(Array, &Index, &V_BSTR(Value)); break;
        case VT_BOOL: Result = SafeArrayGetElement(Array, &Index, &V_BOOL(Value)); break;
        case VT_I1: Result = SafeArrayGetElement(Array, &Index, &V_I1(Value)); break;
        case VT_UI1: Result = SafeArrayGetElement(Array, &Index, &V_UI1(Value)); break;
        case VT_I2: Result = SafeArrayGetElement(Array, &Index, &V_I2(Value)); break;
        case VT_UI2: Result = SafeArrayGetElement(Array, &Index, &V_UI2(Value)); break;
        case VT_I4:
        case VT_INT: Result = SafeArrayGetElement(Array, &Index, &V_I4(Value)); break;
        case VT_UI4:
        case VT_UINT: Result = SafeArrayGetElement(Array, &Index, &V_UI4(Value)); break;
        case VT_I8: Result = SafeArrayGetElement(Array, &Index, &V_I8(Value)); break;
        case VT_UI8: Result = SafeArrayGetElement(Array, &Index, &V_UI8(Value)); break;
        case VT_R4: Result = SafeArrayGetElement(Array, &Index, &V_R4(Value)); break;
        case VT_R8: Result = SafeArrayGetElement(Array, &Index, &V_R8(Value)); break;
        case VT_DATE: Result = SafeArrayGetElement(Array, &Index, &V_DATE(Value)); break;
        default:
            V_VT(Value) = VT_EMPTY;
            return DISP_E_BADVARTYPE;
    }
    if (FAILED(Result)) V_VT(Value) = VT_EMPTY;
    return Result;
}

static
HRESULT
ZpWmi_VariantToString(
    _In_ const VARIANT* Value,
    _Outptr_ PWSTR* Text)
{
    ZP_WMI_STRING_BUILDER Builder = { 0 };
    SAFEARRAY* Array;
    VARTYPE Type;
    LONG Lower, Upper, Index;
    HRESULT Result;

    if (!(V_VT(Value) & VT_ARRAY)) return ZpWmi_ScalarToString(Value, Text);
    Array = V_ARRAY(Value);
    if (Array == NULL || SafeArrayGetDim(Array) != 1) return E_INVALIDARG;
    Type = V_VT(Value) & VT_TYPEMASK;
    if (Type == VT_EMPTY) SafeArrayGetVartype(Array, &Type);
    Result = SafeArrayGetLBound(Array, 1, &Lower);
    if (SUCCEEDED(Result)) Result = SafeArrayGetUBound(Array, 1, &Upper);
    if (SUCCEEDED(Result)) for (Index = Lower; Index <= Upper; Index++)
    {
        VARIANT Element;
        PWSTR ElementText = NULL;

        Result = ZpWmi_GetArrayElement(Array, Type, Index, &Element);
        if (SUCCEEDED(Result)) Result = ZpWmi_ScalarToString(&Element, &ElementText);
        VariantClear(&Element);
        if (FAILED(Result)) break;
        if (Index != Lower)
        {
            NTSTATUS Status = ZpWmi_AppendString(&Builder, L"; ", 2);

            if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
        }
        if (SUCCEEDED(Result))
        {
            NTSTATUS Status = ZpWmi_AppendString(&Builder, ElementText, (ULONG)wcslen(ElementText));

            if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
        }
        Mem_Free(ElementText);
    }
    if (SUCCEEDED(Result) && Builder.Buffer == NULL)
    {
        Builder.Buffer = ZpWmi_CopyString(L"", 0);
        if (Builder.Buffer == NULL) Result = E_OUTOFMEMORY;
    }
    if (FAILED(Result)) Mem_Free(Builder.Buffer);
    else *Text = Builder.Buffer;
    return Result;
}

static
HRESULT
ZpWmi_AddObject(
    _Inout_ PZP_WMI_BUILDER Builder,
    _In_ IWbemClassObject* Object,
    _In_ ULONG Flags)
{
    ZP_WMI_ROW_BUILDER Row = { 0 };
    CIMTYPE Type;
    BSTR Name;
    VARIANT Value;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;

    Result = IWbemClassObject_BeginEnumeration(Object,
                                                Flags & ZP_WMI_FLAG_SYSTEM_PROPERTIES ? 0 :
                                                    WBEM_FLAG_NONSYSTEM_ONLY);
    while (SUCCEEDED(Result))
    {
        PWSTR Text = NULL;

        VariantInit(&Value);
        Name = NULL;
        Result = IWbemClassObject_Next(Object, 0, &Name, &Value, &Type, NULL);
        if (Result == WBEM_S_NO_MORE_DATA)
        {
            Result = S_OK;
            break;
        }
        if (FAILED(Result)) break;
        Result = ZpWmi_VariantToString(&Value, &Text);
        if (SUCCEEDED(Result)) Status = ZpWmi_AddCell(&Row, Type, Name, Text);
        Mem_Free(Text);
        SysFreeString(Name);
        VariantClear(&Value);
        if (!NT_SUCCESS(Status))
        {
            Result = HRESULT_FROM_NT(Status);
            break;
        }
    }
    IWbemClassObject_EndEnumeration(Object);
    if (SUCCEEDED(Result))
    {
        Status = ZpWmi_AddRow(Builder, &Row);
        if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    ZpWmi_FreeRow(&Row);
    return Result;
}

static
HRESULT
ZpWmi_AddSelectedProperties(
    _Inout_ PZP_WMI_BUILDER Builder,
    _In_ IWbemClassObject* Object,
    _In_reads_(NameCount) const PCWSTR* Names,
    _In_ ULONG NameCount)
{
    ZP_WMI_ROW_BUILDER Row = { 0 };
    ULONG Index;
    HRESULT Result = S_OK;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Index = 0; SUCCEEDED(Result) && Index < NameCount; Index++)
    {
        VARIANT Value;
        CIMTYPE Type;
        PWSTR Text = NULL;

        VariantInit(&Value);
        Result = IWbemClassObject_Get(Object, Names[Index], 0, &Value, &Type, NULL);
        if (SUCCEEDED(Result)) Result = ZpWmi_VariantToString(&Value, &Text);
        if (SUCCEEDED(Result)) Status = ZpWmi_AddCell(&Row, Type, Names[Index], Text);
        Mem_Free(Text);
        VariantClear(&Value);
        if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    if (SUCCEEDED(Result))
    {
        Status = ZpWmi_AddRow(Builder, &Row);
        if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    ZpWmi_FreeRow(&Row);
    return Result;
}

static
HRESULT
ZpWmi_Connect(
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _Outptr_ IWbemServices** Services)
{
    IWbemLocator* Locator;
    BSTR Path;
    HRESULT Result;

    Result = CoCreateInstance(&CLSID_WbemLocator,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IWbemLocator,
                              (PVOID*)&Locator);
    if (FAILED(Result)) return Result;
    Path = SysAllocStringLen(Namespace, NamespaceLength);
    if (Path == NULL)
    {
        IWbemLocator_Release(Locator);
        return E_OUTOFMEMORY;
    }
    Result = IWbemLocator_ConnectServer(Locator, Path, NULL, NULL, NULL, 0, NULL, NULL, Services);
    SysFreeString(Path);
    IWbemLocator_Release(Locator);
    if (SUCCEEDED(Result))
    {
        Result = CoSetProxyBlanket((IUnknown*)*Services,
                                   RPC_C_AUTHN_WINNT,
                                   RPC_C_AUTHZ_NONE,
                                   NULL,
                                   RPC_C_AUTHN_LEVEL_CALL,
                                   RPC_C_IMP_LEVEL_IMPERSONATE,
                                   NULL,
                                   EOAC_NONE);
        if (FAILED(Result))
        {
            IWbemServices_Release(*Services);
        }
    }
    return Result;
}

static
HRESULT
ZpWmi_CreateEnumerator(
    _In_ IWbemServices* Services,
    _In_ USHORT OperationId,
    _In_ PCZP_WMI_REQUEST_VIEW Request,
    _Outptr_ IEnumWbemClassObject** Enumerator)
{
    BSTR QueryLanguage, Query;
    HRESULT Result;

    if (OperationId == ZP_WMI_OPERATION_ENUMERATE_NAMESPACES)
    {
        return IWbemServices_CreateInstanceEnum(Services,
                                                 L"__NAMESPACE",
                                                 (LONG)WBEM_FLAG_SHALLOW | (LONG)WBEM_FLAG_FORWARD_ONLY |
                                                     (LONG)WBEM_FLAG_RETURN_IMMEDIATELY,
                                                 NULL,
                                                 Enumerator);
    }
    if (OperationId == ZP_WMI_OPERATION_ENUMERATE_CLASSES)
    {
        return IWbemServices_CreateClassEnum(Services,
                                              NULL,
                                              (LONG)WBEM_FLAG_DEEP | (LONG)WBEM_FLAG_FORWARD_ONLY |
                                                  (LONG)WBEM_FLAG_RETURN_IMMEDIATELY,
                                              NULL,
                                              Enumerator);
    }
    QueryLanguage = SysAllocString(L"WQL");
    Query = SysAllocStringLen((PCWCH)Request->Query.Buffer, Request->Query.Length);
    if (QueryLanguage == NULL || Query == NULL)
    {
        SysFreeString(Query);
        SysFreeString(QueryLanguage);
        return E_OUTOFMEMORY;
    }
    Result = IWbemServices_ExecQuery(Services,
                                     QueryLanguage,
                                     Query,
                                     WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                     NULL,
                                     Enumerator);
    SysFreeString(Query);
    SysFreeString(QueryLanguage);
    return Result;
}

static
HRESULT
ZpWmi_Enumerate(
    _In_ IEnumWbemClassObject* Enumerator,
    _In_ USHORT OperationId,
    _In_ PCZP_WMI_REQUEST_VIEW Request,
    _Inout_ PZP_WMI_BUILDER Builder)
{
    static const PCWSTR NamespaceProperties[] = { L"Name" };
    static const PCWSTR ClassProperties[] = { L"__CLASS", L"__SUPERCLASS" };
    HRESULT Result = S_OK;

    while (Builder->Count < Request->Limit)
    {
        IWbemClassObject* Object;
        ULONG Returned;

        Result = IEnumWbemClassObject_Next(Enumerator, 30000, 1, &Object, &Returned);
        if (Result == WBEM_S_FALSE)
        {
            Result = S_OK;
            break;
        }
        if (Result == WBEM_S_TIMEDOUT)
        {
            Result = WBEM_E_TIMED_OUT;
            break;
        }
        if (Result != S_OK || Returned == 0) break;
        Result = OperationId == ZP_WMI_OPERATION_ENUMERATE_NAMESPACES ?
                     ZpWmi_AddSelectedProperties(Builder,
                                                 Object,
                                                 NamespaceProperties,
                                                 ARRAYSIZE(NamespaceProperties)) :
                 OperationId == ZP_WMI_OPERATION_ENUMERATE_CLASSES ?
                     ZpWmi_AddSelectedProperties(Builder,
                                                 Object,
                                                 ClassProperties,
                                                 ARRAYSIZE(ClassProperties)) :
                     ZpWmi_AddObject(Builder, Object, Request->Flags);
        IWbemClassObject_Release(Object);
        if (FAILED(Result)) break;
    }
    return Result;
}

static
NTSTATUS
ZpWmi_EncodeBuilder(
    _In_ PZP_WMI_BUILDER Builder,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    NTSTATUS Status;

    Status = ZpWmi_EncodePage(Builder->Rows, Builder->Count, NULL, 0, ResponseLength);
    *Response = NT_SUCCESS(Status) ? Mem_Alloc(*ResponseLength) : NULL;
    if (!NT_SUCCESS(Status) || *Response == NULL)
    {
        return NT_SUCCESS(Status) ? STATUS_NO_MEMORY : Status;
    }
    Status = ZpWmi_EncodePage(Builder->Rows, Builder->Count, *Response, *ResponseLength, ResponseLength);
    if (!NT_SUCCESS(Status))
    {
        Mem_Free(*Response);
        *Response = NULL;
    }
    return Status;
}

static
ZP_STATUS
ZpWmi_Run(
    _In_ USHORT OperationId,
    _In_ PCZP_WMI_REQUEST_VIEW Request,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_WMI_BUILDER Builder = { 0 };
    IEnumWbemClassObject* Enumerator = NULL;
    IWbemServices* Services = NULL;
    LOGICAL Uninitialize;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Uninitialize = SUCCEEDED(Result);
    if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
    if (SUCCEEDED(Result))
    {
        Result = ZpWmi_Connect((PCWCH)Request->Namespace.Buffer, Request->Namespace.Length, &Services);
    }
    if (SUCCEEDED(Result)) Result = ZpWmi_CreateEnumerator(Services, OperationId, Request, &Enumerator);
    if (SUCCEEDED(Result))
    {
        Result = ZpWmi_Enumerate(Enumerator, OperationId, Request, &Builder);
        IEnumWbemClassObject_Release(Enumerator);
    }
    if (Services != NULL) IWbemServices_Release(Services);
    if (Result == S_OK) Status = ZpWmi_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpWmi_FreeBuilder(&Builder);
    if (Uninitialize) CoUninitialize();
    return !NT_SUCCESS(Status) ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusHResult, (ULONG)Result);
}

ZP_STATUS
ZpWmi_Execute(
    _In_ USHORT OperationId,
    _In_reads_bytes_opt_(RequestLength) const VOID* Request,
    _In_ ULONG RequestLength,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_WMI_REQUEST_VIEW View;
    NTSTATUS Status;

    if (OperationId < ZP_WMI_OPERATION_ENUMERATE_NAMESPACES || OperationId > ZP_WMI_OPERATION_QUERY)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Status = ZpWmi_DecodeRequest(Request, RequestLength, &View);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    if (!ZpWmi_IsNamespaceValid(&View.Namespace) || !ZpWmi_IsQueryValid(&View.Query) ||
        (OperationId == ZP_WMI_OPERATION_QUERY) != (View.Query.Length != 0) ||
        (OperationId == ZP_WMI_OPERATION_QUERY && View.Limit > ZP_WMI_MAX_QUERY_ROWS))
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    return ZpWmi_Run(OperationId, &View, Response, ResponseLength);
}
