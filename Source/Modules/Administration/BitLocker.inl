typedef struct _ZP_BITLOCKER_ENUMERATION_CONTEXT
{
    PZP_ADMINISTRATION_BUILDER Builder;
    LOGICAL Protectors;
} ZP_BITLOCKER_ENUMERATION_CONTEXT, *PZP_BITLOCKER_ENUMERATION_CONTEXT;

static
LOGICAL
ZpBitLocker_IsValidText(
    _In_ PCZP_STRING_VIEW View,
    _In_ ULONG MinimumLength,
    _In_ ULONG MaximumLength)
{
    PCWCH Buffer = (PCWCH)View->Buffer;
    ULONG Index;

    if (View->Length < MinimumLength || View->Length > MaximumLength)
    {
        return FALSE;
    }
    for (Index = 0; Index < View->Length; Index++)
    {
        if (Buffer[Index] == UNICODE_NULL)
        {
            return FALSE;
        }
    }
    return TRUE;
}

static
VOID
ZpBitLocker_GetDriveLetter(
    _In_ PCWSTR VolumeName,
    _Out_writes_(3) PWSTR DriveLetter)
{
    PWSTR Paths, Path;
    SIZE_T PathLength;
    DWORD PathCch;

    DriveLetter[0] = UNICODE_NULL;
    if (GetVolumePathNamesForVolumeNameW(VolumeName, NULL, 0, &PathCch) ||
        GetLastError() != ERROR_MORE_DATA || PathCch == 0 ||
        (SIZE_T)PathCch > MAXSIZE_T / sizeof(WCHAR))
    {
        return;
    }
    Paths = Mem_Alloc((SIZE_T)PathCch * sizeof(WCHAR));
    if (Paths == NULL)
    {
        return;
    }
    if (GetVolumePathNamesForVolumeNameW(VolumeName, Paths, PathCch, &PathCch))
    {
        for (Path = Paths; *Path != UNICODE_NULL; Path += PathLength + 1)
        {
            PathLength = wcslen(Path);
            if (PathLength == 3 && Path[1] == L':' && Path[2] == L'\\')
            {
                DriveLetter[0] = Path[0];
                DriveLetter[1] = L':';
                DriveLetter[2] = UNICODE_NULL;
                break;
            }
        }
    }
    Mem_Free(Paths);
}

static
ULONG
ZpBitLocker_GetConversionStatus(
    _In_ ULONG Flags)
{
    if ((Flags & FVE_STATUS_FLAG_FULLY_DECRYPTED) != 0)
    {
        return ZP_ADMINISTRATION_BITLOCKER_CONVERSION_DECRYPTED;
    }
    if ((Flags & FVE_STATUS_FLAG_FULLY_ENCRYPTED) != 0)
    {
        return ZP_ADMINISTRATION_BITLOCKER_CONVERSION_ENCRYPTED;
    }
    if ((Flags & FVE_STATUS_FLAG_ENCRYPTION_IN_PROGRESS) != 0)
    {
        return (Flags & FVE_STATUS_FLAG_CONVERSION_PAUSED_MASK) != 0 ?
                   ZP_ADMINISTRATION_BITLOCKER_CONVERSION_ENCRYPTION_PAUSED :
                   ZP_ADMINISTRATION_BITLOCKER_CONVERSION_ENCRYPTING;
    }
    if ((Flags & FVE_STATUS_FLAG_DECRYPTION_IN_PROGRESS) != 0)
    {
        return (Flags & FVE_STATUS_FLAG_CONVERSION_PAUSED_MASK) != 0 ?
                   ZP_ADMINISTRATION_BITLOCKER_CONVERSION_DECRYPTION_PAUSED :
                   ZP_ADMINISTRATION_BITLOCKER_CONVERSION_DECRYPTING;
    }
    return ZP_ADMINISTRATION_BITLOCKER_CONVERSION_UNKNOWN;
}

static
ULONG
ZpBitLocker_GetProtectionStatus(
    _In_ ULONG Flags)
{
    if ((Flags & FVE_STATUS_FLAG_LOCKED) != 0)
    {
        return ZP_ADMINISTRATION_BITLOCKER_PROTECTION_UNKNOWN;
    }
    return (Flags & (FVE_STATUS_FLAG_PROTECTION_ACTIVE |
                     FVE_STATUS_FLAG_FULLY_ENCRYPTED |
                     FVE_STATUS_FLAG_CLEAR_KEY)) ==
               (FVE_STATUS_FLAG_PROTECTION_ACTIVE | FVE_STATUS_FLAG_FULLY_ENCRYPTED) ?
               ZP_ADMINISTRATION_BITLOCKER_PROTECTION_ON :
               ZP_ADMINISTRATION_BITLOCKER_PROTECTION_OFF;
}

static
ULONG
ZpBitLocker_GetVolumeType(
    _In_ ULONG Flags)
{
    if ((Flags & FVE_STATUS_FLAG_OS_VOLUME) != 0)
    {
        return ZP_ADMINISTRATION_BITLOCKER_VOLUME_TYPE_OS;
    }
    return (Flags & FVE_STATUS_FLAG_REMOVABLE_DATA_VOLUME) != 0 ?
               ZP_ADMINISTRATION_BITLOCKER_VOLUME_TYPE_REMOVABLE :
               ZP_ADMINISTRATION_BITLOCKER_VOLUME_TYPE_FIXED;
}

static
ULONG
ZpBitLocker_GetEncryptionMethod(
    _In_ HANDLE VolumeHandle,
    _In_ ULONG StatusFlags)
{
    FVE_LEGACY_METHOD Method;

    if ((StatusFlags & FVE_STATUS_FLAG_INITIALIZED) == 0)
    {
        return FveLegacyMethodNone;
    }
    if (FAILED(FveGetFveMethod(VolumeHandle, &Method)) ||
        Method < FveLegacyMethodNone || Method > FveLegacyMethodXtsAes256)
    {
        return ZP_ADMINISTRATION_BITLOCKER_ENCRYPTION_METHOD_UNKNOWN;
    }
    return (ULONG)Method;
}

static
ULONG
ZpBitLocker_GetProtectorType(
    _In_ ULONG AuthFlags)
{
    switch (AuthFlags & (FVE_AUTH_INFORMATION_FLAG_CLEAR_KEY |
                         FVE_AUTH_INFORMATION_PROTECTOR_MASK))
    {
        case FVE_AUTH_INFORMATION_FLAG_CLEAR_KEY:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_CLEAR_KEY;
        case FVE_AUTH_INFORMATION_FLAG_TPM:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_TPM;
        case FVE_AUTH_INFORMATION_FLAG_EXTERNAL_KEY:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_EXTERNAL_KEY;
        case FVE_AUTH_INFORMATION_FLAG_RECOVERY_PASSWORD:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_RECOVERY_PASSWORD;
        case FVE_AUTH_INFORMATION_FLAG_TPM_AND_PIN:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_TPM_PIN;
        case FVE_AUTH_INFORMATION_FLAG_TPM_AND_STARTUP_KEY:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_TPM_STARTUP_KEY;
        case FVE_AUTH_INFORMATION_FLAG_TPM_PIN_AND_STARTUP_KEY:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_TPM_PIN_STARTUP_KEY;
        case FVE_AUTH_INFORMATION_FLAG_CERTIFICATE:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_PUBLIC_KEY;
        case FVE_AUTH_INFORMATION_FLAG_PASSPHRASE:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_PASSPHRASE;
        case FVE_AUTH_INFORMATION_FLAG_TPM_AND_CERTIFICATE:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_TPM_CERTIFICATE;
        case FVE_AUTH_INFORMATION_FLAG_DPAPI_NG:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_DPAPI_NG;
        default:
            return ZP_ADMINISTRATION_BITLOCKER_PROTECTOR_UNKNOWN;
    }
}

static
HRESULT
ZpBitLocker_AddVolume(
    _In_ PCWSTR VolumeName,
    _In_ HANDLE VolumeHandle,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    FVE_STATUS_V9 FveStatus;
    WCHAR DriveLetter[3];
    GUID AutoUnlockGuid;
    BOOL AutoUnlockEnabled;
    ULONG Flags, Percentage, ProtectionStatus;
    NTSTATUS Status;
    HRESULT Hr;

    Hr = Sys_FveGetStatus(VolumeHandle, &FveStatus);
    if (FAILED(Hr))
    {
        return Hr;
    }
    Flags = ZpBitLocker_GetVolumeType(FveStatus.Flags);
    ProtectionStatus = ZpBitLocker_GetProtectionStatus(FveStatus.Flags);
    Flags |= (ProtectionStatus << ZP_ADMINISTRATION_BITLOCKER_PROTECTION_SHIFT) &
             ZP_ADMINISTRATION_BITLOCKER_PROTECTION_MASK;
    Flags |= (((FveStatus.Flags & FVE_STATUS_FLAG_LOCKED) != 0 ?
                   ZP_ADMINISTRATION_BITLOCKER_LOCK_LOCKED :
                   ZP_ADMINISTRATION_BITLOCKER_LOCK_UNLOCKED) <<
              ZP_ADMINISTRATION_BITLOCKER_LOCK_SHIFT) &
             ZP_ADMINISTRATION_BITLOCKER_LOCK_MASK;
    Flags |= (ZpBitLocker_GetEncryptionMethod(VolumeHandle, FveStatus.Flags) <<
              ZP_ADMINISTRATION_BITLOCKER_ENCRYPTION_METHOD_SHIFT) &
             ZP_ADMINISTRATION_BITLOCKER_ENCRYPTION_METHOD_MASK;
    if ((FveStatus.Flags & FVE_STATUS_FLAG_INITIALIZED) != 0)
    {
        Flags |= ZP_ADMINISTRATION_BITLOCKER_FLAG_INITIALIZED;
    }
    if (FveIsBoundDataVolume(VolumeHandle, &AutoUnlockEnabled, &AutoUnlockGuid) == S_OK &&
        AutoUnlockEnabled)
    {
        Flags |= ZP_ADMINISTRATION_BITLOCKER_FLAG_AUTO_UNLOCK;
    }
    if ((FveStatus.Flags & FVE_STATUS_FLAG_DATA_ONLY_ENCRYPTION) != 0)
    {
        Flags |= ZP_ADMINISTRATION_BITLOCKER_FLAG_DATA_ONLY;
    }
    if (FveStatus.ConvertedPercent <= 0)
    {
        Percentage = 0;
    }
    else if (FveStatus.ConvertedPercent >= 100)
    {
        Percentage = 100;
    }
    else
    {
        Percentage = (ULONG)FveStatus.ConvertedPercent;
    }
    ZpBitLocker_GetDriveLetter(VolumeName, DriveLetter);
    Status = ZpAdministration_AddRecord(
        Builder,
        ZpAdministrationKindBitLockerVolume,
        ZpBitLocker_GetConversionStatus(FveStatus.Flags),
        Flags,
        Percentage,
        VolumeName,
        DriveLetter[0] == UNICODE_NULL ? VolumeName : DriveLetter,
        NULL,
        NULL);
    return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
}

static
HRESULT
ZpBitLocker_AddProtector(
    _In_ PCWSTR VolumeName,
    _In_reads_(3) PCWSTR DriveLetter,
    _In_ PCFVE_AUTH_INFORMATION Information,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    UNICODE_STRING ProtectorId;
    ULONGLONG CreationTime;
    ULONG ProtectorFlags, ProtectorType;
    NTSTATUS Status;

    ProtectorFlags = Information->AuthFlags & (FVE_AUTH_INFORMATION_FLAG_CLEAR_KEY |
                                                FVE_AUTH_INFORMATION_PROTECTOR_MASK);
    if (ProtectorFlags == 0)
    {
        return S_OK;
    }
    ProtectorType = ZpBitLocker_GetProtectorType(ProtectorFlags);
    CreationTime = ((ULONGLONG)Information->CreationTime.dwHighDateTime << 32) |
                   Information->CreationTime.dwLowDateTime;
    Status = RtlStringFromGUID(&Information->Identifier, &ProtectorId);
    if (!NT_SUCCESS(Status))
    {
        return HRESULT_FROM_NT(Status);
    }
    Status = ZpAdministration_AddRecord(
        Builder,
        ZpAdministrationKindBitLockerProtector,
        ProtectorType,
        0,
        CreationTime,
        ProtectorId.Buffer,
        DriveLetter[0] == UNICODE_NULL ? VolumeName : DriveLetter,
        Information->Description,
        VolumeName);
    RtlFreeUnicodeString(&ProtectorId);
    return NT_SUCCESS(Status) ? S_OK : HRESULT_FROM_NT(Status);
}

static
HRESULT
ZpBitLocker_AddProtectors(
    _In_ PCWSTR VolumeName,
    _In_ HANDLE VolumeHandle,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    FVE_STATUS_V9 FveStatus;
    PFVE_AUTH_INFORMATION Information;
    PGUID ProtectorGuids;
    GUID ClearKeyGuid;
    WCHAR DriveLetter[3];
    SIZE_T InformationSize;
    UINT ProtectorCount, Index;
    LOGICAL ClearKeyAdded = FALSE;
    HRESULT Hr;

    Hr = Sys_FveGetStatus(VolumeHandle, &FveStatus);
    if (FAILED(Hr))
    {
        return Hr;
    }
    if ((FveStatus.Flags & FVE_STATUS_FLAG_INITIALIZED) == 0)
    {
        return S_OK;
    }
    Hr = Sys_FveGetAuthMethodGuids(VolumeHandle, &ProtectorGuids, &ProtectorCount);
    if (FAILED(Hr))
    {
        return Hr;
    }
    ZpBitLocker_GetDriveLetter(VolumeName, DriveLetter);
    if ((FveStatus.Flags & FVE_STATUS_FLAG_CLEAR_KEY) != 0)
    {
        Hr = Sys_FveGetAuthMethodInformation(VolumeHandle,
                                             NULL,
                                             FVE_AUTH_INFORMATION_FLAG_CLEAR_KEY |
                                                 FVE_AUTH_INFORMATION_QUERY_UNKNOWN2,
                                             &Information,
                                             &InformationSize);
        if (SUCCEEDED(Hr))
        {
            Hr = ZpBitLocker_AddProtector(VolumeName, DriveLetter, Information, Builder);
            ClearKeyGuid = Information->Identifier;
            ClearKeyAdded = SUCCEEDED(Hr);
            Sys_FveFreeAuthMethodInformation(Information, InformationSize);
        }
    }
    if (FAILED(Hr))
    {
        Mem_Free(ProtectorGuids);
        return Hr;
    }
    for (Index = 0; Index < ProtectorCount; Index++)
    {
        if (ClearKeyAdded && RtlEqualMemory(&ProtectorGuids[Index], &ClearKeyGuid, sizeof(GUID)))
        {
            continue;
        }
        Hr = Sys_FveGetAuthMethodInformation(VolumeHandle,
                                             &ProtectorGuids[Index],
                                             FVE_AUTH_INFORMATION_QUERY_UNKNOWN1,
                                             &Information,
                                             &InformationSize);
        if (FAILED(Hr))
        {
            break;
        }
        Hr = ZpBitLocker_AddProtector(VolumeName, DriveLetter, Information, Builder);
        Sys_FveFreeAuthMethodInformation(Information, InformationSize);
        if (FAILED(Hr))
        {
            break;
        }
    }
    Mem_Free(ProtectorGuids);
    return Hr;
}

static
HRESULT
CALLBACK
ZpBitLocker_EnumerateVolume(
    _In_ PCWSTR VolumeName,
    _In_ FVE_DEVICE_TYPE DeviceType,
    _In_opt_ PVOID Context)
{
    PZP_BITLOCKER_ENUMERATION_CONTEXT Enumeration = Context;
    HANDLE VolumeHandle;
    HRESULT CloseHr, Hr;

    UNREFERENCED_PARAMETER(DeviceType);
    Hr = FveOpenVolumeW(VolumeName, FALSE, &VolumeHandle);
    if (FAILED(Hr))
    {
        return Hr;
    }
    Hr = Enumeration->Protectors ?
             ZpBitLocker_AddProtectors(VolumeName, VolumeHandle, Enumeration->Builder) :
             ZpBitLocker_AddVolume(VolumeName, VolumeHandle, Enumeration->Builder);
    CloseHr = FveCloseVolume(VolumeHandle);
    return SUCCEEDED(Hr) && FAILED(CloseHr) ? CloseHr : Hr;
}

static
ZP_STATUS
ZpBitLocker_Enumerate(
    _In_ LOGICAL Protectors,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_BITLOCKER_ENUMERATION_CONTEXT Context = { &Builder, Protectors };
    NTSTATUS Status;
    HRESULT Hr;

    Hr = Sys_FveEnumerateVolumes(ZpBitLocker_EnumerateVolume, &Context);
    if (FAILED(Hr))
    {
        ZpAdministration_FreeBuilder(&Builder);
        return ZpStatus_FromCode(ZpStatusHResult, Hr);
    }
    Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateBitLockerVolumes(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    return ZpBitLocker_Enumerate(FALSE, Response, ResponseLength);
}

static
ZP_STATUS
ZpAdministration_EnumerateBitLockerProtectors(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    return ZpBitLocker_Enumerate(TRUE, Response, ResponseLength);
}

static
HRESULT
ZpBitLocker_ParseUInt32(
    _In_ PCZP_STRING_VIEW View,
    _In_ ULONG Maximum,
    _Out_ PULONG Value)
{
    PCWCH Buffer = (PCWCH)View->Buffer;
    ULONG Parsed = 0, Digit, Index;

    if (View->Length == 0)
    {
        return E_INVALIDARG;
    }
    for (Index = 0; Index < View->Length; Index++)
    {
        if (Buffer[Index] < L'0' || Buffer[Index] > L'9')
        {
            return E_INVALIDARG;
        }
        Digit = Buffer[Index] - L'0';
        if (Digit > Maximum || Parsed > (Maximum - Digit) / 10)
        {
            return E_INVALIDARG;
        }
        Parsed = Parsed * 10 + Digit;
    }
    *Value = Parsed;
    return S_OK;
}

static
HRESULT
ZpBitLocker_OpenVolume(
    _In_ PCZP_STRING_VIEW Identity,
    _In_ BOOL NeedWriteAccess,
    _Out_ PHANDLE VolumeHandle)
{
    PWSTR VolumeName;
    HRESULT Hr;

    if (!ZpBitLocker_IsValidText(Identity, 1, MAX_PATH))
    {
        return E_INVALIDARG;
    }
    VolumeName = ZpAdministration_CopyView(Identity);
    if (VolumeName == NULL)
    {
        return E_OUTOFMEMORY;
    }
    Hr = FveOpenVolumeW(VolumeName, NeedWriteAccess, VolumeHandle);
    Mem_Free(VolumeName);
    return Hr;
}

static
HRESULT
ZpBitLocker_CopySecret(
    _In_ PCZP_STRING_VIEW View,
    _In_ ULONG MaximumLength,
    _Outptr_ PWSTR* Secret)
{
    if (!ZpBitLocker_IsValidText(View, 1, MaximumLength))
    {
        return E_INVALIDARG;
    }
    *Secret = ZpAdministration_CopyView(View);
    return *Secret == NULL ? E_OUTOFMEMORY : S_OK;
}

static
ZP_STATUS
ZpAdministration_ControlBitLockerVolume(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    HANDLE VolumeHandle;
    HRESULT CloseHr, Hr;

    Hr = ZpBitLocker_OpenVolume(&Control->Identity, TRUE, &VolumeHandle);
    if (FAILED(Hr))
    {
        return ZpStatus_FromCode(ZpStatusHResult, Hr);
    }
    switch (Control->Action)
    {
        case ZpAdministrationActionEncrypt:
        {
            ULONG EncryptionArgument, EncryptionMethod;

            Hr = ZpBitLocker_ParseUInt32(
                &Control->Argument,
                ZP_ADMINISTRATION_BITLOCKER_ENCRYPT_ARGUMENT_METHOD_MASK |
                    ZP_ADMINISTRATION_BITLOCKER_ENCRYPT_ARGUMENT_DATA_ONLY,
                &EncryptionArgument);
            if (SUCCEEDED(Hr))
            {
                EncryptionMethod = EncryptionArgument &
                                   ZP_ADMINISTRATION_BITLOCKER_ENCRYPT_ARGUMENT_METHOD_MASK;
                if ((EncryptionArgument &
                     ~(ZP_ADMINISTRATION_BITLOCKER_ENCRYPT_ARGUMENT_METHOD_MASK |
                       ZP_ADMINISTRATION_BITLOCKER_ENCRYPT_ARGUMENT_DATA_ONLY)) != 0 ||
                    (EncryptionMethod != FveLegacyMethodAes128 &&
                     EncryptionMethod != FveLegacyMethodAes256 &&
                     EncryptionMethod != FveLegacyMethodXtsAes128 &&
                     EncryptionMethod != FveLegacyMethodXtsAes256))
                {
                    Hr = E_INVALIDARG;
                }
            }
            if (SUCCEEDED(Hr))
            {
                Hr = Sys_FveEncrypt(
                    VolumeHandle,
                    (FVE_LEGACY_METHOD)EncryptionMethod,
                    (EncryptionArgument & ZP_ADMINISTRATION_BITLOCKER_ENCRYPT_ARGUMENT_DATA_ONLY) != 0);
            }
            break;
        }
        case ZpAdministrationActionDecrypt:
            Hr = Sys_FveDecrypt(VolumeHandle);
            break;
        case ZpAdministrationActionPause:
            Hr = FveConversionStop(VolumeHandle);
            break;
        case ZpAdministrationActionResume:
            Hr = FveConversionResume(VolumeHandle);
            break;
        case ZpAdministrationActionEnable:
            Hr = Sys_FveEnableProtectors(VolumeHandle);
            break;
        case ZpAdministrationActionDisable:
        {
            ULONG DisableCount = SYS_FVE_DISABLE_COUNT_DEFAULT;

            if (Control->Argument.Length != 0)
            {
                Hr = ZpBitLocker_ParseUInt32(&Control->Argument, 15, &DisableCount);
            }
            if (SUCCEEDED(Hr))
            {
                Hr = Sys_FveDisableProtectors(VolumeHandle, DisableCount);
            }
            break;
        }
        case ZpAdministrationActionLock:
            Hr = FveLockVolume(VolumeHandle, FALSE);
            break;
        case ZpAdministrationActionUnlock:
        {
            PWSTR RecoveryPassword;

            Hr = ZpBitLocker_CopySecret(&Control->Secret, 64, &RecoveryPassword);
            if (SUCCEEDED(Hr))
            {
                Hr = Sys_FveUnlockWithRecoveryPassword(VolumeHandle, RecoveryPassword);
                RtlSecureZeroMemory(RecoveryPassword,
                                    ((SIZE_T)Control->Secret.Length + 1) * sizeof(WCHAR));
                Mem_Free(RecoveryPassword);
            }
            break;
        }
        default:
            Hr = E_INVALIDARG;
            break;
    }
    CloseHr = FveCloseVolume(VolumeHandle);
    if (SUCCEEDED(Hr) && FAILED(CloseHr))
    {
        Hr = CloseHr;
    }
    return ZpStatus_FromCode(ZpStatusHResult, Hr);
}

static
ZP_STATUS
ZpAdministration_ControlBitLockerProtector(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    HANDLE VolumeHandle;
    HRESULT CloseHr, Hr;

    if (Control->Action != ZpAdministrationActionCreate &&
        Control->Action != ZpAdministrationActionDelete)
    {
        return ZpStatus_FromCode(ZpStatusHResult, E_INVALIDARG);
    }
    Hr = ZpBitLocker_OpenVolume(&Control->Identity, TRUE, &VolumeHandle);
    if (FAILED(Hr))
    {
        return ZpStatus_FromCode(ZpStatusHResult, Hr);
    }
    if (Control->Action == ZpAdministrationActionCreate)
    {
        PWSTR RecoveryPassword = NULL, Description = NULL;
        GUID ProtectorGuid;

        if (!ZpBitLocker_IsValidText(&Control->Argument, 0, 256))
        {
            Hr = E_INVALIDARG;
        }
        else if (Control->Argument.Length != 0)
        {
            Description = ZpAdministration_CopyView(&Control->Argument);
            if (Description == NULL)
            {
                Hr = E_OUTOFMEMORY;
            }
        }
        if (SUCCEEDED(Hr))
        {
            Hr = ZpBitLocker_CopySecret(&Control->Secret, 64, &RecoveryPassword);
        }
        if (SUCCEEDED(Hr))
        {
            Hr = Sys_FveAddRecoveryPasswordProtector(VolumeHandle,
                                                      RecoveryPassword,
                                                      Description,
                                                      &ProtectorGuid);
        }
        if (RecoveryPassword != NULL)
        {
            RtlSecureZeroMemory(RecoveryPassword,
                                ((SIZE_T)Control->Secret.Length + 1) * sizeof(WCHAR));
            Mem_Free(RecoveryPassword);
        }
        Mem_Free(Description);
    }
    else
    {
        UNICODE_STRING ProtectorId;
        GUID ProtectorGuid;
        NTSTATUS Status;

        if (!ZpBitLocker_IsValidText(&Control->Argument, 1, 128))
        {
            Hr = E_INVALIDARG;
        }
        else
        {
            ProtectorId.Buffer = (PWSTR)Control->Argument.Buffer;
            ProtectorId.Length = (USHORT)(Control->Argument.Length * sizeof(WCHAR));
            ProtectorId.MaximumLength = ProtectorId.Length;
            Status = RtlGUIDFromString(&ProtectorId, &ProtectorGuid);
            Hr = NT_SUCCESS(Status) ?
                     Sys_FveDeleteProtector(VolumeHandle, &ProtectorGuid) :
                     HRESULT_FROM_NT(Status);
        }
    }
    CloseHr = FveCloseVolume(VolumeHandle);
    if (SUCCEEDED(Hr) && FAILED(CloseHr))
    {
        Hr = CloseHr;
    }
    return ZpStatus_FromCode(ZpStatusHResult, Hr);
}
