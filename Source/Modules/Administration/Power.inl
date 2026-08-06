#pragma comment(lib, "PowrProf.lib")
#pragma comment(lib, "User32.lib")

#define ZP_POWER_SUPPLY_SHORT_TERM 0x00000008

static const UNICODE_STRING ZpPowerKeyPath = RTL_CONSTANT_STRING(
    L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Power");
static const UNICODE_STRING ZpHiberbootValueName = RTL_CONSTANT_STRING(L"HiberbootEnabled");

static
NTSTATUS
ZpAdministration_QueryFastStartup(
    _Out_ PBOOLEAN Enabled)
{
    BYTE Buffer[FIELD_OFFSET(KEY_VALUE_PARTIAL_INFORMATION, Data) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION Information = (PVOID)Buffer;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key;
    ULONG ResultLength;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&ZpPowerKeyPath,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenKey(&Key, KEY_QUERY_VALUE, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryValueKey(Key,
                                 (PUNICODE_STRING)&ZpHiberbootValueName,
                                 KeyValuePartialInformation,
                                 Information,
                                 sizeof(Buffer),
                                 &ResultLength);
        NtClose(Key);
    }
    if (NT_SUCCESS(Status))
    {
        if (Information->Type != REG_DWORD || Information->DataLength != sizeof(ULONG))
        {
            return STATUS_DATA_ERROR;
        }
        *Enabled = *(UNALIGNED ULONG*)Information->Data != 0;
    }
    return Status;
}

static
NTSTATUS
ZpAdministration_AddPowerSupply(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder,
    _In_ ZP_ADMINISTRATION_KIND Kind,
    _In_ PCWSTR Identity,
    _In_ BOOLEAN ShortTerm)
{
    SYSTEM_POWER_BATTERY_STATE Battery;
    ZP_CODEC_WRITER Writer;
    BYTE Data[sizeof(ULONG)];
    NTSTATUS Status;

    Status = NtPowerInformation(SystemBatteryState, NULL, 0, &Battery, sizeof(Battery));
    if (!NT_SUCCESS(Status)) return Status;
    ZpCodec_InitializeWriter(&Writer, Data, sizeof(Data));
    Status = ZpCodec_WriteUInt32(&Writer, Battery.EstimatedTime);
    if (!NT_SUCCESS(Status)) return Status;
    return ZpAdministration_AddRecordData(
        Builder,
        Kind,
        Battery.BatteryPresent,
        (Battery.AcOnLine ? 1 : 0) | (Battery.Charging ? 2 : 0) |
            (Battery.Discharging ? 4 : 0) | (ShortTerm ? ZP_POWER_SUPPLY_SHORT_TERM : 0),
        ((ULONGLONG)Battery.MaxCapacity << 32) | Battery.RemainingCapacity,
        Identity,
        NULL,
        NULL,
        NULL,
        Data,
        Writer.Offset);
}

static
ZP_STATUS
ZpAdministration_EnumeratePowerPlans(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    HKEY Root;
    GUID* ActiveScheme;
    DWORD Error, Index;

    Error = PowerOpenSystemPowerKey(&Root, KEY_READ, TRUE);
    if (Error != ERROR_SUCCESS) return ZpStatus_FromCode(ZpStatusWin32, Error);
    Error = PowerGetActiveScheme(Root, &ActiveScheme);
    if (Error != ERROR_SUCCESS)
    {
        NtClose(Root);
        return ZpStatus_FromCode(ZpStatusWin32, Error);
    }
    for (Index = 0;; Index++)
    {
        UNICODE_STRING Identity;
        GUID Scheme;
        PWSTR Name;
        DWORD SchemeSize = sizeof(Scheme), NameSize = 0;
        NTSTATUS Status;

        Error = PowerEnumerate(Root, NULL, NULL, ACCESS_SCHEME, Index, (PUCHAR)&Scheme, &SchemeSize);
        if (Error == ERROR_NO_MORE_ITEMS) break;
        if (Error != ERROR_SUCCESS)
        {
            LocalFree(ActiveScheme);
            NtClose(Root);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }
        Error = PowerReadFriendlyName(Root, &Scheme, NULL, NULL, NULL, &NameSize);
        if ((Error != ERROR_SUCCESS && Error != ERROR_MORE_DATA) || NameSize < sizeof(WCHAR))
        {
            LocalFree(ActiveScheme);
            NtClose(Root);
            return ZpStatus_FromCode(
                ZpStatusWin32,
                Error == ERROR_SUCCESS ? ERROR_INVALID_DATA : Error);
        }
        Name = Mem_Alloc(NameSize);
        if (Name == NULL)
        {
            LocalFree(ActiveScheme);
            NtClose(Root);
            return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
        Error = PowerReadFriendlyName(Root, &Scheme, NULL, NULL, (PUCHAR)Name, &NameSize);
        Status = Error == ERROR_SUCCESS ? RtlStringFromGUID(&Scheme, &Identity) : STATUS_UNSUCCESSFUL;
        if (Error == ERROR_SUCCESS && NT_SUCCESS(Status))
        {
            Status = ZpAdministration_AddRecord(Builder,
                                                 ZpAdministrationKindPowerPlan,
                                                 IsEqualGUID(&Scheme, ActiveScheme),
                                                 0,
                                                 0,
                                                 Identity.Buffer,
                                                 Name,
                                                 NULL,
                                                 NULL);
            RtlFreeUnicodeString(&Identity);
        }
        Mem_Free(Name);
        if (Error != ERROR_SUCCESS || !NT_SUCCESS(Status))
        {
            LocalFree(ActiveScheme);
            NtClose(Root);
            return Error != ERROR_SUCCESS ?
                       ZpStatus_FromCode(ZpStatusWin32, Error) :
                       ZpStatus_FromNtStatus(Status);
        }
    }
    LocalFree(ActiveScheme);
    NtClose(Root);
    return ZpStatus_FromNtStatus(STATUS_SUCCESS);
}

ZP_STATUS
ZpAdministration_EnumeratePower(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    SYSTEM_POWER_CAPABILITIES_POLICY Capabilities;
    BOOLEAN Hiberboot;
    NTSTATUS Status;
    ZP_STATUS Result;

    Status = NtPowerInformation(SystemPowerCapabilities, NULL, 0, &Capabilities, sizeof(Capabilities));
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = ZpAdministration_QueryFastStartup(&Hiberboot);
    if (!NT_SUCCESS(Status)) return ZpStatus_FromNtStatus(Status);
    Status = ZpAdministration_AddRecord(&Builder,
                                         ZpAdministrationKindPowerSetting,
                                         Hiberboot,
                                         Capabilities.Hiberboot,
                                         0,
                                         L"FastStartup",
                                         NULL,
                                         NULL,
                                         NULL);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindPowerSetting,
                                             Capabilities.SystemS1 || Capabilities.SystemS2 ||
                                                 Capabilities.SystemS3 || Capabilities.AoAc,
                                             0,
                                             0,
                                             L"Sleep",
                                             NULL,
                                             NULL,
                                             NULL);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindPowerSetting,
                                             Capabilities.SystemS4 && Capabilities.HiberFilePresent,
                                             Capabilities.SystemS4,
                                             0,
                                             L"Hibernate",
                                             NULL,
                                             NULL,
                                             NULL);
    }
    if (!NT_SUCCESS(Status))
    {
        ZpAdministration_FreeBuilder(&Builder);
        return ZpStatus_FromNtStatus(Status);
    }
    Result = ZpAdministration_EnumeratePowerPlans(&Builder);
    if (!ZpStatus_IsSuccess(Result))
    {
        ZpAdministration_FreeBuilder(&Builder);
        return Result;
    }
    if (Capabilities.SystemBatteriesPresent)
    {
        Status = ZpAdministration_AddPowerSupply(
            &Builder,
            ZpAdministrationKindBattery,
            L"Battery",
            Capabilities.BatteriesAreShortTerm);
    }
    if (NT_SUCCESS(Status) && Capabilities.UpsPresent)
    {
        Status = ZpAdministration_AddPowerSupply(&Builder,
                                                  ZpAdministrationKindUps,
                                                  L"UPS",
                                                  TRUE);
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
NTSTATUS
ZpAdministration_SetFastStartup(
    _In_ LOGICAL Enabled)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE Key;
    ULONG Value = Enabled;
    NTSTATUS Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               (PUNICODE_STRING)&ZpPowerKeyPath,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenKey(&Key, KEY_SET_VALUE, &ObjectAttributes);
    if (NT_SUCCESS(Status))
    {
        Status = NtSetValueKey(Key,
                               (PUNICODE_STRING)&ZpHiberbootValueName,
                               0,
                               REG_DWORD,
                               &Value,
                               sizeof(Value));
        NtClose(Key);
    }
    return Status;
}

static
NTSTATUS
ZpAdministration_InitiatePowerAction(
    _In_ POWER_ACTION Action,
    _In_ SYSTEM_POWER_STATE State)
{
    BOOLEAN Previous;
    NTSTATUS Status;

    Status = RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &Previous);
    if (NT_SUCCESS(Status))
    {
        Status = NtInitiatePowerAction(Action, State, 0, TRUE);
        RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, Previous, FALSE, &Previous);
    }
    return Status;
}

static
ZP_STATUS
ZpAdministration_ControlPower(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PWSTR Identity;
    DWORD Error;
    NTSTATUS Status;

    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    switch (Control->Action)
    {
        case ZpAdministrationActionEnable:
        case ZpAdministrationActionDisable:
            Status = wcscmp(Identity, L"FastStartup") == 0 ?
                         ZpAdministration_SetFastStartup(
                             Control->Action == ZpAdministrationActionEnable) :
                         STATUS_NOT_SUPPORTED;
            Mem_Free(Identity);
            return ZpStatus_FromNtStatus(Status);

        case ZpAdministrationActionActivate:
        {
            UNICODE_STRING String;
            GUID Scheme;

            RtlInitUnicodeString(&String, Identity);
            Status = RtlGUIDFromString(&String, &Scheme);
            Error = NT_SUCCESS(Status) ? PowerSetActiveScheme(NULL, &Scheme) : ERROR_INVALID_PARAMETER;
            Mem_Free(Identity);
            return NT_SUCCESS(Status) ?
                       ZpStatus_FromCode(ZpStatusWin32, Error) :
                       ZpStatus_FromNtStatus(Status);
        }

        case ZpAdministrationActionSleep:
            Status = ZpAdministration_InitiatePowerAction(PowerActionSleep, PowerSystemSleeping1);
            break;

        case ZpAdministrationActionHibernate:
            Status = ZpAdministration_InitiatePowerAction(PowerActionHibernate, PowerSystemHibernate);
            break;

        case ZpAdministrationActionShutdown:
            Status = ZpAdministration_InitiatePowerAction(PowerActionShutdownOff, PowerSystemShutdown);
            break;

        case ZpAdministrationActionRestart:
            Status = ZpAdministration_InitiatePowerAction(PowerActionShutdownReset, PowerSystemShutdown);
            break;

        case ZpAdministrationActionFirmware:
        {
            BOOLEAN Previous;

            Status = RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, TRUE, FALSE, &Previous);
            if (NT_SUCCESS(Status))
            {
                Error = ExitWindowsEx(EWX_REBOOT | EWX_BOOTOPTIONS,
                                      SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER) ?
                            ERROR_SUCCESS :
                            GetLastError();
                RtlAdjustPrivilege(SE_SHUTDOWN_PRIVILEGE, Previous, FALSE, &Previous);
                Mem_Free(Identity);
                return ZpStatus_FromCode(ZpStatusWin32, Error);
            }
            break;
        }

        case ZpAdministrationActionSignOut:
            Error = ExitWindowsEx(EWX_LOGOFF, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER) ?
                        ERROR_SUCCESS :
                        GetLastError();
            Mem_Free(Identity);
            return ZpStatus_FromCode(ZpStatusWin32, Error);

        case ZpAdministrationActionLock:
            Error = NtUserLockWorkStation() ? ERROR_SUCCESS : GetLastError();
            Mem_Free(Identity);
            return ZpStatus_FromCode(ZpStatusWin32, Error);

        case ZpAdministrationActionTurnOffDisplay:
        {
            DWORD_PTR Result;

            Error = SendMessageTimeoutW(HWND_BROADCAST,
                                        WM_SYSCOMMAND,
                                        SC_MONITORPOWER,
                                        2,
                                        SMTO_ABORTIFHUNG,
                                        1000,
                                        &Result) ?
                        ERROR_SUCCESS : GetLastError();
            Mem_Free(Identity);
            return ZpStatus_FromCode(ZpStatusWin32, Error);
        }

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }
    Mem_Free(Identity);
    return ZpStatus_FromNtStatus(Status);
}
