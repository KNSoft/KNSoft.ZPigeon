#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>

#include "Json.h"

#include <roapi.h>
#include <winstring.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <memory>
#include <new>
#include <utility>

#pragma comment(lib, "runtimeobject.lib")

namespace Json = winrt::Windows::Data::Json;
namespace Collections = winrt::Windows::Foundation::Collections;

static_assert(static_cast<BYTE>(Json::JsonValueType::Null) == ZpJsonNull &&
              static_cast<BYTE>(Json::JsonValueType::Boolean) == ZpJsonBoolean &&
              static_cast<BYTE>(Json::JsonValueType::Number) == ZpJsonNumber &&
              static_cast<BYTE>(Json::JsonValueType::String) == ZpJsonString &&
              static_cast<BYTE>(Json::JsonValueType::Array) == ZpJsonArray &&
              static_cast<BYTE>(Json::JsonValueType::Object) == ZpJsonObject);

struct _ZP_JSON_VALUE
{
    explicit _ZP_JSON_VALUE(Json::IJsonValue&& value) noexcept : Value(std::move(value)) {}

    Json::IJsonValue Value;
};

struct _ZP_JSON_ITERATOR
{
    Json::JsonArray Array{ nullptr };
    Collections::IIterator<Collections::IKeyValuePair<winrt::hstring, Json::IJsonValue>> Object{ nullptr };
    ULONG Index = 0;
    ZP_JSON_TYPE Type = ZpJsonNull;
};

namespace
{
    class Apartment
    {
    public:
        Apartment() noexcept
        {
            Result = RoInitialize(RO_INIT_MULTITHREADED);
            Initialized = SUCCEEDED(Result);
            if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
        }

        ~Apartment()
        {
            if (Initialized) RoUninitialize();
        }

        HRESULT Result = E_FAIL;
        bool Initialized = false;
    };

    struct MemoryDeleter
    {
        void operator()(PVOID value) const noexcept
        {
            Mem_Free(value);
        }
    };

    NTSTATUS FromHResult(HRESULT result) noexcept
    {
        if (result == E_OUTOFMEMORY) return STATUS_NO_MEMORY;
        if (result == E_INVALIDARG || result == E_POINTER) return STATUS_INVALID_PARAMETER;
        if (HRESULT_FACILITY(result) == FACILITY_WIN32) return NTSTATUS_FROM_WIN32(HRESULT_CODE(result));
        return STATUS_UNSUCCESSFUL;
    }

    NTSTATUS EnsureApartment() noexcept
    {
        static thread_local Apartment apartment;

        return SUCCEEDED(apartment.Result) ? STATUS_SUCCESS : FromHResult(apartment.Result);
    }

    template<typename Operation>
    NTSTATUS Invoke(Operation&& operation) noexcept
    {
        NTSTATUS status = EnsureApartment();

        if (!NT_SUCCESS(status)) return status;
        try
        {
            return operation();
        }
        catch (const winrt::hresult_error& error)
        {
            return FromHResult(error.code());
        }
        catch (const std::bad_alloc&)
        {
            return STATUS_NO_MEMORY;
        }
        catch (...)
        {
            return STATUS_UNSUCCESSFUL;
        }
    }

    NTSTATUS CreateUtf8String(
        _In_reads_bytes_(length) const BYTE* text,
        _In_ ULONG length,
        _Out_ HSTRING* value) noexcept
    {
        HSTRING_BUFFER bufferHandle = nullptr;
        HSTRING string = nullptr;
        PWSTR buffer = nullptr;
        NTSTATUS status = STATUS_SUCCESS;
        HRESULT result;
        int characterCount;

        if (text == nullptr) return STATUS_INVALID_PARAMETER;
        if (length == 0) return STATUS_DATA_ERROR;
        if (length > INT_MAX) return STATUS_FILE_TOO_LARGE;
        characterCount = MultiByteToWideChar(CP_UTF8,
                                             MB_ERR_INVALID_CHARS,
                                             reinterpret_cast<PCCH>(text),
                                             static_cast<int>(length),
                                             nullptr,
                                             0);
        if (characterCount == 0)
        {
            DWORD error = GetLastError();

            return error != ERROR_SUCCESS ? NTSTATUS_FROM_WIN32(error) : STATUS_DATA_ERROR;
        }
        result = WindowsPreallocateStringBuffer(static_cast<UINT32>(characterCount),
                                                 &buffer,
                                                 &bufferHandle);
        if (FAILED(result)) return FromHResult(result);
        if (MultiByteToWideChar(CP_UTF8,
                                MB_ERR_INVALID_CHARS,
                                reinterpret_cast<PCCH>(text),
                                static_cast<int>(length),
                                buffer,
                                characterCount) == characterCount)
        {
            result = WindowsPromoteStringBuffer(bufferHandle, &string);
            if (FAILED(result)) status = FromHResult(result);
            else bufferHandle = nullptr;
        }
        else
        {
            DWORD error = GetLastError();

            status = error != ERROR_SUCCESS ? NTSTATUS_FROM_WIN32(error) : STATUS_DATA_ERROR;
        }
        if (bufferHandle != nullptr) WindowsDeleteStringBuffer(bufferHandle);
        if (NT_SUCCESS(status)) *value = string;
        return status;
    }

    NTSTATUS AllocateValue(
        _In_ Json::IJsonValue value,
        _Outptr_ PZP_JSON_VALUE* result) noexcept
    {
        std::unique_ptr<ZP_JSON_VALUE> allocation(new (std::nothrow) ZP_JSON_VALUE(std::move(value)));

        if (!allocation) return STATUS_NO_MEMORY;
        *result = allocation.release();
        return STATUS_SUCCESS;
    }

    NTSTATUS ParseString(
        _In_ HSTRING text,
        _Outptr_ PZP_JSON_VALUE* value) noexcept
    {
        return Invoke([&]() -> NTSTATUS
        {
            winrt::hstring input{ text, winrt::take_ownership_from_abi };
            Json::JsonValue parsed{ nullptr };

            if (!Json::JsonValue::TryParse(input, parsed)) return STATUS_DATA_ERROR;
            return AllocateValue(parsed, value);
        });
    }

    NTSTATUS QueryType(
        _In_ PZP_JSON_VALUE value,
        _Out_ PZP_JSON_TYPE type)
    {
        *type = static_cast<ZP_JSON_TYPE>(value->Value.ValueType());
        return STATUS_SUCCESS;
    }
}

Json::IJsonValue
ZpJson::Parse(
    std::wstring_view Text)
{
    NTSTATUS status = EnsureApartment();

    if (!NT_SUCCESS(status)) winrt::throw_hresult(HRESULT_FROM_NT(status));
    if (Text.size() > MAXULONG) winrt::throw_hresult(HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE));
    return Json::JsonValue::Parse(winrt::hstring(Text));
}

NTSTATUS
ZpJson_ParseUtf8(
    _In_reads_bytes_(Length) const BYTE* Text,
    _In_ ULONG Length,
    _Outptr_ PZP_JSON_VALUE* Value)
{
    HSTRING string = nullptr;
    NTSTATUS status;

    if (Value == nullptr) return STATUS_INVALID_PARAMETER;
    status = EnsureApartment();
    if (!NT_SUCCESS(status)) return status;
    status = CreateUtf8String(Text, Length, &string);
    return NT_SUCCESS(status) ? ParseString(string, Value) : status;
}

NTSTATUS
ZpJson_ParseUtf8File(
    _In_ PCWSTR Path,
    _In_ ULONG MaximumSize,
    _Outptr_ PZP_JSON_VALUE* Value)
{
    std::unique_ptr<VOID, MemoryDeleter> buffer;
    ULONGLONG fileSize;
    HSTRING string = nullptr;
    ULONG bytesRead;
    HANDLE file;
    NTSTATUS status;

    if (Path == nullptr || MaximumSize == 0 || Value == nullptr) return STATUS_INVALID_PARAMETER;
    status = EnsureApartment();
    if (!NT_SUCCESS(status)) return status;
    status = IO_OpenWin32File(&file,
                              Path,
                              nullptr,
                              FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE);
    if (!NT_SUCCESS(status)) return status;
    status = IO_GetFileSize(file, &fileSize);
    if (NT_SUCCESS(status) && fileSize == 0) status = STATUS_DATA_ERROR;
    if (NT_SUCCESS(status) && fileSize > MaximumSize) status = STATUS_FILE_TOO_LARGE;
    if (NT_SUCCESS(status))
    {
        buffer.reset(Mem_Alloc(static_cast<SIZE_T>(fileSize)));
        if (!buffer) status = STATUS_NO_MEMORY;
    }
    if (NT_SUCCESS(status))
    {
        status = IO_ReadFile(file,
                             nullptr,
                             buffer.get(),
                             static_cast<ULONG>(fileSize),
                             &bytesRead);
    }
    NtClose(file);
    if (!NT_SUCCESS(status)) return status;
    if (bytesRead == 0) return STATUS_DATA_ERROR;
    status = CreateUtf8String(static_cast<const BYTE*>(buffer.get()), bytesRead, &string);
    return NT_SUCCESS(status) ? ParseString(string, Value) : status;
}

VOID
ZpJson_CloseValue(
    _In_opt_ PZP_JSON_VALUE Value)
{
    if (Value == nullptr) return;
    (void)EnsureApartment();
    delete Value;
}

NTSTATUS
ZpJson_GetType(
    _In_ PZP_JSON_VALUE Value,
    _Out_ PZP_JSON_TYPE Type)
{
    if (Value == nullptr || Type == nullptr) return STATUS_INVALID_PARAMETER;
    return Invoke([&]() -> NTSTATUS
    {
        return QueryType(Value, Type);
    });
}

NTSTATUS
ZpJson_GetSize(
    _In_ PZP_JSON_VALUE Value,
    _Out_ PULONG Size)
{
    if (Value == nullptr || Size == nullptr) return STATUS_INVALID_PARAMETER;
    return Invoke([&]() -> NTSTATUS
    {
        ZP_JSON_TYPE type;
        NTSTATUS status = QueryType(Value, &type);

        if (!NT_SUCCESS(status)) return status;
        if (type == ZpJsonArray) *Size = Value->Value.GetArray().Size();
        else if (type == ZpJsonObject) *Size = Value->Value.GetObject().Size();
        else return STATUS_OBJECT_TYPE_MISMATCH;
        return STATUS_SUCCESS;
    });
}

NTSTATUS
ZpJson_GetNamedValue(
    _In_ PZP_JSON_VALUE Value,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _Outptr_ PZP_JSON_VALUE* NamedValue)
{
    if (Value == nullptr || Name == nullptr || NamedValue == nullptr) return STATUS_INVALID_PARAMETER;
    return Invoke([&]() -> NTSTATUS
    {
        ZP_JSON_TYPE type;
        NTSTATUS status = QueryType(Value, &type);

        if (!NT_SUCCESS(status)) return status;
        if (type != ZpJsonObject) return STATUS_OBJECT_TYPE_MISMATCH;
        Json::JsonObject object = Value->Value.GetObject();
        winrt::hstring name(Name, NameLength);

        if (!object.HasKey(name)) return STATUS_NOT_FOUND;
        return AllocateValue(object.Lookup(name), NamedValue);
    });
}

NTSTATUS
ZpJson_CreateIterator(
    _In_ PZP_JSON_VALUE Value,
    _In_ ULONG StartIndex,
    _Outptr_ PZP_JSON_ITERATOR* Iterator)
{
    if (Value == nullptr || Iterator == nullptr) return STATUS_INVALID_PARAMETER;
    return Invoke([&]() -> NTSTATUS
    {
        ZP_JSON_TYPE type;
        NTSTATUS status = QueryType(Value, &type);
        std::unique_ptr<ZP_JSON_ITERATOR> iterator;
        ULONG index;

        if (!NT_SUCCESS(status)) return status;
        if (type != ZpJsonArray && type != ZpJsonObject) return STATUS_OBJECT_TYPE_MISMATCH;
        iterator.reset(new (std::nothrow) ZP_JSON_ITERATOR());
        if (!iterator) return STATUS_NO_MEMORY;
        iterator->Type = type;
        if (type == ZpJsonArray)
        {
            iterator->Array = Value->Value.GetArray();
            if (StartIndex > iterator->Array.Size()) return STATUS_INVALID_PARAMETER;
            iterator->Index = StartIndex;
        }
        else
        {
            Json::JsonObject object = Value->Value.GetObject();

            if (StartIndex > object.Size()) return STATUS_INVALID_PARAMETER;
            iterator->Object = object.First();
            for (index = 0; index < StartIndex; index++) iterator->Object.MoveNext();
        }
        *Iterator = iterator.release();
        return STATUS_SUCCESS;
    });
}

NTSTATUS
ZpJson_IteratorNext(
    _Inout_ PZP_JSON_ITERATOR Iterator,
    _Out_ HSTRING* Name,
    _Outptr_ PZP_JSON_VALUE* Value)
{
    if (Iterator == nullptr || Name == nullptr || Value == nullptr)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return Invoke([&]() -> NTSTATUS
    {
        if (Iterator->Type == ZpJsonArray)
        {
            PZP_JSON_VALUE value = nullptr;
            NTSTATUS status;

            if (Iterator->Index == Iterator->Array.Size()) return STATUS_NO_MORE_ENTRIES;
            status = AllocateValue(Iterator->Array.GetAt(Iterator->Index), &value);
            if (!NT_SUCCESS(status)) return status;
            Iterator->Index++;
            *Name = nullptr;
            *Value = value;
            return STATUS_SUCCESS;
        }
        if (!Iterator->Object.HasCurrent()) return STATUS_NO_MORE_ENTRIES;
        auto current = Iterator->Object.Current();
        winrt::hstring name = current.Key();
        std::unique_ptr<ZP_JSON_VALUE> value(
            new (std::nothrow) ZP_JSON_VALUE(current.Value()));
        if (!value) return STATUS_NO_MEMORY;
        Iterator->Object.MoveNext();
        *Name = static_cast<HSTRING>(winrt::detach_abi(name));
        *Value = value.release();
        return STATUS_SUCCESS;
    });
}

VOID
ZpJson_CloseIterator(
    _In_opt_ PZP_JSON_ITERATOR Iterator)
{
    if (Iterator == nullptr) return;
    (void)EnsureApartment();
    delete Iterator;
}

NTSTATUS
ZpJson_GetString(
    _In_ PZP_JSON_VALUE Value,
    _Out_ HSTRING* Text)
{
    if (Value == nullptr || Text == nullptr) return STATUS_INVALID_PARAMETER;
    return Invoke([&]() -> NTSTATUS
    {
        ZP_JSON_TYPE type;
        NTSTATUS status = QueryType(Value, &type);

        if (!NT_SUCCESS(status)) return status;
        if (type != ZpJsonString) return STATUS_OBJECT_TYPE_MISMATCH;
        *Text = static_cast<HSTRING>(winrt::detach_abi(Value->Value.GetString()));
        return STATUS_SUCCESS;
    });
}

NTSTATUS
ZpJson_Stringify(
    _In_ PZP_JSON_VALUE Value,
    _Out_ HSTRING* Text)
{
    if (Value == nullptr || Text == nullptr) return STATUS_INVALID_PARAMETER;
    return Invoke([&]() -> NTSTATUS
    {
        *Text = static_cast<HSTRING>(winrt::detach_abi(Value->Value.Stringify()));
        return STATUS_SUCCESS;
    });
}
