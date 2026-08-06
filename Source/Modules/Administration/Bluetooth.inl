#include <bluetoothapis.h>

#include <roapi.h>
#include <windows.devices.radios.h>

#pragma comment(lib, "Bthprops.lib")
#pragma comment(lib, "RuntimeObject.lib")

#define ZP_BLUETOOTH_DISCOVERABLE 0x00000001
#define ZP_BLUETOOTH_CONNECTABLE 0x00000002
#define ZP_BLUETOOTH_POWERED 0x00000004
#define ZP_BLUETOOTH_DISABLED 0x00000008
#define ZP_BLUETOOTH_CONNECTED 0x00000001
#define ZP_BLUETOOTH_AUTHENTICATED 0x00000002
#define ZP_BLUETOOTH_REMEMBERED 0x00000004

typedef __x_ABI_CWindows_CDevices_CRadios_CIRadio ZP_BLUETOOTH_RADIO;
typedef __x_ABI_CWindows_CDevices_CRadios_CIRadioStatics ZP_BLUETOOTH_RADIO_STATICS;
typedef __FIAsyncOperation_1___FIVectorView_1_Windows__CDevices__CRadios__CRadio ZP_BLUETOOTH_RADIOS_ASYNC;
typedef __FIVectorView_1_Windows__CDevices__CRadios__CRadio ZP_BLUETOOTH_RADIOS;
typedef __FIAsyncOperation_1_Windows__CDevices__CRadios__CRadioAccessStatus ZP_BLUETOOTH_ACCESS_ASYNC;

static const IID ZpBluetoothRadioStaticsIid = {
    0x5fb6a12e, 0x67cb, 0x46ae, { 0xaa, 0xe9, 0x65, 0x91, 0x9f, 0x86, 0xef, 0xf4 }
};

static
HRESULT
ZpBluetooth_WaitAsync(
    _In_ IUnknown* Operation)
{
    LARGE_INTEGER Delay = { .QuadPart = -100000 };
    IAsyncInfo* Information;
    AsyncStatus Status;
    HRESULT Result;
    ULONG Count;

    Result = Operation->lpVtbl->QueryInterface(Operation, &IID_IAsyncInfo, (PVOID*)&Information);
    if (FAILED(Result)) return Result;
    for (Count = 0; Count != 1000; Count++)
    {
        Result = IAsyncInfo_get_Status(Information, &Status);
        if (FAILED(Result) || Status != Started) break;
        NtDelayExecution(FALSE, &Delay);
    }
    if (SUCCEEDED(Result))
    {
        if (Status == Error)
        {
            HRESULT Error;

            Result = IAsyncInfo_get_ErrorCode(Information, &Error);
            if (SUCCEEDED(Result)) Result = Error;
        }
        else if (Status == Canceled)
        {
            Result = E_ABORT;
        }
        else if (Status == Started)
        {
            Result = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
    }
    IAsyncInfo_Release(Information);
    return Result;
}

static
HRESULT
ZpBluetooth_GetRadios(
    _Outptr_ ZP_BLUETOOTH_RADIOS** Radios,
    _Out_ PLOGICAL Uninitialize)
{
    ZP_BLUETOOTH_RADIO_STATICS* Statics;
    ZP_BLUETOOTH_RADIOS_ASYNC* Operation;
    HSTRING ClassName;
    HRESULT Result;

    Result = RoInitialize(RO_INIT_MULTITHREADED);
    *Uninitialize = SUCCEEDED(Result);
    if (FAILED(Result) && Result != RPC_E_CHANGED_MODE) return Result;
    Result = WindowsCreateString(RuntimeClass_Windows_Devices_Radios_Radio,
                                 RTL_NUMBER_OF(RuntimeClass_Windows_Devices_Radios_Radio) - 1,
                                 &ClassName);
    if (FAILED(Result)) goto Cleanup;
    Result = RoGetActivationFactory(ClassName,
                                    &ZpBluetoothRadioStaticsIid,
                                    (PVOID*)&Statics);
    WindowsDeleteString(ClassName);
    if (FAILED(Result)) goto Cleanup;
    Result = Statics->lpVtbl->GetRadiosAsync(Statics, &Operation);
    Statics->lpVtbl->Release(Statics);
    if (FAILED(Result)) goto Cleanup;
    Result = ZpBluetooth_WaitAsync((IUnknown*)Operation);
    if (SUCCEEDED(Result)) Result = Operation->lpVtbl->GetResults(Operation, Radios);
    Operation->lpVtbl->Release(Operation);

Cleanup:
    if (FAILED(Result) && *Uninitialize) RoUninitialize();
    return Result;
}

static
VOID
ZpBluetooth_FormatAddress(
    _In_ BLUETOOTH_ADDRESS Address,
    _Out_writes_(CharacterCount) PWSTR Buffer,
    _In_ ULONG CharacterCount)
{
    _snwprintf_s(Buffer,
                 CharacterCount,
                 _TRUNCATE,
                 L"%02X:%02X:%02X:%02X:%02X:%02X",
                 Address.rgBytes[5],
                 Address.rgBytes[4],
                 Address.rgBytes[3],
                 Address.rgBytes[2],
                 Address.rgBytes[1],
                 Address.rgBytes[0]);
}

static
DWORD
ZpBluetooth_OpenRadio(
    _In_ ULONG Index,
    _In_ ULONGLONG Address,
    _Out_ PHANDLE Radio,
    _Out_ PBLUETOOTH_RADIO_INFO Information)
{
    BLUETOOTH_FIND_RADIO_PARAMS Search = { sizeof(Search) };
    HBLUETOOTH_RADIO_FIND Find;
    HANDLE Handle;
    DWORD Error;
    ULONG Position = 0;

    Find = BluetoothFindFirstRadio(&Search, &Handle);
    if (Find == NULL) return GetLastError();
    do
    {
        Information->dwSize = sizeof(*Information);
        Error = BluetoothGetRadioInfo(Handle, Information);
        if (Error == ERROR_SUCCESS &&
            (Address != 0 ? Information->address.ullLong == Address : Position == Index))
        {
            *Radio = Handle;
            BluetoothFindRadioClose(Find);
            return ERROR_SUCCESS;
        }
        NtClose(Handle);
        Position++;
    } while (BluetoothFindNextRadio(Find, &Handle));
    Error = GetLastError();
    BluetoothFindRadioClose(Find);
    return Error == ERROR_NO_MORE_ITEMS ? ERROR_NOT_FOUND : Error;
}

static
NTSTATUS
ZpBluetooth_AddDevices(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ HANDLE Radio,
    _In_ PCWSTR RadioIdentity)
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS Search = { sizeof(Search) };
    BLUETOOTH_DEVICE_INFO Device = { sizeof(Device) };
    HBLUETOOTH_DEVICE_FIND Find;
    FILETIME LastSeen;
    WCHAR Identity[32], Address[24];
    ULONGLONG LastSeenValue;
    ULONG Flags;
    NTSTATUS Status = STATUS_SUCCESS;
    DWORD Error;

    Search.fReturnAuthenticated = TRUE;
    Search.fReturnRemembered = TRUE;
    Search.fReturnUnknown = TRUE;
    Search.fReturnConnected = TRUE;
    Search.hRadio = Radio;
    Find = BluetoothFindFirstDevice(&Search, &Device);
    if (Find == NULL)
    {
        Error = GetLastError();
        return Error == ERROR_NO_MORE_ITEMS ? STATUS_SUCCESS : NTSTATUS_FROM_WIN32(Error);
    }
    do
    {
        ZpBluetooth_FormatAddress(Device.Address, Address, ARRAYSIZE(Address));
        _snwprintf_s(Identity, ARRAYSIZE(Identity), _TRUNCATE, L"device:%012llX", Device.Address.ullLong);
        LastSeenValue = 0;
        if (Device.stLastSeen.wYear != 0 && !SystemTimeToFileTime(&Device.stLastSeen, &LastSeen))
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
            break;
        }
        if (Device.stLastSeen.wYear != 0)
            LastSeenValue = ((ULONGLONG)LastSeen.dwHighDateTime << 32) | LastSeen.dwLowDateTime;
        Flags = (Device.fConnected ? ZP_BLUETOOTH_CONNECTED : 0) |
                (Device.fAuthenticated ? ZP_BLUETOOTH_AUTHENTICATED : 0) |
                (Device.fRemembered ? ZP_BLUETOOTH_REMEMBERED : 0);
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindBluetoothDevice,
                                             Flags,
                                             Device.ulClassofDevice,
                                             LastSeenValue,
                                             Identity,
                                             Device.szName,
                                             Address,
                                             RadioIdentity);
        Device.dwSize = sizeof(Device);
    } while (NT_SUCCESS(Status) && BluetoothFindNextDevice(Find, &Device));
    Error = GetLastError();
    BluetoothFindDeviceClose(Find);
    return NT_SUCCESS(Status) && Error != ERROR_NO_MORE_ITEMS ? NTSTATUS_FROM_WIN32(Error) : Status;
}

static
NTSTATUS
ZpBluetooth_AddDevicesForRadios(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    BLUETOOTH_FIND_RADIO_PARAMS Search = { sizeof(Search) };
    BLUETOOTH_RADIO_INFO Radio = { sizeof(Radio) };
    HBLUETOOTH_RADIO_FIND Find;
    HANDLE Handle;
    WCHAR Identity[32];
    NTSTATUS Status = STATUS_SUCCESS;
    DWORD Error;

    Find = BluetoothFindFirstRadio(&Search, &Handle);
    if (Find == NULL)
    {
        Error = GetLastError();
        return Error == ERROR_NO_MORE_ITEMS ? STATUS_SUCCESS : NTSTATUS_FROM_WIN32(Error);
    }
    do
    {
        Error = BluetoothGetRadioInfo(Handle, &Radio);
        if (Error != ERROR_SUCCESS)
        {
            Status = NTSTATUS_FROM_WIN32(Error);
            NtClose(Handle);
            break;
        }
        _snwprintf_s(Identity, ARRAYSIZE(Identity), _TRUNCATE, L"radio:%012llX", Radio.address.ullLong);
        Status = ZpBluetooth_AddDevices(Builder, Handle, Identity);
        NtClose(Handle);
        Radio.dwSize = sizeof(Radio);
    } while (NT_SUCCESS(Status) && BluetoothFindNextRadio(Find, &Handle));
    Error = GetLastError();
    BluetoothFindRadioClose(Find);
    if (NT_SUCCESS(Status) && Error != ERROR_NO_MORE_ITEMS) Status = NTSTATUS_FROM_WIN32(Error);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateBluetooth(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_BLUETOOTH_RADIOS* Radios;
    ZP_BLUETOOTH_RADIO* Radio;
    HSTRING Name = NULL;
    PCWSTR NameBuffer;
    BLUETOOTH_RADIO_INFO Information;
    ZP_CODEC_WRITER Writer;
    HANDLE Handle;
    WCHAR Identity[32], Address[24];
    BYTE Data[sizeof(ULONG) * 2];
    enum __x_ABI_CWindows_CDevices_CRadios_CRadioKind Kind;
    enum __x_ABI_CWindows_CDevices_CRadios_CRadioState State;
    LOGICAL Uninitialize;
    BOOLEAN Physical;
    UINT32 BluetoothIndex = 0, Count, Index;
    ULONG DataLength, Flags;
    NTSTATUS Status = STATUS_SUCCESS;
    HRESULT Result;

    Result = ZpBluetooth_GetRadios(&Radios, &Uninitialize);
    if (FAILED(Result)) return ZpStatus_FromCode(ZpStatusHResult, Result);
    Result = Radios->lpVtbl->get_Size(Radios, &Count);
    for (Index = 0; SUCCEEDED(Result) && Index != Count; Index++)
    {
        Result = Radios->lpVtbl->GetAt(Radios, Index, &Radio);
        if (FAILED(Result)) break;
        Result = Radio->lpVtbl->get_Kind(Radio, &Kind);
        if (SUCCEEDED(Result) && Kind == RadioKind_Bluetooth)
        {
            Result = Radio->lpVtbl->get_State(Radio, &State);
            if (SUCCEEDED(Result)) Result = Radio->lpVtbl->get_Name(Radio, &Name);
            if (SUCCEEDED(Result))
            {
                NameBuffer = WindowsGetStringRawBuffer(Name, NULL);
                _snwprintf_s(Identity, ARRAYSIZE(Identity), _TRUNCATE, L"radio:%u", Index);
                Flags = (State == RadioState_On ? ZP_BLUETOOTH_POWERED : 0) |
                        (State == RadioState_Disabled ? ZP_BLUETOOTH_DISABLED : 0);
                Physical = FALSE;
                DataLength = 0;
                if (ZpBluetooth_OpenRadio(BluetoothIndex++, 0, &Handle, &Information) == ERROR_SUCCESS)
                {
                    Physical = TRUE;
                    _snwprintf_s(Identity,
                                 ARRAYSIZE(Identity),
                                 _TRUNCATE,
                                 L"radio:%u:%012llX",
                                 Index,
                                 Information.address.ullLong);
                    ZpBluetooth_FormatAddress(Information.address, Address, ARRAYSIZE(Address));
                    ZpCodec_InitializeWriter(&Writer, Data, sizeof(Data));
                    Status = ZpCodec_WriteUInt32(&Writer, Information.manufacturer);
                    if (NT_SUCCESS(Status))
                        Status = ZpCodec_WriteUInt32(&Writer, Information.lmpSubversion);
                    DataLength = Writer.Offset;
                    Flags |= (BluetoothIsDiscoverable(Handle) ? ZP_BLUETOOTH_DISCOVERABLE : 0) |
                             (BluetoothIsConnectable(Handle) ? ZP_BLUETOOTH_CONNECTABLE : 0);
                    NtClose(Handle);
                }
                if (NT_SUCCESS(Status))
                {
                    Status = ZpAdministration_AddRecordData(&Builder,
                                                             ZpAdministrationKindBluetoothRadio,
                                                             Flags,
                                                             State,
                                                             0,
                                                             Identity,
                                                             NameBuffer,
                                                             Physical ? Address : NULL,
                                                             NULL,
                                                             Data,
                                                             DataLength);
                }
                WindowsDeleteString(Name);
            }
        }
        Radio->lpVtbl->Release(Radio);
        if (!NT_SUCCESS(Status)) break;
    }
    Radios->lpVtbl->Release(Radios);
    if (Uninitialize) RoUninitialize();
    if (SUCCEEDED(Result) && NT_SUCCESS(Status)) Status = ZpBluetooth_AddDevicesForRadios(&Builder);
    if (SUCCEEDED(Result) && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    ZpAdministration_FreeBuilder(&Builder);
    return FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, Result) : ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlBluetooth(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    ZP_BLUETOOTH_RADIOS* Radios;
    ZP_BLUETOOTH_RADIO* Radio;
    ZP_BLUETOOTH_ACCESS_ASYNC* Operation;
    BLUETOOTH_RADIO_INFO Information;
    BLUETOOTH_ADDRESS Address;
    HANDLE Handle;
    PWSTR Identity, Argument = NULL, End;
    ULONGLONG Value, AddressValue = 0;
    enum __x_ABI_CWindows_CDevices_CRadios_CRadioAccessStatus Access;
    enum __x_ABI_CWindows_CDevices_CRadios_CRadioKind Kind;
    LOGICAL Uninitialize;
    BOOLEAN RadioAcquired = FALSE;
    DWORD Error = ERROR_SUCCESS;
    BOOL Enabled;
    HRESULT Result;

    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Control->Argument.Length != 0) Argument = ZpAdministration_CopyView(&Control->Argument);
    if (Identity == NULL || (Control->Argument.Length != 0 && Argument == NULL))
    {
        Error = ERROR_NOT_ENOUGH_MEMORY;
        goto Win32Cleanup;
    }
    if (Control->Action == ZpAdministrationActionDelete && wcsncmp(Identity, L"device:", 7) == 0)
    {
        Value = _wcstoui64(Identity + 7, &End, 16);
        if (*End != UNICODE_NULL)
        {
            Error = ERROR_INVALID_PARAMETER;
        }
        else
        {
            Address.ullLong = Value;
            Error = BluetoothRemoveDevice(&Address);
        }
        goto Win32Cleanup;
    }
    if (Control->Action != ZpAdministrationActionConfigure || wcsncmp(Identity, L"radio:", 6) != 0 ||
        Argument == NULL ||
        (wcscmp(Argument, L"discovery:0") != 0 && wcscmp(Argument, L"discovery:1") != 0 &&
         wcscmp(Argument, L"incoming:0") != 0 && wcscmp(Argument, L"incoming:1") != 0 &&
         wcscmp(Argument, L"power:0") != 0 && wcscmp(Argument, L"power:1") != 0))
    {
        Error = ERROR_INVALID_PARAMETER;
        goto Win32Cleanup;
    }
    Value = _wcstoui64(Identity + 6, &End, 10);
    if (*End == L':')
    {
        AddressValue = _wcstoui64(End + 1, &End, 16);
    }
    if (*End != UNICODE_NULL || Value > MAXUINT32 || AddressValue > 0xFFFFFFFFFFFF)
    {
        Error = ERROR_INVALID_PARAMETER;
        goto Win32Cleanup;
    }
    Result = ZpBluetooth_GetRadios(&Radios, &Uninitialize);
    if (FAILED(Result)) goto HResultCleanup;
    Result = Radios->lpVtbl->GetAt(Radios, (UINT32)Value, &Radio);
    RadioAcquired = SUCCEEDED(Result);
    if (SUCCEEDED(Result)) Result = Radio->lpVtbl->get_Kind(Radio, &Kind);
    if (SUCCEEDED(Result) && Kind != RadioKind_Bluetooth) Result = E_INVALIDARG;
    if (SUCCEEDED(Result) && wcsncmp(Argument, L"power:", 6) == 0)
    {
        Result = Radio->lpVtbl->SetStateAsync(Radio,
                                              Argument[6] == L'1' ? RadioState_On : RadioState_Off,
                                              &Operation);
        if (SUCCEEDED(Result))
        {
            Result = ZpBluetooth_WaitAsync((IUnknown*)Operation);
            if (SUCCEEDED(Result)) Result = Operation->lpVtbl->GetResults(Operation, &Access);
            Operation->lpVtbl->Release(Operation);
            if (SUCCEEDED(Result) && Access != RadioAccessStatus_Allowed) Result = E_ACCESSDENIED;
        }
    }
    else if (SUCCEEDED(Result))
    {
        if (AddressValue == 0)
        {
            Error = ERROR_NOT_FOUND;
        }
        else
        {
            Error = ZpBluetooth_OpenRadio(0, AddressValue, &Handle, &Information);
            if (Error == ERROR_SUCCESS)
            {
                Enabled = Argument[wcslen(Argument) - 1] == L'1';
                if (wcsncmp(Argument, L"discovery:", 10) == 0)
                {
                    if (Enabled && !BluetoothIsConnectable(Handle) &&
                        !BluetoothEnableIncomingConnections(Handle, TRUE))
                    {
                        Error = GetLastError();
                    }
                    else if (!BluetoothEnableDiscovery(Handle, Enabled))
                    {
                        Error = GetLastError();
                    }
                }
                else if (!Enabled && BluetoothIsDiscoverable(Handle) &&
                         !BluetoothEnableDiscovery(Handle, FALSE))
                {
                    Error = GetLastError();
                }
                else if (!BluetoothEnableIncomingConnections(Handle, Enabled))
                {
                    Error = GetLastError();
                }
                NtClose(Handle);
            }
        }
    }
    if (RadioAcquired) Radio->lpVtbl->Release(Radio);
    Radios->lpVtbl->Release(Radios);
    if (Uninitialize) RoUninitialize();

HResultCleanup:
    Mem_Free(Argument);
    Mem_Free(Identity);
    return FAILED(Result) ? ZpStatus_FromCode(ZpStatusHResult, Result) :
                               ZpStatus_FromCode(ZpStatusWin32, Error);

Win32Cleanup:
    Mem_Free(Argument);
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusWin32, Error);
}
