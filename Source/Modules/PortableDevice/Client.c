#define COBJMACROS

#include "Client.h"

#include "../../KNSoft.ZPigeon.Client.SDK/Client.inl"
#include "../../KNSoft.ZPigeon.Client.SDK/Core/Channel.h"
#include "../../KNSoft.ZPigeon.Protocol/Include/KNSoft/ZPigeon/File.h"
#include <KNSoft/MakeLifeEasier/MakeLifeEasier.h>
#include <PortableDeviceApi.h>
#include <PortableDevice.h>
#include <propvarutil.h>

#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "PortableDeviceGuids.lib")

#define ZpPortable_StatusFromHResult(Result) ZpStatus_FromCode(ZpStatusHResult, (ULONG)(Result))
#define ZP_PORTABLE_CHANNEL_CHUNK_SIZE 0x00010000UL
#define ZP_PORTABLE_WRITE_WINDOW_SIZE 0x00100000UL

typedef enum _ZP_PORTABLE_MANAGER_STRING
{
    ZpPortableManagerFriendlyName,
    ZpPortableManagerManufacturer,
    ZpPortableManagerDescription
} ZP_PORTABLE_MANAGER_STRING;

typedef struct _ZP_PORTABLE_DEVICE_LOCAL
{
    ZP_PORTABLE_DEVICE_RECORD Record;
    PWSTR Id;
    PWSTR Name;
    PWSTR Manufacturer;
    PWSTR Model;
} ZP_PORTABLE_DEVICE_LOCAL, *PZP_PORTABLE_DEVICE_LOCAL;

typedef struct _ZP_PORTABLE_OBJECT_LOCAL
{
    ZP_PORTABLE_OBJECT_RECORD Record;
    PWSTR Id;
    PWSTR PersistentId;
    PWSTR Name;
} ZP_PORTABLE_OBJECT_LOCAL, *PZP_PORTABLE_OBJECT_LOCAL;

typedef enum _ZP_CLIENT_PORTABLE_CHANNEL_TYPE
{
    ZpClientPortableChannelRead,
    ZpClientPortableChannelWrite
} ZP_CLIENT_PORTABLE_CHANNEL_TYPE;

struct _ZP_CLIENT_PORTABLE_CHANNEL
{
    ZP_CLIENT_LOCAL_CHANNEL Header;
    LOGICAL WorkerActive;
    ZP_CLIENT_PORTABLE_CHANNEL_TYPE Type;
    ULONGLONG Credit;
    ULONGLONG ReceiveCredit;
    ULONGLONG RemainingBytes;
    IPortableDevice* Device;
    IPortableDeviceContent* Content;
    IStream* Stream;
};

static NTSTATUS ZpPortable_ChannelData(_Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
                                       _In_ const ZP_CHANNEL_DATA_VIEW* Message);
static NTSTATUS ZpPortable_ChannelWindow(_Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
                                         _In_ ULONG CreditBytes);
static NTSTATUS ZpPortable_ChannelClose(_Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
                                        _In_ ZP_STATUS Status);
static VOID ZpPortable_ChannelAbort(_Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
                                    _In_ ZP_STATUS Status);
static VOID ZpPortable_ChannelDestroy(_Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel);

static
PWSTR
ZpPortable_CopyView(
    _In_ PCZP_STRING_VIEW View)
{
    PWSTR String;

    String = Mem_Alloc(((SIZE_T)View->Length + 1) * sizeof(WCHAR));
    if (String == NULL) return NULL;
    RtlCopyMemory(String, View->Buffer, (SIZE_T)View->Length * sizeof(WCHAR));
    String[View->Length] = UNICODE_NULL;
    return String;
}

static
HRESULT
ZpPortable_GetManagerString(
    _In_ IPortableDeviceManager* Manager,
    _In_ PCWSTR DeviceId,
    _In_ ZP_PORTABLE_MANAGER_STRING Type,
    _Outptr_result_z_ PWSTR* String)
{
    PWSTR Buffer;
    DWORD Length = 0;
    HRESULT Result;

    switch (Type)
    {
        case ZpPortableManagerFriendlyName:
            Result = IPortableDeviceManager_GetDeviceFriendlyName(Manager, DeviceId, NULL, &Length);
            break;
        case ZpPortableManagerManufacturer:
            Result = IPortableDeviceManager_GetDeviceManufacturer(Manager, DeviceId, NULL, &Length);
            break;
        default:
            Result = IPortableDeviceManager_GetDeviceDescription(Manager, DeviceId, NULL, &Length);
            break;
    }
    if (Result != HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) && FAILED(Result)) return Result;
    if (Length == 0 || Length > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH + 1) return HRESULT_FROM_WIN32(ERROR_BAD_LENGTH);
    Buffer = Mem_Alloc((SIZE_T)Length * sizeof(WCHAR));
    if (Buffer == NULL) return E_OUTOFMEMORY;
    switch (Type)
    {
        case ZpPortableManagerFriendlyName:
            Result = IPortableDeviceManager_GetDeviceFriendlyName(Manager, DeviceId, Buffer, &Length);
            break;
        case ZpPortableManagerManufacturer:
            Result = IPortableDeviceManager_GetDeviceManufacturer(Manager, DeviceId, Buffer, &Length);
            break;
        default:
            Result = IPortableDeviceManager_GetDeviceDescription(Manager, DeviceId, Buffer, &Length);
            break;
    }
    if (FAILED(Result))
    {
        Mem_Free(Buffer);
        return Result;
    }
    *String = Buffer;
    return S_OK;
}

static
VOID
ZpPortable_FreeDevices(
    _Inout_updates_(Count) PZP_PORTABLE_DEVICE_LOCAL Devices,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Mem_Free(Devices[Index].Id);
        Mem_Free(Devices[Index].Name);
        Mem_Free(Devices[Index].Manufacturer);
        Mem_Free(Devices[Index].Model);
    }
    Mem_Free(Devices);
}

static
HRESULT
ZpPortable_EnumerateDevices(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_PORTABLE_DEVICE_RECORD Records[ZP_PORTABLE_DEVICE_MAX_DEVICES];
    IPortableDeviceManager* Manager = NULL;
    PZP_PORTABLE_DEVICE_LOCAL Devices = NULL;
    PWSTR* Ids = NULL;
    DWORD Count = 0, Index, Actual = 0;
    ULONG Length;
    HRESULT Result;
    NTSTATUS Status;

    Result = CoCreateInstance(&CLSID_PortableDeviceManager,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IPortableDeviceManager,
                              (PVOID*)&Manager);
    if (SUCCEEDED(Result)) Result = IPortableDeviceManager_GetDevices(Manager, NULL, &Count);
    if (SUCCEEDED(Result) && Count > ZP_PORTABLE_DEVICE_MAX_DEVICES) Count = ZP_PORTABLE_DEVICE_MAX_DEVICES;
    if (SUCCEEDED(Result) && Count != 0)
    {
        Ids = Mem_Alloc((SIZE_T)Count * sizeof(*Ids));
        Devices = Mem_Alloc((SIZE_T)Count * sizeof(*Devices));
        if (Ids == NULL || Devices == NULL) Result = E_OUTOFMEMORY;
        else
        {
            RtlZeroMemory(Ids, (SIZE_T)Count * sizeof(*Ids));
            RtlZeroMemory(Devices, (SIZE_T)Count * sizeof(*Devices));
        }
    }
    if (SUCCEEDED(Result))
    {
        for (Index = 0; Index < Count; Index++)
        {
            Ids[Index] = Mem_Alloc((ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH + 1) * sizeof(WCHAR));
            if (Ids[Index] == NULL)
            {
                Result = E_OUTOFMEMORY;
                break;
            }
        }
    }
    if (SUCCEEDED(Result)) Result = IPortableDeviceManager_GetDevices(Manager, Ids, &Count);
    for (Index = 0; SUCCEEDED(Result) && Index < Count; Index++)
    {
        PZP_PORTABLE_DEVICE_LOCAL Device = &Devices[Actual];

        Length = (ULONG)wcsnlen(Ids[Index], ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH + 1);
        if (Length == 0 || Length > ZP_PORTABLE_DEVICE_MAX_STRING_LENGTH)
        {
            Result = HRESULT_FROM_WIN32(ERROR_BAD_LENGTH);
            break;
        }
        Device->Id = Ids[Index];
        Ids[Index] = NULL;
        Result = ZpPortable_GetManagerString(Manager, Device->Id, ZpPortableManagerFriendlyName, &Device->Name);
        if (FAILED(Result)) Device->Name = NULL;
        Result = ZpPortable_GetManagerString(Manager,
                                             Device->Id,
                                             ZpPortableManagerManufacturer,
                                             &Device->Manufacturer);
        if (FAILED(Result)) Device->Manufacturer = NULL;
        Result = ZpPortable_GetManagerString(Manager, Device->Id, ZpPortableManagerDescription, &Device->Model);
        if (FAILED(Result)) Device->Model = NULL;
        Result = S_OK;
        Device->Record.Id = Device->Id;
        Device->Record.IdLength = Length;
        Device->Record.Name = Device->Name;
        Device->Record.NameLength = Device->Name != NULL ? (ULONG)wcslen(Device->Name) : 0;
        Device->Record.Manufacturer = Device->Manufacturer;
        Device->Record.ManufacturerLength = Device->Manufacturer != NULL ?
                                                (ULONG)wcslen(Device->Manufacturer) : 0;
        Device->Record.Model = Device->Model;
        Device->Record.ModelLength = Device->Model != NULL ? (ULONG)wcslen(Device->Model) : 0;
        Actual++;
    }
    if (SUCCEEDED(Result))
    {
        for (Index = 0; Index < Actual; Index++) Records[Index] = Devices[Index].Record;
        Status = ZpPortable_EncodeDeviceList(Actual != 0 ? Records : NULL,
                                             Actual,
                                             NULL,
                                             0,
                                             &Length);
        *Response = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
        if (NT_SUCCESS(Status) && *Response == NULL) Status = STATUS_NO_MEMORY;
        if (NT_SUCCESS(Status))
        {
            Status = ZpPortable_EncodeDeviceList(Records, Actual, *Response, Length, ResponseLength);
        }
        Result = NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
    }
    if (Ids != NULL)
    {
        for (Index = 0; Index < Count; Index++) Mem_Free(Ids[Index]);
        Mem_Free(Ids);
    }
    ZpPortable_FreeDevices(Devices, Actual);
    if (Manager != NULL) IPortableDeviceManager_Release(Manager);
    return Result;
}

static
HRESULT
ZpPortable_Open(
    _In_ PCWSTR DeviceId,
    _Outptr_ IPortableDevice** Device,
    _Outptr_ IPortableDeviceContent** Content)
{
    IPortableDeviceValues* Information = NULL;
    IPortableDevice* Object = NULL;
    HRESULT Result;

    Result = CoCreateInstance(&CLSID_PortableDeviceValues,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IPortableDeviceValues,
                              (PVOID*)&Information);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetStringValue(Information, &WPD_CLIENT_NAME, L"ZPigeon");
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetUnsignedIntegerValue(Information,
                                                                                  &WPD_CLIENT_MAJOR_VERSION,
                                                                                  1);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetUnsignedIntegerValue(Information,
                                                                                  &WPD_CLIENT_MINOR_VERSION,
                                                                                  0);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetUnsignedIntegerValue(Information,
                                                                                  &WPD_CLIENT_REVISION,
                                                                                  0);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetUnsignedIntegerValue(
        Information,
        &WPD_CLIENT_SECURITY_QUALITY_OF_SERVICE,
        SECURITY_IMPERSONATION);
    if (SUCCEEDED(Result)) Result = CoCreateInstance(&CLSID_PortableDeviceFTM,
                                                      NULL,
                                                      CLSCTX_INPROC_SERVER,
                                                      &IID_IPortableDevice,
                                                      (PVOID*)&Object);
    if (SUCCEEDED(Result)) Result = IPortableDevice_Open(Object, DeviceId, Information);
    if (SUCCEEDED(Result)) Result = IPortableDevice_Content(Object, Content);
    if (Information != NULL) IPortableDeviceValues_Release(Information);
    if (FAILED(Result))
    {
        if (Object != NULL) IPortableDevice_Release(Object);
        return Result;
    }
    *Device = Object;
    return S_OK;
}

static
HRESULT
ZpPortable_CreateKeys(
    _Outptr_ IPortableDeviceKeyCollection** Keys)
{
    static const PROPERTYKEY* Properties[] = {
        &WPD_OBJECT_NAME,
        &WPD_OBJECT_ORIGINAL_FILE_NAME,
        &WPD_OBJECT_PERSISTENT_UNIQUE_ID,
        &WPD_OBJECT_CONTENT_TYPE,
        &WPD_OBJECT_SIZE,
        &WPD_OBJECT_DATE_MODIFIED,
        &WPD_OBJECT_CAN_DELETE,
        &WPD_FUNCTIONAL_OBJECT_CATEGORY,
        &WPD_STORAGE_CAPACITY,
        &WPD_STORAGE_FREE_SPACE_IN_BYTES
    };
    IPortableDeviceKeyCollection* Collection = NULL;
    ULONG Index;
    HRESULT Result;

    Result = CoCreateInstance(&CLSID_PortableDeviceKeyCollection,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IPortableDeviceKeyCollection,
                              (PVOID*)&Collection);
    for (Index = 0; SUCCEEDED(Result) && Index < ARRAYSIZE(Properties); Index++)
    {
        Result = IPortableDeviceKeyCollection_Add(Collection, Properties[Index]);
    }
    if (FAILED(Result))
    {
        if (Collection != NULL) IPortableDeviceKeyCollection_Release(Collection);
        return Result;
    }
    *Keys = Collection;
    return S_OK;
}

static
VOID
ZpPortable_FreeObjects(
    _Inout_updates_(Count) PZP_PORTABLE_OBJECT_LOCAL Objects,
    _In_ ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        CoTaskMemFree(Objects[Index].Id);
        CoTaskMemFree(Objects[Index].PersistentId);
        CoTaskMemFree(Objects[Index].Name);
    }
}

static
VOID
ZpPortable_SetDate(
    _In_ IPortableDeviceValues* Values,
    _Out_ PULONGLONG Time)
{
    PROPVARIANT Value;
    SYSTEMTIME SystemTime;
    FILETIME FileTime;

    PropVariantInit(&Value);
    if (SUCCEEDED(IPortableDeviceValues_GetValue(Values, &WPD_OBJECT_DATE_MODIFIED, &Value)) &&
        Value.vt == VT_DATE && VariantTimeToSystemTime(Value.date, &SystemTime) &&
        SystemTimeToFileTime(&SystemTime, &FileTime))
    {
        *Time = ((ULONGLONG)FileTime.dwHighDateTime << 32) | FileTime.dwLowDateTime;
    }
    PropVariantClear(&Value);
}

static
HRESULT
ZpPortable_SetObject(
    _In_ IPortableDeviceProperties* Properties,
    _In_ IPortableDeviceKeyCollection* Keys,
    _Inout_ PZP_PORTABLE_OBJECT_LOCAL Object)
{
    IPortableDeviceValues* Values = NULL;
    PWSTR OriginalName = NULL;
    GUID ContentType, Category;
    BOOL CanDelete;
    HRESULT Result;

    Result = IPortableDeviceProperties_GetValues(Properties, Object->Id, Keys, &Values);
    if (FAILED(Result)) return Result;
    IPortableDeviceValues_GetStringValue(Values, &WPD_OBJECT_PERSISTENT_UNIQUE_ID, &Object->PersistentId);
    IPortableDeviceValues_GetStringValue(Values, &WPD_OBJECT_ORIGINAL_FILE_NAME, &OriginalName);
    if (OriginalName != NULL) Object->Name = OriginalName;
    else IPortableDeviceValues_GetStringValue(Values, &WPD_OBJECT_NAME, &Object->Name);
    IPortableDeviceValues_GetUnsignedLargeIntegerValue(Values, &WPD_OBJECT_SIZE, &Object->Record.Size);
    IPortableDeviceValues_GetUnsignedLargeIntegerValue(Values, &WPD_STORAGE_CAPACITY, &Object->Record.Capacity);
    IPortableDeviceValues_GetUnsignedLargeIntegerValue(Values, &WPD_STORAGE_FREE_SPACE_IN_BYTES,
                                                        &Object->Record.FreeSpace);
    ZpPortable_SetDate(Values, &Object->Record.ModifiedTime);
    if (SUCCEEDED(IPortableDeviceValues_GetGuidValue(Values, &WPD_OBJECT_CONTENT_TYPE, &ContentType)) &&
        (IsEqualGUID(&ContentType, &WPD_CONTENT_TYPE_FOLDER) ||
         IsEqualGUID(&ContentType, &WPD_CONTENT_TYPE_FUNCTIONAL_OBJECT)))
    {
        Object->Record.Flags |= ZP_PORTABLE_OBJECT_FOLDER;
    }
    if (SUCCEEDED(IPortableDeviceValues_GetGuidValue(Values, &WPD_FUNCTIONAL_OBJECT_CATEGORY, &Category)) &&
        IsEqualGUID(&Category, &WPD_FUNCTIONAL_CATEGORY_STORAGE))
    {
        Object->Record.Flags |= ZP_PORTABLE_OBJECT_STORAGE;
    }
    if (SUCCEEDED(IPortableDeviceValues_GetBoolValue(Values, &WPD_OBJECT_CAN_DELETE, &CanDelete)) && CanDelete)
    {
        Object->Record.Flags |= ZP_PORTABLE_OBJECT_CAN_DELETE;
    }
    Object->Record.Id = Object->Id;
    Object->Record.IdLength = (ULONG)wcslen(Object->Id);
    Object->Record.PersistentId = Object->PersistentId;
    Object->Record.PersistentIdLength = Object->PersistentId != NULL ? (ULONG)wcslen(Object->PersistentId) : 0;
    Object->Record.Name = Object->Name;
    Object->Record.NameLength = Object->Name != NULL ? (ULONG)wcslen(Object->Name) : 0;
    IPortableDeviceValues_Release(Values);
    return S_OK;
}

static
HRESULT
ZpPortable_EnumerateObjects(
    _In_ PCZP_PORTABLE_OBJECT_PAGE_REQUEST_VIEW Request,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_PORTABLE_OBJECT_RECORD Records[ZP_PORTABLE_DEVICE_PAGE_COUNT];
    PZP_PORTABLE_OBJECT_LOCAL Objects = NULL;
    IPortableDevice* Device = NULL;
    IPortableDeviceContent* Content = NULL;
    IPortableDeviceProperties* Properties = NULL;
    IPortableDeviceKeyCollection* Keys = NULL;
    IEnumPortableDeviceObjectIDs* Enumerator = NULL;
    PWSTR DeviceId = NULL, ParentId = NULL;
    PCWSTR Parent;
    ULONG Count = 0, Length, Index, Fetched, NextOffset;
    HRESULT Result;
    NTSTATUS Status;

    DeviceId = ZpPortable_CopyView(&Request->DeviceId);
    if (Request->ParentId.Length != 0) ParentId = ZpPortable_CopyView(&Request->ParentId);
    if (DeviceId == NULL || (Request->ParentId.Length != 0 && ParentId == NULL))
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    Parent = ParentId != NULL ? ParentId : WPD_DEVICE_OBJECT_ID;
    Result = ZpPortable_Open(DeviceId, &Device, &Content);
    if (SUCCEEDED(Result)) Result = IPortableDeviceContent_EnumObjects(Content, 0, Parent, NULL, &Enumerator);
    if (SUCCEEDED(Result) && Request->Offset != 0) Result = IEnumPortableDeviceObjectIDs_Skip(Enumerator,
                                                                                             Request->Offset);
    Objects = SUCCEEDED(Result) ? Mem_Alloc(sizeof(*Objects) * ZP_PORTABLE_DEVICE_PAGE_COUNT) : NULL;
    if (SUCCEEDED(Result) && Objects == NULL) Result = E_OUTOFMEMORY;
    if (SUCCEEDED(Result))
    {
        RtlZeroMemory(Objects, sizeof(*Objects) * ZP_PORTABLE_DEVICE_PAGE_COUNT);
        Result = ZpPortable_CreateKeys(&Keys);
    }
    if (SUCCEEDED(Result)) Result = IPortableDeviceContent_Properties(Content, &Properties);
    while (SUCCEEDED(Result) && Count < ZP_PORTABLE_DEVICE_PAGE_COUNT)
    {
        Result = IEnumPortableDeviceObjectIDs_Next(Enumerator, 1, &Objects[Count].Id, &Fetched);
        if (Result == S_FALSE || Fetched == 0)
        {
            Result = S_FALSE;
            break;
        }
        if (FAILED(Result)) break;
        Result = ZpPortable_SetObject(Properties, Keys, &Objects[Count]);
        if (FAILED(Result)) break;
        Count++;
    }
    NextOffset = Count == ZP_PORTABLE_DEVICE_PAGE_COUNT ? Request->Offset + Count : 0;
    if (SUCCEEDED(Result))
    {
        for (Index = 0; Index < Count; Index++) Records[Index] = Objects[Index].Record;
        Status = ZpPortable_EncodeObjectPage(Count != 0 ? Records : NULL,
                                             Count,
                                             NextOffset,
                                             NULL,
                                             0,
                                             &Length);
        *Response = NT_SUCCESS(Status) ? Mem_Alloc(Length) : NULL;
        if (NT_SUCCESS(Status) && *Response == NULL) Status = STATUS_NO_MEMORY;
        if (NT_SUCCESS(Status))
        {
            Status = ZpPortable_EncodeObjectPage(Records,
                                                  Count,
                                                  NextOffset,
                                                  *Response,
                                                  Length,
                                                  ResponseLength);
        }
        Result = NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
    }
Cleanup:
    if (Objects != NULL)
    {
        ZpPortable_FreeObjects(Objects, Count + (Count < ZP_PORTABLE_DEVICE_PAGE_COUNT &&
                                                Objects[Count].Id != NULL ? 1 : 0));
        Mem_Free(Objects);
    }
    if (Enumerator != NULL) IEnumPortableDeviceObjectIDs_Release(Enumerator);
    if (Keys != NULL) IPortableDeviceKeyCollection_Release(Keys);
    if (Properties != NULL) IPortableDeviceProperties_Release(Properties);
    if (Content != NULL) IPortableDeviceContent_Release(Content);
    if (Device != NULL) IPortableDevice_Release(Device);
    Mem_Free(ParentId);
    Mem_Free(DeviceId);
    return Result;
}

static
HRESULT
ZpPortable_CreateValues(
    _Outptr_ IPortableDeviceValues** Values)
{
    return CoCreateInstance(&CLSID_PortableDeviceValues,
                            NULL,
                            CLSCTX_INPROC_SERVER,
                            &IID_IPortableDeviceValues,
                            (PVOID*)Values);
}

static
HRESULT
ZpPortable_CreateFolder(
    _In_ PCZP_PORTABLE_NAME_REQUEST_VIEW Request)
{
    IPortableDevice* Device = NULL;
    IPortableDeviceContent* Content = NULL;
    IPortableDeviceValues* Values = NULL;
    PWSTR DeviceId = NULL, ParentId = NULL, Name = NULL, ObjectId = NULL;
    HRESULT Result;

    DeviceId = ZpPortable_CopyView(&Request->DeviceId);
    ParentId = ZpPortable_CopyView(&Request->ObjectId);
    Name = ZpPortable_CopyView(&Request->Name);
    if (DeviceId == NULL || ParentId == NULL || Name == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    Result = ZpPortable_Open(DeviceId, &Device, &Content);
    if (SUCCEEDED(Result)) Result = ZpPortable_CreateValues(&Values);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_PARENT_ID, ParentId);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_NAME, Name);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetGuidValue(Values,
                                                                       &WPD_OBJECT_CONTENT_TYPE,
                                                                       &WPD_CONTENT_TYPE_FOLDER);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetGuidValue(Values,
                                                                       &WPD_OBJECT_FORMAT,
                                                                       &WPD_OBJECT_FORMAT_PROPERTIES_ONLY);
    if (SUCCEEDED(Result)) Result = IPortableDeviceContent_CreateObjectWithPropertiesOnly(Content,
                                                                                           Values,
                                                                                           &ObjectId);
Cleanup:
    CoTaskMemFree(ObjectId);
    if (Values != NULL) IPortableDeviceValues_Release(Values);
    if (Content != NULL) IPortableDeviceContent_Release(Content);
    if (Device != NULL) IPortableDevice_Release(Device);
    Mem_Free(Name);
    Mem_Free(ParentId);
    Mem_Free(DeviceId);
    return Result;
}

static
HRESULT
ZpPortable_Delete(
    _In_ PCZP_PORTABLE_OBJECT_REQUEST_VIEW Request)
{
    IPortableDevice* Device = NULL;
    IPortableDeviceContent* Content = NULL;
    IPortableDevicePropVariantCollection* Objects = NULL;
    IPortableDevicePropVariantCollection* Results = NULL;
    PWSTR DeviceId = NULL, ObjectId = NULL;
    PROPVARIANT Value, DeleteResult;
    HRESULT Result;

    PropVariantInit(&DeleteResult);
    DeviceId = ZpPortable_CopyView(&Request->DeviceId);
    ObjectId = ZpPortable_CopyView(&Request->ObjectId);
    if (DeviceId == NULL || ObjectId == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    Result = ZpPortable_Open(DeviceId, &Device, &Content);
    if (SUCCEEDED(Result)) Result = CoCreateInstance(&CLSID_PortableDevicePropVariantCollection,
                                                      NULL,
                                                      CLSCTX_INPROC_SERVER,
                                                      &IID_IPortableDevicePropVariantCollection,
                                                      (PVOID*)&Objects);
    PropVariantInit(&Value);
    Value.vt = VT_LPWSTR;
    Value.pwszVal = ObjectId;
    if (SUCCEEDED(Result)) Result = IPortableDevicePropVariantCollection_Add(Objects, &Value);
    if (SUCCEEDED(Result)) Result = IPortableDeviceContent_Delete(Content,
                                                                  PORTABLE_DEVICE_DELETE_WITH_RECURSION,
                                                                  Objects,
                                                                  &Results);
    if (SUCCEEDED(Result) && Results != NULL)
    {
        Result = IPortableDevicePropVariantCollection_GetAt(Results, 0, &DeleteResult);
        if (SUCCEEDED(Result) && DeleteResult.vt == VT_ERROR) Result = DeleteResult.scode;
    }
Cleanup:
    PropVariantClear(&DeleteResult);
    if (Results != NULL) IPortableDevicePropVariantCollection_Release(Results);
    if (Objects != NULL) IPortableDevicePropVariantCollection_Release(Objects);
    if (Content != NULL) IPortableDeviceContent_Release(Content);
    if (Device != NULL) IPortableDevice_Release(Device);
    Mem_Free(ObjectId);
    Mem_Free(DeviceId);
    return Result;
}

static
HRESULT
ZpPortable_Rename(
    _In_ PCZP_PORTABLE_NAME_REQUEST_VIEW Request)
{
    IPortableDevice* Device = NULL;
    IPortableDeviceContent* Content = NULL;
    IPortableDeviceProperties* Properties = NULL;
    IPortableDeviceValues* Values = NULL;
    IPortableDeviceValues* Results = NULL;
    PWSTR DeviceId = NULL, ObjectId = NULL, Name = NULL;
    HRESULT Result, PropertyResult;

    DeviceId = ZpPortable_CopyView(&Request->DeviceId);
    ObjectId = ZpPortable_CopyView(&Request->ObjectId);
    Name = ZpPortable_CopyView(&Request->Name);
    if (DeviceId == NULL || ObjectId == NULL || Name == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    Result = ZpPortable_Open(DeviceId, &Device, &Content);
    if (SUCCEEDED(Result)) Result = IPortableDeviceContent_Properties(Content, &Properties);
    if (SUCCEEDED(Result)) Result = ZpPortable_CreateValues(&Values);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_NAME, Name);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetStringValue(Values,
                                                                         &WPD_OBJECT_ORIGINAL_FILE_NAME,
                                                                         Name);
    if (SUCCEEDED(Result)) Result = IPortableDeviceProperties_SetValues(Properties, ObjectId, Values, &Results);
    if (SUCCEEDED(Result))
    {
        Result = IPortableDeviceValues_GetErrorValue(Results, &WPD_OBJECT_NAME, &PropertyResult);
        if (SUCCEEDED(Result)) Result = PropertyResult;
    }
    if (SUCCEEDED(Result))
    {
        Result = IPortableDeviceValues_GetErrorValue(Results, &WPD_OBJECT_ORIGINAL_FILE_NAME, &PropertyResult);
        if (SUCCEEDED(Result)) Result = PropertyResult;
    }
Cleanup:
    if (Results != NULL) IPortableDeviceValues_Release(Results);
    if (Values != NULL) IPortableDeviceValues_Release(Values);
    if (Properties != NULL) IPortableDeviceProperties_Release(Properties);
    if (Content != NULL) IPortableDeviceContent_Release(Content);
    if (Device != NULL) IPortableDevice_Release(Device);
    Mem_Free(Name);
    Mem_Free(ObjectId);
    Mem_Free(DeviceId);
    return Result;
}

static
NTSTATUS
ZpPortable_SendLocked(
    _In_ PZP_CLIENT_OBJECT Object,
    _In_ ZP_MESSAGE_TYPE MessageType,
    _In_reads_bytes_(BodyLength) const VOID* Body,
    _In_ ULONG BodyLength)
{
    PCZP_TRANSPORT_OPERATIONS Operations = Object->TransportOperations[Object->ActiveTransport];

    return Object->State == ZpClientStateReady && Operations->Send != NULL ?
               Operations->Send(Object->TransportContexts[Object->ActiveTransport], MessageType, Body, BodyLength) :
               STATUS_CONNECTION_DISCONNECTED;
}

static
NTSTATUS
ZpPortable_SendCloseLocked(
    _In_ PZP_CLIENT_PORTABLE_CHANNEL Channel,
    _In_ ZP_STATUS CloseStatus)
{
    BYTE Body[sizeof(ULONG) + ZP_STATUS_WIRE_SIZE];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelClose(Channel->Header.ChannelId, CloseStatus, Body, sizeof(Body), &BodyLength);
    return NT_SUCCESS(Status) ?
               ZpPortable_SendLocked(Channel->Header.Owner, ZpMessageChannelClose, Body, BodyLength) : Status;
}

static
NTSTATUS
ZpPortable_SendWindowLocked(
    _Inout_ PZP_CLIENT_PORTABLE_CHANNEL Channel,
    _In_ ULONG CreditBytes)
{
    BYTE Body[2 * sizeof(ULONG)];
    ULONG BodyLength;
    NTSTATUS Status;

    Status = ZpMessage_EncodeChannelWindow(Channel->Header.ChannelId, CreditBytes, Body, sizeof(Body), &BodyLength);
    if (NT_SUCCESS(Status))
    {
        Channel->ReceiveCredit += CreditBytes;
        Status = ZpPortable_SendLocked(Channel->Header.Owner, ZpMessageChannelWindow, Body, BodyLength);
        if (!NT_SUCCESS(Status)) Channel->ReceiveCredit -= CreditBytes;
    }
    return Status;
}

static
VOID
ZpPortable_DestroyChannel(
    _Inout_ PZP_CLIENT_PORTABLE_CHANNEL Channel)
{
    HRESULT InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    if (Channel->Stream != NULL) IStream_Release(Channel->Stream);
    if (Channel->Content != NULL) IPortableDeviceContent_Release(Channel->Content);
    if (Channel->Device != NULL) IPortableDevice_Release(Channel->Device);
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
    Mem_Free(Channel);
}

static
VOID
ZpPortable_ChannelDestroy(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel)
{
    ZpPortable_DestroyChannel((PZP_CLIENT_PORTABLE_CHANNEL)LocalChannel);
}

static
VOID
ZpPortable_ChannelAbort(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    UNREFERENCED_PARAMETER(LocalChannel);
    UNREFERENCED_PARAMETER(Status);
}

static
ZP_STATUS
ZpPortable_CreateReadChannel(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_PORTABLE_OBJECT_REQUEST_VIEW Request,
    _Out_ PZP_CLIENT_PORTABLE_CHANNEL* Channel,
    _Out_ PULONGLONG FileSize)
{
    PZP_CLIENT_PORTABLE_CHANNEL Object;
    IPortableDeviceResources* Resources = NULL;
    IPortableDeviceProperties* Properties = NULL;
    IPortableDeviceValues* Values = NULL;
    PWSTR DeviceId = NULL, ObjectId = NULL;
    DWORD OptimalSize;
    NTSTATUS Status;
    HRESULT Result;

    Object = Mem_Alloc(sizeof(*Object));
    DeviceId = ZpPortable_CopyView(&Request->DeviceId);
    ObjectId = ZpPortable_CopyView(&Request->ObjectId);
    if (Object == NULL || DeviceId == NULL || ObjectId == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    RtlZeroMemory(Object, sizeof(*Object));
    Object->Type = ZpClientPortableChannelRead;
    Result = ZpPortable_Open(DeviceId, &Object->Device, &Object->Content);
    if (SUCCEEDED(Result)) Result = IPortableDeviceContent_Properties(Object->Content, &Properties);
    if (SUCCEEDED(Result)) Result = IPortableDeviceProperties_GetValues(Properties, ObjectId, NULL, &Values);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_GetUnsignedLargeIntegerValue(Values,
                                                                                       &WPD_OBJECT_SIZE,
                                                                                       FileSize);
    if (SUCCEEDED(Result)) Result = IPortableDeviceContent_Transfer(Object->Content, &Resources);
    if (SUCCEEDED(Result)) Result = IPortableDeviceResources_GetStream(Resources,
                                                                       ObjectId,
                                                                       &WPD_RESOURCE_DEFAULT,
                                                                       STGM_READ,
                                                                       &OptimalSize,
                                                                       &Object->Stream);
    if (SUCCEEDED(Result))
    {
        Object->RemainingBytes = *FileSize;
        Status = ZpClientLocalChannel_Insert(Client,
                                             &Object->Header,
                                             ZP_PORTABLE_DEVICE_MODULE_ID,
                                             NULL,
                                             ZpPortable_ChannelWindow,
                                             ZpPortable_ChannelClose,
                                             ZpPortable_ChannelAbort,
                                             ZpPortable_ChannelDestroy);
    }
Cleanup:
    if (Values != NULL) IPortableDeviceValues_Release(Values);
    if (Properties != NULL) IPortableDeviceProperties_Release(Properties);
    if (Resources != NULL) IPortableDeviceResources_Release(Resources);
    Mem_Free(ObjectId);
    Mem_Free(DeviceId);
    if (FAILED(Result))
    {
        if (Object != NULL) ZpPortable_DestroyChannel(Object);
        return ZpPortable_StatusFromHResult(Result);
    }
    if (!NT_SUCCESS(Status))
    {
        ZpPortable_DestroyChannel(Object);
        return ZpStatus_FromNtStatus(Status);
    }
    *Channel = Object;
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpPortable_CreateWriteChannel(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ PCZP_PORTABLE_WRITE_REQUEST_VIEW Request,
    _Out_ PZP_CLIENT_PORTABLE_CHANNEL* Channel)
{
    PZP_CLIENT_PORTABLE_CHANNEL Object;
    IPortableDeviceValues* Values = NULL;
    PWSTR DeviceId = NULL, ParentId = NULL, Name = NULL;
    DWORD OptimalSize;
    NTSTATUS Status;
    HRESULT Result;

    Object = Mem_Alloc(sizeof(*Object));
    DeviceId = ZpPortable_CopyView(&Request->DeviceId);
    ParentId = ZpPortable_CopyView(&Request->ParentId);
    Name = ZpPortable_CopyView(&Request->Name);
    if (Object == NULL || DeviceId == NULL || ParentId == NULL || Name == NULL)
    {
        Result = E_OUTOFMEMORY;
        goto Cleanup;
    }
    RtlZeroMemory(Object, sizeof(*Object));
    Object->Type = ZpClientPortableChannelWrite;
    Result = ZpPortable_Open(DeviceId, &Object->Device, &Object->Content);
    if (SUCCEEDED(Result)) Result = ZpPortable_CreateValues(&Values);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_PARENT_ID, ParentId);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetStringValue(Values, &WPD_OBJECT_NAME, Name);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetStringValue(Values,
                                                                         &WPD_OBJECT_ORIGINAL_FILE_NAME,
                                                                         Name);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetGuidValue(Values,
                                                                       &WPD_OBJECT_CONTENT_TYPE,
                                                                       &WPD_CONTENT_TYPE_GENERIC_FILE);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetGuidValue(Values,
                                                                       &WPD_OBJECT_FORMAT,
                                                                       &WPD_OBJECT_FORMAT_UNSPECIFIED);
    if (SUCCEEDED(Result)) Result = IPortableDeviceValues_SetUnsignedLargeIntegerValue(Values,
                                                                                       &WPD_OBJECT_SIZE,
                                                                                       Request->FileSize);
    if (SUCCEEDED(Result)) Result = IPortableDeviceContent_CreateObjectWithPropertiesAndData(Object->Content,
                                                                                              Values,
                                                                                              &Object->Stream,
                                                                                              &OptimalSize,
                                                                                              NULL);
    if (SUCCEEDED(Result))
    {
        Object->RemainingBytes = Request->FileSize;
        Status = ZpClientLocalChannel_Insert(Client,
                                             &Object->Header,
                                             ZP_PORTABLE_DEVICE_MODULE_ID,
                                             ZpPortable_ChannelData,
                                             NULL,
                                             ZpPortable_ChannelClose,
                                             ZpPortable_ChannelAbort,
                                             ZpPortable_ChannelDestroy);
    }
Cleanup:
    if (Values != NULL) IPortableDeviceValues_Release(Values);
    Mem_Free(Name);
    Mem_Free(ParentId);
    Mem_Free(DeviceId);
    if (FAILED(Result))
    {
        if (Object != NULL) ZpPortable_DestroyChannel(Object);
        return ZpPortable_StatusFromHResult(Result);
    }
    if (!NT_SUCCESS(Status))
    {
        ZpPortable_DestroyChannel(Object);
        return ZpStatus_FromNtStatus(Status);
    }
    *Channel = Object;
    return ZpStatus_FromNtStatus(Status);
}

static
VOID
ZpPortable_FinishRead(
    _Inout_ PZP_CLIENT_PORTABLE_CHANNEL Channel,
    _In_ ZP_STATUS Status,
    _In_ LOGICAL Notify)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Removed;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    if (Removed && Notify) ZpPortable_SendCloseLocked(Channel, Status);
    Channel->WorkerActive = FALSE;
    Object->CallbackCount--;
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    ZpClientLocalChannel_Release(&Channel->Header);
}

static
VOID
CALLBACK
ZpPortable_ReadCallback(
    _Inout_ PTP_CALLBACK_INSTANCE Instance,
    _In_opt_ PVOID Context)
{
    PZP_CLIENT_PORTABLE_CHANNEL Channel = Context;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    HRESULT InitializeResult, Result;
    PBYTE Body;
    ULONG ReadLength, BytesRead, BodyLength;
    NTSTATUS Status;
    LOGICAL Removed = FALSE;

    UNREFERENCED_PARAMETER(Instance);
    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(InitializeResult) && InitializeResult != RPC_E_CHANGED_MODE)
    {
        ZpPortable_FinishRead(Channel, ZpPortable_StatusFromHResult(InitializeResult), TRUE);
        return;
    }
    Body = Mem_Alloc(sizeof(ULONG) + ZP_PORTABLE_CHANNEL_CHUNK_SIZE);
    if (Body == NULL)
    {
        ZpPortable_FinishRead(Channel, ZpStatus_FromNtStatus(STATUS_NO_MEMORY), TRUE);
        goto Cleanup;
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
        ReadLength = (ULONG)min(min(Channel->Credit, Channel->RemainingBytes), ZP_PORTABLE_CHANNEL_CHUNK_SIZE);
        if (ReadLength == 0)
        {
            if (Channel->RemainingBytes == 0)
            {
                Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
                ZpPortable_SendCloseLocked(Channel, ZpStatus_FromNtStatus(STATUS_SUCCESS));
            }
            Channel->WorkerActive = FALSE;
            Object->CallbackCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            break;
        }
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Result = ISequentialStream_Read((ISequentialStream*)Channel->Stream,
                                        Add2Ptr(Body, sizeof(ULONG)),
                                        ReadLength,
                                        &BytesRead);
        if (FAILED(Result) || BytesRead != ReadLength)
        {
            ZpPortable_FinishRead(Channel,
                                  FAILED(Result) ? ZpPortable_StatusFromHResult(Result) :
                                                   ZpStatus_FromNtStatus(STATUS_END_OF_FILE),
                                  TRUE);
            Mem_Free(Body);
            goto Cleanup;
        }
        Status = ZpMessage_EncodeChannelData(Channel->Header.ChannelId,
                                             Add2Ptr(Body, sizeof(ULONG)),
                                             BytesRead,
                                             Body,
                                             sizeof(ULONG) + ZP_PORTABLE_CHANNEL_CHUNK_SIZE,
                                             &BodyLength);
        if (!NT_SUCCESS(Status))
        {
            ZpPortable_FinishRead(Channel, ZpStatus_FromNtStatus(Status), TRUE);
            Mem_Free(Body);
            goto Cleanup;
        }
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
        Status = ZpPortable_SendLocked(Object, ZpMessageChannelData, Body, BodyLength);
        if (!NT_SUCCESS(Status) || Channel->RemainingBytes == 0)
        {
            Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
            ZpPortable_SendCloseLocked(Channel, ZpStatus_FromNtStatus(Status));
            Channel->WorkerActive = FALSE;
            Object->CallbackCount--;
            RtlReleaseSRWLockExclusive(&Object->Lock);
            break;
        }
        RtlReleaseSRWLockExclusive(&Object->Lock);
    }
    Mem_Free(Body);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    ZpClientLocalChannel_Release(&Channel->Header);
Cleanup:
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
}

static
NTSTATUS
ZpPortable_ChannelWindow(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ULONG CreditBytes)
{
    PZP_CLIENT_PORTABLE_CHANNEL Channel = (PZP_CLIENT_PORTABLE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    LOGICAL Queue = FALSE;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || Channel->Type != ZpClientPortableChannelRead ||
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
    if (Queue && !TrySubmitThreadpoolCallback(ZpPortable_ReadCallback, Channel, NULL))
    {
        ZpPortable_FinishRead(Channel, ZpStatus_FromNtStatus(STATUS_NO_MEMORY), TRUE);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
ZpPortable_ChannelData(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ const ZP_CHANNEL_DATA_VIEW* Message)
{
    PZP_CLIENT_PORTABLE_CHANNEL Channel = (PZP_CLIENT_PORTABLE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    HRESULT InitializeResult, Result;
    ULONG BytesWritten, CreditBytes;
    NTSTATUS Status = STATUS_SUCCESS;
    ZP_STATUS CompletionStatus;
    LOGICAL Removed = FALSE;

    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(InitializeResult) && InitializeResult != RPC_E_CHANGED_MODE)
    {
        RtlAcquireSRWLockExclusive(&Object->Lock);
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        Status = Removed ? ZpPortable_SendCloseLocked(Channel, ZpPortable_StatusFromHResult(InitializeResult)) :
                           STATUS_PROTOCOL_UNREACHABLE;
        RtlReleaseSRWLockExclusive(&Object->Lock);
        if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
        return Status;
    }
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!Channel->Header.Pending || Channel->Type != ZpClientPortableChannelWrite ||
        Message->Data.Length > Channel->ReceiveCredit || Message->Data.Length > Channel->RemainingBytes)
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        Status = STATUS_PROTOCOL_UNREACHABLE;
        goto Cleanup;
    }
    Channel->ReceiveCredit -= Message->Data.Length;
    Result = ISequentialStream_Write((ISequentialStream*)Channel->Stream,
                                     Message->Data.Buffer,
                                     Message->Data.Length,
                                     &BytesWritten);
    if (SUCCEEDED(Result) && BytesWritten != Message->Data.Length) Result = STG_E_WRITEFAULT;
    if (SUCCEEDED(Result)) Channel->RemainingBytes -= BytesWritten;
    if (SUCCEEDED(Result) && Channel->RemainingBytes == 0) Result = IStream_Commit(Channel->Stream, STGC_DEFAULT);
    if (FAILED(Result) || Channel->RemainingBytes == 0)
    {
        CompletionStatus = FAILED(Result) ? ZpPortable_StatusFromHResult(Result) :
                                            ZpStatus_FromNtStatus(STATUS_SUCCESS);
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        Status = ZpPortable_SendCloseLocked(Channel, CompletionStatus);
    }
    else
    {
        CreditBytes = (ULONG)min(Message->Data.Length, Channel->RemainingBytes - Channel->ReceiveCredit);
        if (CreditBytes != 0)
        {
            Status = ZpPortable_SendWindowLocked(Channel, CreditBytes);
            if (!NT_SUCCESS(Status)) Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        }
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
Cleanup:
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
    return Status;
}

static
NTSTATUS
ZpPortable_ChannelClose(
    _Inout_ PZP_CLIENT_LOCAL_CHANNEL LocalChannel,
    _In_ ZP_STATUS Status)
{
    PZP_CLIENT_PORTABLE_CHANNEL Channel = (PZP_CLIENT_PORTABLE_CHANNEL)LocalChannel;
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;

    RtlAcquireSRWLockExclusive(&Object->Lock);
    if ((ZpStatus_IsSuccess(Status) && Channel->RemainingBytes != 0) ||
        !ZpClientLocalChannel_RemoveLocked(&Channel->Header))
    {
        RtlReleaseSRWLockExclusive(&Object->Lock);
        return STATUS_PROTOCOL_UNREACHABLE;
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    ZpClientLocalChannel_Release(&Channel->Header);
    return STATUS_SUCCESS;
}

ZP_STATUS
ZpPortable_Execute(
    _Inout_ PZP_CLIENT_OBJECT Client,
    _In_ BYTE OperationId,
    _In_reads_bytes_opt_(PayloadLength) const VOID* Payload,
    _In_ ULONG PayloadLength,
    _Inout_ volatile LONG* Pending,
    _Outptr_result_bytebuffer_maybenull_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength,
    _Outptr_result_maybenull_ PZP_CLIENT_PORTABLE_CHANNEL* Channel)
{
    ZP_PORTABLE_OBJECT_PAGE_REQUEST_VIEW PageRequest;
    ZP_PORTABLE_OBJECT_REQUEST_VIEW ObjectRequest;
    ZP_PORTABLE_NAME_REQUEST_VIEW NameRequest;
    ZP_PORTABLE_WRITE_REQUEST_VIEW WriteRequest;
    PZP_CLIENT_PORTABLE_CHANNEL PortableChannel = NULL;
    ULONGLONG FileSize;
    HRESULT Result = S_OK, InitializeResult;
    NTSTATUS Status;
    ZP_STATUS OperationStatus = ZpStatus_FromNtStatus(STATUS_SUCCESS);

    UNREFERENCED_PARAMETER(Pending);
    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(InitializeResult) && InitializeResult != RPC_E_CHANGED_MODE)
    {
        return ZpPortable_StatusFromHResult(InitializeResult);
    }
    switch (OperationId)
    {
        case ZP_PORTABLE_DEVICE_OPERATION_ENUMERATE_DEVICES:
            if (PayloadLength == 0) Result = ZpPortable_EnumerateDevices(Response, ResponseLength);
            else OperationStatus = ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
            break;
        case ZP_PORTABLE_DEVICE_OPERATION_ENUMERATE_OBJECTS:
            Status = ZpPortable_DecodeObjectPageRequest(Payload, PayloadLength, &PageRequest);
            if (NT_SUCCESS(Status)) Result = ZpPortable_EnumerateObjects(&PageRequest, Response, ResponseLength);
            else OperationStatus = ZpStatus_FromNtStatus(Status);
            break;
        case ZP_PORTABLE_DEVICE_OPERATION_CREATE_FOLDER:
            Status = ZpPortable_DecodeNameRequest(Payload, PayloadLength, &NameRequest);
            if (NT_SUCCESS(Status)) Result = ZpPortable_CreateFolder(&NameRequest);
            else OperationStatus = ZpStatus_FromNtStatus(Status);
            break;
        case ZP_PORTABLE_DEVICE_OPERATION_DELETE:
            Status = ZpPortable_DecodeObjectRequest(Payload, PayloadLength, &ObjectRequest);
            if (NT_SUCCESS(Status)) Result = ZpPortable_Delete(&ObjectRequest);
            else OperationStatus = ZpStatus_FromNtStatus(Status);
            break;
        case ZP_PORTABLE_DEVICE_OPERATION_RENAME:
            Status = ZpPortable_DecodeNameRequest(Payload, PayloadLength, &NameRequest);
            if (NT_SUCCESS(Status)) Result = ZpPortable_Rename(&NameRequest);
            else OperationStatus = ZpStatus_FromNtStatus(Status);
            break;
        case ZP_PORTABLE_DEVICE_OPERATION_OPEN_READ:
            Status = ZpPortable_DecodeObjectRequest(Payload, PayloadLength, &ObjectRequest);
            OperationStatus = NT_SUCCESS(Status) ?
                                  ZpPortable_CreateReadChannel(Client,
                                                               &ObjectRequest,
                                                               &PortableChannel,
                                                               &FileSize) :
                                  ZpStatus_FromNtStatus(Status);
            if (ZpStatus_IsSuccess(OperationStatus))
            {
                *ResponseLength = 3 * sizeof(ULONGLONG);
                *Response = Mem_Alloc(*ResponseLength);
                Status = *Response != NULL ?
                             ZpFile_EncodeOpenReadResponse(PortableChannel->Header.ChannelId,
                                                           FileSize,
                                                           0,
                                                           *Response,
                                                           *ResponseLength,
                                                           ResponseLength) : STATUS_NO_MEMORY;
                OperationStatus = ZpStatus_FromNtStatus(Status);
            }
            break;
        case ZP_PORTABLE_DEVICE_OPERATION_OPEN_WRITE:
            Status = ZpPortable_DecodeWriteRequest(Payload, PayloadLength, &WriteRequest);
            OperationStatus = NT_SUCCESS(Status) ?
                                  ZpPortable_CreateWriteChannel(Client, &WriteRequest, &PortableChannel) :
                                  ZpStatus_FromNtStatus(Status);
            if (ZpStatus_IsSuccess(OperationStatus))
            {
                *ResponseLength = 2 * sizeof(ULONGLONG);
                *Response = Mem_Alloc(*ResponseLength);
                Status = *Response != NULL ?
                             ZpFile_EncodeOpenWriteResponse(PortableChannel->Header.ChannelId,
                                                            WriteRequest.FileSize,
                                                            *Response,
                                                            *ResponseLength,
                                                            ResponseLength) : STATUS_NO_MEMORY;
                OperationStatus = ZpStatus_FromNtStatus(Status);
            }
            break;
        default:
            OperationStatus = ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
            break;
    }
    if (FAILED(Result)) OperationStatus = ZpPortable_StatusFromHResult(Result);
    if (ZpStatus_IsSuccess(OperationStatus) && PortableChannel != NULL) *Channel = PortableChannel;
    else if (PortableChannel != NULL)
    {
        Mem_Free(*Response);
        *Response = NULL;
        ZpPortable_CommitChannel(PortableChannel, FALSE);
    }
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
    return OperationStatus;
}

VOID
ZpPortable_CommitChannel(
    _Inout_ PZP_CLIENT_PORTABLE_CHANNEL Channel,
    _In_ LOGICAL ResponseSent)
{
    PZP_CLIENT_OBJECT Object = Channel->Header.Owner;
    HRESULT InitializeResult, Result = S_OK;
    ULONG CreditBytes;
    LOGICAL Removed = FALSE;

    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    RtlAcquireSRWLockExclusive(&Object->Lock);
    if (!ResponseSent)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
    }
    else if (Channel->Type == ZpClientPortableChannelWrite)
    {
        if (Channel->RemainingBytes == 0)
        {
            Result = IStream_Commit(Channel->Stream, STGC_DEFAULT);
            Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
            ZpPortable_SendCloseLocked(Channel,
                                       SUCCEEDED(Result) ? ZpStatus_FromNtStatus(STATUS_SUCCESS) :
                                                           ZpPortable_StatusFromHResult(Result));
        }
        else
        {
            CreditBytes = (ULONG)min(Channel->RemainingBytes, ZP_PORTABLE_WRITE_WINDOW_SIZE);
            if (!NT_SUCCESS(ZpPortable_SendWindowLocked(Channel, CreditBytes)))
            {
                Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
            }
        }
    }
    else if (Channel->RemainingBytes == 0)
    {
        Removed = ZpClientLocalChannel_RemoveLocked(&Channel->Header);
        ZpPortable_SendCloseLocked(Channel, ZpStatus_FromNtStatus(STATUS_SUCCESS));
    }
    RtlReleaseSRWLockExclusive(&Object->Lock);
    if (Removed) ZpClientLocalChannel_Release(&Channel->Header);
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
}
