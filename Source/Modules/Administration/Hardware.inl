#include <cfgmgr32.h>
#ifndef DEVPKEY_H_INCLUDED
#include <devpkey.h>
#endif


#pragma comment(lib, "Cfgmgr32.lib")

static
PWSTR
ZpAdministration_QueryDeviceString(
    _In_ DEVINST Device,
    _In_ const DEVPROPKEY* Key)
{
    DEVPROPTYPE Type;
    CONFIGRET Result;
    ULONG Size = 0;
    PWSTR Value;

    Result = CM_Get_DevNode_PropertyW(Device, Key, &Type, NULL, &Size, 0);
    if (Result != CR_BUFFER_SMALL || Type != DEVPROP_TYPE_STRING) return NULL;
    Value = Mem_Alloc(Size + sizeof(WCHAR));
    if (Value == NULL) return NULL;
    Result = CM_Get_DevNode_PropertyW(Device, Key, &Type, (PBYTE)Value, &Size, 0);
    if (Result != CR_SUCCESS)
    {
        Mem_Free(Value);
        return NULL;
    }
    Value[Size / sizeof(WCHAR)] = UNICODE_NULL;
    return Value;
}

static
ZP_STATUS
ZpAdministration_EnumerateHardware(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PWSTR DeviceIds, DeviceId, Name, Description, Manufacturer, ClassName;
    DEVINST Device, Parent;
    CONFIGRET Result;
    ULONG Length, StatusFlags, Problem;
    NTSTATUS Status = STATUS_SUCCESS;

    Result = CM_Get_Device_ID_List_SizeW(&Length, NULL, CM_GETIDLIST_FILTER_PRESENT);
    if (Result != CR_SUCCESS) return ZpStatus_FromCode(ZpStatusConfigurationManager, Result);
    DeviceIds = Mem_Alloc((SIZE_T)Length * sizeof(WCHAR));
    if (DeviceIds == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = CM_Get_Device_ID_ListW(NULL, DeviceIds, Length, CM_GETIDLIST_FILTER_PRESENT);
    for (DeviceId = DeviceIds; Result == CR_SUCCESS && NT_SUCCESS(Status) && *DeviceId != UNICODE_NULL;
         DeviceId += wcslen(DeviceId) + 1)
    {
        Result = CM_Locate_DevNodeW(&Device, DeviceId, CM_LOCATE_DEVNODE_NORMAL);
        if (Result != CR_SUCCESS) break;
        Result = CM_Get_DevNode_Status(&StatusFlags, &Problem, Device, 0);
        if (Result != CR_SUCCESS) break;
        if (CM_Get_Parent(&Parent, Device, 0) != CR_SUCCESS) Parent = 0;
        Name = ZpAdministration_QueryDeviceString(Device, &DEVPKEY_Device_FriendlyName);
        Description = ZpAdministration_QueryDeviceString(Device, &DEVPKEY_Device_DeviceDesc);
        Manufacturer = ZpAdministration_QueryDeviceString(Device, &DEVPKEY_Device_Manufacturer);
        ClassName = ZpAdministration_QueryDeviceString(Device, &DEVPKEY_Device_Class);
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindDevice,
                                             Problem,
                                             StatusFlags,
                                             ((ULONGLONG)Parent << 32) | Device,
                                             DeviceId,
                                             Name != NULL ? Name : Description,
                                             Manufacturer,
                                             ClassName);
        Mem_Free(ClassName);
        Mem_Free(Manufacturer);
        Mem_Free(Description);
        Mem_Free(Name);
    }
    Mem_Free(DeviceIds);
    if (Result == CR_SUCCESS && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }
    ZpAdministration_FreeBuilder(&Builder);
    return Result == CR_SUCCESS ?
               ZpStatus_FromNtStatus(Status) :
               ZpStatus_FromCode(ZpStatusConfigurationManager, Result);
}

static
ZP_STATUS
ZpAdministration_ControlHardware(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PWSTR Identity;
    DEVINST Device;
    CONFIGRET Result;

    if (Control->Action == ZpAdministrationActionRefresh)
    {
        Result = CM_Locate_DevNodeW(&Device, NULL, CM_LOCATE_DEVNODE_NORMAL);
        if (Result == CR_SUCCESS)
        {
            Result = CM_Reenumerate_DevNode(Device, CM_REENUMERATE_SYNCHRONOUS);
        }
        return ZpStatus_FromCode(ZpStatusConfigurationManager, Result);
    }
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = CM_Locate_DevNodeW(&Device, Identity, CM_LOCATE_DEVNODE_NORMAL);
    if (Result == CR_SUCCESS)
    {
        if (Control->Action == ZpAdministrationActionEnable)
        {
            Result = CM_Enable_DevNode(Device, 0);
        }
        else if (Control->Action == ZpAdministrationActionDisable)
        {
            Result = CM_Disable_DevNode(Device, 0);
        }
        else if (Control->Action == ZpAdministrationActionRestart)
        {
            Result = CM_Disable_DevNode(Device, 0);
            if (Result == CR_SUCCESS) Result = CM_Enable_DevNode(Device, 0);
        }
        else if (Control->Action == ZpAdministrationActionUninstall)
        {
            Result = CM_Uninstall_DevNode(Device, 0);
        }
        else
        {
            Result = CR_INVALID_FLAG;
        }
    }
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusConfigurationManager, Result);
}
