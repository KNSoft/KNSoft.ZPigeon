#pragma warning(push)
// ILocation is the only synchronous native desktop location API and remains available on Windows 10+.
#pragma warning(disable: 4995)
#include <locationapi.h>

#pragma comment(lib, "LocationApi.lib")

typedef struct _ZP_LOCATION_EVENTS
{
    ILocationEvents Interface;
    LONG References;
    HANDLE Event;
    ILocationReport* Report;
    HRESULT Result;
} ZP_LOCATION_EVENTS, *PZP_LOCATION_EVENTS;

static
HRESULT
STDMETHODCALLTYPE
ZpLocation_QueryInterface(
    _In_ ILocationEvents* Interface,
    _In_ REFIID InterfaceId,
    _COM_Outptr_ PVOID* Object)
{
    if (!IsEqualIID(InterfaceId, &IID_IUnknown) && !IsEqualIID(InterfaceId, &IID_ILocationEvents))
    {
        *Object = NULL;
        return E_NOINTERFACE;
    }
    *Object = Interface;
    ILocationEvents_AddRef(Interface);
    return S_OK;
}

static
ULONG
STDMETHODCALLTYPE
ZpLocation_AddRef(
    _In_ ILocationEvents* Interface)
{
    PZP_LOCATION_EVENTS Events = CONTAINING_RECORD(Interface, ZP_LOCATION_EVENTS, Interface);

    return InterlockedIncrement(&Events->References);
}

static
ULONG
STDMETHODCALLTYPE
ZpLocation_Release(
    _In_ ILocationEvents* Interface)
{
    PZP_LOCATION_EVENTS Events = CONTAINING_RECORD(Interface, ZP_LOCATION_EVENTS, Interface);

    return InterlockedDecrement(&Events->References);
}

static
HRESULT
STDMETHODCALLTYPE
ZpLocation_OnLocationChanged(
    _In_ ILocationEvents* Interface,
    _In_ REFIID ReportType,
    _In_opt_ ILocationReport* Report)
{
    PZP_LOCATION_EVENTS Events = CONTAINING_RECORD(Interface, ZP_LOCATION_EVENTS, Interface);

    if (IsEqualIID(ReportType, &IID_ILatLongReport) && Report != NULL)
    {
        ILocationReport_AddRef(Report);
        if (InterlockedCompareExchangePointer((PVOID volatile*)&Events->Report, Report, NULL) != NULL)
        {
            ILocationReport_Release(Report);
        }
        Events->Result = S_OK;
        NtSetEvent(Events->Event, NULL);
    }
    return S_OK;
}

static
HRESULT
STDMETHODCALLTYPE
ZpLocation_OnStatusChanged(
    _In_ ILocationEvents* Interface,
    _In_ REFIID ReportType,
    _In_ LOCATION_REPORT_STATUS NewStatus)
{
    PZP_LOCATION_EVENTS Events = CONTAINING_RECORD(Interface, ZP_LOCATION_EVENTS, Interface);

    UNREFERENCED_PARAMETER(ReportType);
    if (NewStatus == REPORT_ACCESS_DENIED || NewStatus == REPORT_ERROR || NewStatus == REPORT_NOT_SUPPORTED)
    {
        Events->Result = NewStatus == REPORT_ACCESS_DENIED ? E_ACCESSDENIED :
                             NewStatus == REPORT_NOT_SUPPORTED ? E_NOTIMPL : E_FAIL;
        NtSetEvent(Events->Event, NULL);
    }
    return S_OK;
}

static ILocationEventsVtbl ZpLocationEventsVtbl = {
    ZpLocation_QueryInterface,
    ZpLocation_AddRef,
    ZpLocation_Release,
    ZpLocation_OnLocationChanged,
    ZpLocation_OnStatusChanged
};

static
HRESULT
ZpLocation_WaitForReport(
    _In_ ILocation* Location,
    _Outptr_ ILocationReport** Report)
{
    ZP_LOCATION_EVENTS Events = { { &ZpLocationEventsVtbl }, 1 };
    LARGE_INTEGER Timeout;
    NTSTATUS Status;
    HRESULT Result;

    Status = NtCreateEvent(&Events.Event, EVENT_MODIFY_STATE | SYNCHRONIZE, NULL, NotificationEvent, FALSE);
    if (!NT_SUCCESS(Status)) return HRESULT_FROM_NT(Status);
    Result = ILocation_RegisterForReport(Location, &Events.Interface, &IID_ILatLongReport, 1000);
    if (SUCCEEDED(Result))
    {
        Timeout.QuadPart = -100000000LL;
        Status = NtWaitForSingleObject(Events.Event, FALSE, &Timeout);
        ILocation_UnregisterForReport(Location, &IID_ILatLongReport);
        Result = Status == STATUS_SUCCESS ? Events.Result :
                     Status == STATUS_TIMEOUT ? HRESULT_FROM_WIN32(ERROR_TIMEOUT) : HRESULT_FROM_NT(Status);
    }
    NtClose(Events.Event);
    if (SUCCEEDED(Result) && Events.Report != NULL)
    {
        *Report = Events.Report;
    }
    else if (Events.Report != NULL)
    {
        ILocationReport_Release(Events.Report);
    }
    return Result;
}

static
ZP_STATUS
ZpAdministration_QueryLocation(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ILocation* Location = NULL;
    ILocationReport* Report = NULL;
    ILatLongReport* Coordinates = NULL;
    SYSTEMTIME Time;
    FILETIME FileTime;
    WCHAR Detail[256];
    DOUBLE Latitude, Longitude, Radius, Altitude, AltitudeError;
    HRESULT Result, InitializeResult;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Identity);
    InitializeResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(InitializeResult) && InitializeResult != RPC_E_CHANGED_MODE)
    {
        return ZpStatus_FromCode(ZpStatusHResult, InitializeResult);
    }
    Result = CoCreateInstance(&CLSID_Location,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_ILocation,
                              (PVOID*)&Location);
    if (SUCCEEDED(Result))
    {
        Result = ILocation_GetReport(Location, &IID_ILatLongReport, &Report);
        if (Result == HRESULT_FROM_WIN32(ERROR_NO_DATA)) Result = ZpLocation_WaitForReport(Location, &Report);
    }
    if (SUCCEEDED(Result))
    {
        Result = ILocationReport_QueryInterface(Report, &IID_ILatLongReport, (PVOID*)&Coordinates);
    }
    if (SUCCEEDED(Result)) Result = ILatLongReport_GetLatitude(Coordinates, &Latitude);
    if (SUCCEEDED(Result)) Result = ILatLongReport_GetLongitude(Coordinates, &Longitude);
    if (SUCCEEDED(Result)) Result = ILatLongReport_GetErrorRadius(Coordinates, &Radius);
    if (SUCCEEDED(Result)) Result = ILatLongReport_GetAltitude(Coordinates, &Altitude);
    if (Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
    {
        Altitude = AltitudeError = 0;
        Result = S_OK;
    }
    else if (SUCCEEDED(Result))
    {
        Result = ILatLongReport_GetAltitudeError(Coordinates, &AltitudeError);
        if (Result == HRESULT_FROM_WIN32(ERROR_NOT_FOUND))
        {
            AltitudeError = 0;
            Result = S_OK;
        }
    }
    if (SUCCEEDED(Result)) Result = ILatLongReport_GetTimestamp(Coordinates, &Time);
    if (SUCCEEDED(Result) && !SystemTimeToFileTime(&Time, &FileTime)) Result = HRESULT_FROM_WIN32(GetLastError());
    if (SUCCEEDED(Result))
    {
        _snwprintf_s(Detail,
                     ARRAYSIZE(Detail),
                     _TRUNCATE,
                     L"纬度: %.8f\n经度: %.8f\n误差半径: %.2f 米\n海拔: %.2f 米\n海拔误差: %.2f 米",
                     Latitude,
                     Longitude,
                     Radius,
                     Altitude,
                     AltitudeError);
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindLocation,
                                             0,
                                             0,
                                             ((ULONGLONG)FileTime.dwHighDateTime << 32) | FileTime.dwLowDateTime,
                                             L"current",
                                             L"当前位置",
                                             NULL,
                                             Detail);
        if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    if (Coordinates != NULL) ILatLongReport_Release(Coordinates);
    if (Report != NULL) ILocationReport_Release(Report);
    if (Location != NULL) ILocation_Release(Location);
    if (SUCCEEDED(InitializeResult)) CoUninitialize();
    ZpAdministration_FreeBuilder(&Builder);
    return FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, Result) : ZpStatus_FromNtStatus(Status);
}

#pragma warning(pop)
