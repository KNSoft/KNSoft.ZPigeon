#include <bluetoothapis.h>

#pragma comment(lib, "Bthprops.lib")

#define ZP_BLUETOOTH_DISCOVERABLE 0x00000001
#define ZP_BLUETOOTH_CONNECTABLE 0x00000002
#define ZP_BLUETOOTH_CONNECTED 0x00000001
#define ZP_BLUETOOTH_AUTHENTICATED 0x00000002
#define ZP_BLUETOOTH_REMEMBERED 0x00000004

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
NTSTATUS
ZpBluetooth_AddDevices(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ HANDLE Radio,
    _In_ PCWSTR RadioIdentity)
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS Search = { sizeof(Search) };
    BLUETOOTH_DEVICE_INFO Device = { sizeof(Device) };
    HBLUETOOTH_DEVICE_FIND Find;
    WCHAR Identity[32], Address[24], Detail[128];
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
        _snwprintf_s(Detail,
                     ARRAYSIZE(Detail),
                     _TRUNCATE,
                     L"%s\n%s\n%04u-%02u-%02u %02u:%02u:%02u",
                     Address,
                     RadioIdentity,
                     Device.stLastSeen.wYear,
                     Device.stLastSeen.wMonth,
                     Device.stLastSeen.wDay,
                     Device.stLastSeen.wHour,
                     Device.stLastSeen.wMinute,
                     Device.stLastSeen.wSecond);
        Flags = (Device.fConnected ? ZP_BLUETOOTH_CONNECTED : 0) |
                (Device.fAuthenticated ? ZP_BLUETOOTH_AUTHENTICATED : 0) |
                (Device.fRemembered ? ZP_BLUETOOTH_REMEMBERED : 0);
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindBluetoothDevice,
                                             Flags,
                                             Device.ulClassofDevice,
                                             0,
                                             Identity,
                                             Device.szName,
                                             Address,
                                             Detail);
        Device.dwSize = sizeof(Device);
    } while (NT_SUCCESS(Status) && BluetoothFindNextDevice(Find, &Device));
    Error = GetLastError();
    BluetoothFindDeviceClose(Find);
    return NT_SUCCESS(Status) && Error != ERROR_NO_MORE_ITEMS ? NTSTATUS_FROM_WIN32(Error) : Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateBluetooth(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    BLUETOOTH_FIND_RADIO_PARAMS Search = { sizeof(Search) };
    BLUETOOTH_RADIO_INFO Radio = { sizeof(Radio) };
    HBLUETOOTH_RADIO_FIND Find;
    HANDLE Handle;
    WCHAR Identity[32], Address[24], Detail[128];
    ULONG Flags;
    NTSTATUS Status = STATUS_SUCCESS;
    DWORD Error;

    Find = BluetoothFindFirstRadio(&Search, &Handle);
    if (Find == NULL)
    {
        Error = GetLastError();
        return Error == ERROR_NO_MORE_ITEMS ?
                   ZpStatus_FromNtStatus(ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength)) :
                   ZpStatus_FromCode(ZpStatusWin32, Error);
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
        ZpBluetooth_FormatAddress(Radio.address, Address, ARRAYSIZE(Address));
        _snwprintf_s(Identity, ARRAYSIZE(Identity), _TRUNCATE, L"radio:%012llX", Radio.address.ullLong);
        _snwprintf_s(Detail,
                     ARRAYSIZE(Detail),
                     _TRUNCATE,
                     L"%s\n制造商: 0x%04X\nLMP 子版本: 0x%04X",
                     Address,
                     Radio.manufacturer,
                     Radio.lmpSubversion);
        Flags = (BluetoothIsDiscoverable(Handle) ? ZP_BLUETOOTH_DISCOVERABLE : 0) |
                (BluetoothIsConnectable(Handle) ? ZP_BLUETOOTH_CONNECTABLE : 0);
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindBluetoothRadio,
                                             Flags,
                                             Radio.ulClassofDevice,
                                             0,
                                             Identity,
                                             Radio.szName,
                                             Address,
                                             Detail);
        if (NT_SUCCESS(Status)) Status = ZpBluetooth_AddDevices(&Builder, Handle, Identity);
        NtClose(Handle);
        Radio.dwSize = sizeof(Radio);
    } while (NT_SUCCESS(Status) && BluetoothFindNextRadio(Find, &Handle));
    Error = GetLastError();
    BluetoothFindRadioClose(Find);
    if (NT_SUCCESS(Status) && Error != ERROR_NO_MORE_ITEMS) Status = NTSTATUS_FROM_WIN32(Error);
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlBluetooth(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    BLUETOOTH_FIND_RADIO_PARAMS Search = { sizeof(Search) };
    BLUETOOTH_RADIO_INFO Radio = { sizeof(Radio) };
    BLUETOOTH_ADDRESS Address;
    HBLUETOOTH_RADIO_FIND Find;
    HANDLE Handle;
    PWSTR Identity, Argument = NULL, End;
    ULONGLONG Value;
    DWORD Error = ERROR_NOT_FOUND;
    BOOL Enabled;

    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Control->Argument.Length != 0) Argument = ZpAdministration_CopyView(&Control->Argument);
    if (Identity == NULL || (Control->Argument.Length != 0 && Argument == NULL))
    {
        Error = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
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
        goto Cleanup;
    }
    if (Control->Action != ZpAdministrationActionConfigure || wcsncmp(Identity, L"radio:", 6) != 0 ||
        Argument == NULL ||
        (wcscmp(Argument, L"discovery:0") != 0 && wcscmp(Argument, L"discovery:1") != 0 &&
         wcscmp(Argument, L"incoming:0") != 0 && wcscmp(Argument, L"incoming:1") != 0))
    {
        Error = ERROR_INVALID_PARAMETER;
        goto Cleanup;
    }
    Value = _wcstoui64(Identity + 6, &End, 16);
    if (*End != UNICODE_NULL)
    {
        Error = ERROR_INVALID_PARAMETER;
        goto Cleanup;
    }
    Find = BluetoothFindFirstRadio(&Search, &Handle);
    if (Find == NULL)
    {
        Error = GetLastError();
        goto Cleanup;
    }
    do
    {
        Error = BluetoothGetRadioInfo(Handle, &Radio);
        if (Error == ERROR_SUCCESS && Radio.address.ullLong == Value)
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
                else
                {
                    Error = ERROR_SUCCESS;
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
            else
            {
                Error = ERROR_SUCCESS;
            }
            NtClose(Handle);
            break;
        }
        NtClose(Handle);
        Radio.dwSize = sizeof(Radio);
    } while (BluetoothFindNextRadio(Find, &Handle));
    BluetoothFindRadioClose(Find);
Cleanup:
    Mem_Free(Argument);
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusWin32, Error);
}
