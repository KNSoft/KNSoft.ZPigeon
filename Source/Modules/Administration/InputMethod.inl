#include <lmcons.h>
#include <msctf.h>

#pragma comment(lib, "Advapi32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Uuid.lib")

// input.dll publishes these flags without declaring them in a Windows SDK header.
#define ZP_INPUT_METHOD_LOT_DEFAULT 0x00000001
#define ZP_INPUT_METHOD_LOT_DISABLED 0x00000002
#define ZP_INPUT_METHOD_ILOT_DEFPROFILE 0x00000002
#define ZP_INPUT_METHOD_ILOT_DISABLED 0x00000080

typedef struct _ZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE
{
    DWORD ProfileType;
    LANGID LanguageId;
    CLSID ClassId;
    GUID ProfileId;
    GUID CategoryId;
    DWORD SubstituteLayout;
    DWORD Flags;
    WCHAR Identity[MAX_PATH];
} ZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE, *PZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE;

typedef const ZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE* PCZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE;

typedef struct _ZP_INPUT_METHOD_LAYOUT_OR_TIP
{
    DWORD Flags;
    WCHAR Identity[MAX_PATH];
    WCHAR Name[MAX_PATH];
} ZP_INPUT_METHOD_LAYOUT_OR_TIP, *PZP_INPUT_METHOD_LAYOUT_OR_TIP;

typedef UINT (CALLBACK *ZP_INPUT_METHOD_ENUM_ENABLED)(
    PCWSTR,
    PCWSTR,
    PCWSTR,
    PZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE,
    UINT);
typedef UINT (CALLBACK *ZP_INPUT_METHOD_ENUM_SETUP)(
    LANGID,
    PZP_INPUT_METHOD_LAYOUT_OR_TIP,
    UINT,
    DWORD);
typedef BOOL (CALLBACK *ZP_INPUT_METHOD_INSTALL)(PCWSTR, DWORD);

typedef struct _ZP_INPUT_METHOD_API
{
    HMODULE Module;
    ZP_INPUT_METHOD_ENUM_ENABLED EnumerateEnabled;
    ZP_INPUT_METHOD_ENUM_SETUP EnumerateSetup;
    ZP_INPUT_METHOD_INSTALL Install;
} ZP_INPUT_METHOD_API, *PZP_INPUT_METHOD_API;

typedef struct _ZP_INPUT_METHOD_PROFILE_LIST
{
    TF_INPUTPROCESSORPROFILE* Items;
    ULONG Count;
    ULONG Capacity;
} ZP_INPUT_METHOD_PROFILE_LIST, *PZP_INPUT_METHOD_PROFILE_LIST;

static
HRESULT
ZpInputMethod_LoadApi(
    _Out_ PZP_INPUT_METHOD_API Api)
{
    Api->Module = LoadLibraryExW(L"input.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Api->Module == NULL) return HRESULT_FROM_WIN32(GetLastError());
    Api->EnumerateEnabled = (ZP_INPUT_METHOD_ENUM_ENABLED)GetProcAddress(Api->Module,
                                                                         "EnumEnabledLayoutOrTip");
    Api->EnumerateSetup = (ZP_INPUT_METHOD_ENUM_SETUP)GetProcAddress(Api->Module, "EnumLayoutOrTipForSetup");
    Api->Install = (ZP_INPUT_METHOD_INSTALL)GetProcAddress(Api->Module, "InstallLayoutOrTip");
    if (Api->EnumerateEnabled == NULL || Api->EnumerateSetup == NULL || Api->Install == NULL)
    {
        FreeLibrary(Api->Module);
        Api->Module = NULL;
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    }
    return S_OK;
}

static
HRESULT
ZpInputMethod_OpenProfiles(
    _Out_ ITfInputProcessorProfiles** Profiles,
    _Out_ ITfInputProcessorProfileMgr** Manager,
    _Out_ PLOGICAL Uninitialize)
{
    ITfInputProcessorProfiles* LocalProfiles;
    ITfInputProcessorProfileMgr* LocalManager;
    HRESULT Result;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    *Uninitialize = SUCCEEDED(Result);
    if (FAILED(Result) && Result != RPC_E_CHANGED_MODE) return Result;
    Result = CoCreateInstance(&CLSID_TF_InputProcessorProfiles,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_ITfInputProcessorProfiles,
                              (PVOID*)&LocalProfiles);
    if (SUCCEEDED(Result))
    {
        Result = ITfInputProcessorProfiles_QueryInterface(LocalProfiles,
                                                          &IID_ITfInputProcessorProfileMgr,
                                                          (PVOID*)&LocalManager);
        if (SUCCEEDED(Result))
        {
            *Profiles = LocalProfiles;
            *Manager = LocalManager;
            return S_OK;
        }
        ITfInputProcessorProfiles_Release(LocalProfiles);
    }
    if (*Uninitialize) CoUninitialize();
    return Result;
}

static
NTSTATUS
ZpInputMethod_AddProfile(
    _Inout_ PZP_INPUT_METHOD_PROFILE_LIST Profiles,
    _In_ const TF_INPUTPROCESSORPROFILE* Profile)
{
    TF_INPUTPROCESSORPROFILE* Items;
    ULONG Capacity;

    if (Profiles->Count == ZP_CODEC_MAX_ELEMENT_COUNT) return STATUS_QUOTA_EXCEEDED;
    if (Profiles->Count == Profiles->Capacity)
    {
        Capacity = Profiles->Capacity == 0 ? 16 : min(Profiles->Capacity * 2, ZP_CODEC_MAX_ELEMENT_COUNT);
        Items = Mem_ReAlloc(Profiles->Items, (SIZE_T)Capacity * sizeof(*Items));
        if (Items == NULL) return STATUS_NO_MEMORY;
        Profiles->Items = Items;
        Profiles->Capacity = Capacity;
    }
    Profiles->Items[Profiles->Count++] = *Profile;
    return STATUS_SUCCESS;
}

static
HRESULT
ZpInputMethod_EnumerateProfiles(
    _In_ ITfInputProcessorProfileMgr* Manager,
    _Inout_ PZP_INPUT_METHOD_PROFILE_LIST Profiles)
{
    IEnumTfInputProcessorProfiles* Enumerator;
    TF_INPUTPROCESSORPROFILE Profile;
    ULONG Fetched;
    NTSTATUS Status;
    HRESULT Result;

    Result = ITfInputProcessorProfileMgr_EnumProfiles(Manager, 0, &Enumerator);
    if (FAILED(Result)) return Result;
    for (;;)
    {
        Result = IEnumTfInputProcessorProfiles_Next(Enumerator, 1, &Profile, &Fetched);
        if (Result == S_FALSE) break;
        if (FAILED(Result)) break;
        if (Fetched != 1)
        {
            Result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            break;
        }
        Status = ZpInputMethod_AddProfile(Profiles, &Profile);
        if (!NT_SUCCESS(Status))
        {
            Result = HRESULT_FROM_NT(Status);
            break;
        }
    }
    IEnumTfInputProcessorProfiles_Release(Enumerator);
    return Result == S_FALSE ? S_OK : Result;
}

static
HRESULT
ZpInputMethod_EnumerateEnabled(
    _In_ PZP_INPUT_METHOD_API Api,
    _Outptr_result_buffer_maybenull_(*Count) PZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE* Layouts,
    _Out_ PUINT Count)
{
    PZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE Items;
    UINT Capacity, Copied;

    Capacity = Api->EnumerateEnabled(NULL, NULL, NULL, NULL, 0);
    if (Capacity == 0)
    {
        *Layouts = NULL;
        *Count = 0;
        return S_OK;
    }
    if (Capacity > ZP_CODEC_MAX_ELEMENT_COUNT) return HRESULT_FROM_NT(STATUS_QUOTA_EXCEEDED);
    Items = Mem_Alloc((SIZE_T)Capacity * sizeof(*Items));
    if (Items == NULL) return E_OUTOFMEMORY;
    Copied = Api->EnumerateEnabled(NULL, NULL, NULL, Items, Capacity);
    if (Copied > Capacity)
    {
        Mem_Free(Items);
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    }
    *Layouts = Items;
    *Count = Copied;
    return S_OK;
}

static
TF_INPUTPROCESSORPROFILE*
ZpInputMethod_FindProfile(
    _In_ PZP_INPUT_METHOD_PROFILE_LIST Profiles,
    _In_ PCZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE Layout)
{
    TF_INPUTPROCESSORPROFILE* Candidate = NULL;
    DWORD KeyboardLayout;
    ULONG Index;

    for (Index = 0; Index < Profiles->Count; Index++)
    {
        TF_INPUTPROCESSORPROFILE* Profile = &Profiles->Items[Index];

        if (Profile->dwProfileType != Layout->ProfileType || Profile->langid != Layout->LanguageId) continue;
        if (Layout->ProfileType == TF_PROFILETYPE_INPUTPROCESSOR)
        {
            if (IsEqualGUID(&Profile->clsid, &Layout->ClassId) &&
                IsEqualGUID(&Profile->guidProfile, &Layout->ProfileId))
            {
                return Profile;
            }
        }
        else if (Layout->ProfileType == TF_PROFILETYPE_KEYBOARDLAYOUT)
        {
            KeyboardLayout = HandleToULong(Profile->hkl);
            if (Layout->SubstituteLayout != 0 && KeyboardLayout == Layout->SubstituteLayout) return Profile;
            if (Candidate != NULL) return NULL;
            Candidate = Profile;
        }
    }
    return Candidate;
}

static
HRESULT
ZpInputMethod_QueryKeyboardLayout(
    _In_ PZP_INPUT_METHOD_API Api,
    _In_ LANGID LanguageId,
    _In_ PCWSTR Identity,
    _Out_writes_(MAX_PATH) PWSTR Name)
{
    PZP_INPUT_METHOD_LAYOUT_OR_TIP Layouts;
    UINT Count, Copied, Index;
    HRESULT Result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);

    Count = Api->EnumerateSetup(LanguageId, NULL, 0, 0);
    if (Count == 0) return Result;
    if (Count > ZP_CODEC_MAX_ELEMENT_COUNT) return HRESULT_FROM_NT(STATUS_QUOTA_EXCEEDED);
    Layouts = Mem_Alloc((SIZE_T)Count * sizeof(*Layouts));
    if (Layouts == NULL) return E_OUTOFMEMORY;
    Copied = Api->EnumerateSetup(LanguageId, Layouts, Count, 0);
    if (Copied > Count)
    {
        Result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        goto Cleanup;
    }
    for (Index = 0; Index < Copied; Index++)
    {
        if (wcsnlen(Layouts[Index].Identity, ARRAYSIZE(Layouts[Index].Identity)) ==
                ARRAYSIZE(Layouts[Index].Identity) ||
            wcsnlen(Layouts[Index].Name, ARRAYSIZE(Layouts[Index].Name)) == ARRAYSIZE(Layouts[Index].Name))
        {
            Result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            break;
        }
        if (CompareStringOrdinal(Layouts[Index].Identity, -1, Identity, -1, TRUE) == CSTR_EQUAL)
        {
            RtlCopyMemory(Name, Layouts[Index].Name, (wcslen(Layouts[Index].Name) + 1) * sizeof(WCHAR));
            Result = S_OK;
            break;
        }
    }
Cleanup:
    Mem_Free(Layouts);
    return Result;
}

static
HRESULT
ZpInputMethod_QueryLanguage(
    _In_ LANGID LanguageId,
    _Out_writes_(LOCALE_NAME_MAX_LENGTH) PWSTR LocaleName,
    _Outptr_result_z_ PWSTR* DisplayName)
{
    PWSTR Name;
    INT Length;

    if (LCIDToLocaleName(MAKELCID(LanguageId, SORT_DEFAULT),
                         LocaleName,
                         LOCALE_NAME_MAX_LENGTH,
                         LOCALE_ALLOW_NEUTRAL_NAMES) == 0)
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    Length = GetLocaleInfoEx(LocaleName, LOCALE_SLOCALIZEDDISPLAYNAME, NULL, 0);
    if (Length == 0) return HRESULT_FROM_WIN32(GetLastError());
    Name = Mem_Alloc((SIZE_T)Length * sizeof(WCHAR));
    if (Name == NULL) return E_OUTOFMEMORY;
    if (GetLocaleInfoEx(LocaleName, LOCALE_SLOCALIZEDDISPLAYNAME, Name, Length) == 0)
    {
        HRESULT Result = HRESULT_FROM_WIN32(GetLastError());

        Mem_Free(Name);
        return Result;
    }
    *DisplayName = Name;
    return S_OK;
}

static
ZP_STATUS
ZpInputMethod_AddContext(
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    WCHAR Account[UNLEN + 1];
    USEROBJECTFLAGS ObjectFlags;
    DWORD AccountLength = ARRAYSIZE(Account), Length, SessionId;
    ULONG Flags;
    NTSTATUS Status;

    if (!GetUserNameW(Account, &AccountLength)) return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &SessionId))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (!GetUserObjectInformationW(GetProcessWindowStation(),
                                   UOI_FLAGS,
                                   &ObjectFlags,
                                   sizeof(ObjectFlags),
                                   &Length))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    Flags = BooleanFlagOn(ObjectFlags.dwFlags, WSF_VISIBLE) ?
                ZP_ADMINISTRATION_INPUT_METHOD_CONTEXT_FLAG_INTERACTIVE :
                0;
    Status = ZpAdministration_AddRecord(Builder,
                                         ZpAdministrationKindInputMethodContext,
                                         SessionId,
                                         Flags,
                                         0,
                                         L"current",
                                         Account,
                                         NULL,
                                         NULL);
    return ZpStatus_FromNtStatus(Status);
}

static
HRESULT
ZpInputMethod_AddLanguageRecords(
    _In_ ITfInputProcessorProfiles* ProfileManager,
    _In_ PZP_INPUT_METHOD_API Api,
    _In_ PZP_INPUT_METHOD_PROFILE_LIST Profiles,
    _In_reads_(LayoutCount) PCZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE Layouts,
    _In_ UINT LayoutCount,
    _In_ UINT First,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    WCHAR LocaleName[LOCALE_NAME_MAX_LENGTH], KeyboardName[MAX_PATH];
    PWSTR LanguageName;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT Index;

    Result = ZpInputMethod_QueryLanguage(Layouts[First].LanguageId, LocaleName, &LanguageName);
    if (FAILED(Result)) return Result;
    for (Index = First; NT_SUCCESS(Status) && Index < LayoutCount; Index++)
    {
        TF_INPUTPROCESSORPROFILE* Profile;
        BSTR InputProcessorName = NULL;
        PCWSTR Name;
        ULONG Flags = 0;

        if (Layouts[Index].LanguageId != Layouts[First].LanguageId) continue;
        if (wcsnlen(Layouts[Index].Identity, ARRAYSIZE(Layouts[Index].Identity)) ==
            ARRAYSIZE(Layouts[Index].Identity))
        {
            Result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            break;
        }
        Profile = ZpInputMethod_FindProfile(Profiles, &Layouts[Index]);
        if (!BooleanFlagOn(Layouts[Index].Flags, ZP_INPUT_METHOD_LOT_DISABLED))
            Flags |= ZP_ADMINISTRATION_INPUT_METHOD_FLAG_ENABLED;
        if (Profile != NULL && BooleanFlagOn(Profile->dwFlags, TF_IPP_FLAG_ACTIVE))
            Flags |= ZP_ADMINISTRATION_INPUT_METHOD_FLAG_ACTIVE;
        if (BooleanFlagOn(Layouts[Index].Flags, ZP_INPUT_METHOD_LOT_DEFAULT))
            Flags |= ZP_ADMINISTRATION_INPUT_METHOD_FLAG_DEFAULT;
        if (Layouts[Index].ProfileType == TF_PROFILETYPE_INPUTPROCESSOR)
        {
            Flags |= ZP_ADMINISTRATION_INPUT_METHOD_FLAG_INPUT_PROCESSOR;
            Result = ITfInputProcessorProfiles_GetLanguageProfileDescription(ProfileManager,
                                                                              &Layouts[Index].ClassId,
                                                                              Layouts[Index].LanguageId,
                                                                              &Layouts[Index].ProfileId,
                                                                              &InputProcessorName);
            Name = InputProcessorName;
        }
        else
        {
            Result = ZpInputMethod_QueryKeyboardLayout(Api,
                                                       Layouts[Index].LanguageId,
                                                       Layouts[Index].Identity,
                                                       KeyboardName);
            Name = KeyboardName;
        }
        if (SUCCEEDED(Result))
        {
            Status = ZpAdministration_AddRecord(Builder,
                                                 ZpAdministrationKindInputMethod,
                                                 0,
                                                 Flags,
                                                 0,
                                                 Layouts[Index].Identity,
                                                 Name,
                                                 LanguageName,
                                                 LocaleName);
        }
        SysFreeString(InputProcessorName);
        if (FAILED(Result)) break;
    }
    Mem_Free(LanguageName);
    return NT_SUCCESS(Status) ? Result : HRESULT_FROM_NT(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateInputMethods(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_INPUT_METHOD_PROFILE_LIST Profiles = { 0 };
    PZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE Layouts = NULL;
    ITfInputProcessorProfiles* ProfileManager;
    ITfInputProcessorProfileMgr* Manager;
    ZP_INPUT_METHOD_API Api = { 0 };
    ZP_STATUS OperationStatus;
    LOGICAL Uninitialize;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT LayoutCount = 0, Index, Previous;

    OperationStatus = ZpInputMethod_AddContext(&Builder);
    if (!ZpStatus_IsSuccess(OperationStatus)) goto CleanupBuilder;
    Result = ZpInputMethod_OpenProfiles(&ProfileManager, &Manager, &Uninitialize);
    if (FAILED(Result))
    {
        OperationStatus = ZpStatus_FromCode(ZpStatusHResult, Result);
        goto CleanupBuilder;
    }
    Result = ZpInputMethod_LoadApi(&Api);
    if (SUCCEEDED(Result)) Result = ZpInputMethod_EnumerateProfiles(Manager, &Profiles);
    if (SUCCEEDED(Result)) Result = ZpInputMethod_EnumerateEnabled(&Api, &Layouts, &LayoutCount);
    for (Index = 0; SUCCEEDED(Result) && Index < LayoutCount; Index++)
    {
        for (Previous = 0; Previous < Index; Previous++)
        {
            if (Layouts[Previous].LanguageId == Layouts[Index].LanguageId) break;
        }
        if (Previous == Index)
        {
            Result = ZpInputMethod_AddLanguageRecords(ProfileManager,
                                                      &Api,
                                                      &Profiles,
                                                      Layouts,
                                                      LayoutCount,
                                                      Index,
                                                      &Builder);
        }
    }
    if (SUCCEEDED(Result)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    Mem_Free(Layouts);
    Mem_Free(Profiles.Items);
    if (Api.Module != NULL) FreeLibrary(Api.Module);
    ITfInputProcessorProfileMgr_Release(Manager);
    ITfInputProcessorProfiles_Release(ProfileManager);
    if (Uninitialize) CoUninitialize();
    OperationStatus = FAILED(Result) ?
                          ZpStatus_FromCode(ZpStatusHResult, Result) :
                          ZpStatus_FromNtStatus(Status);
CleanupBuilder:
    ZpAdministration_FreeBuilder(&Builder);
    return OperationStatus;
}

static
HRESULT
ZpInputMethod_InstallProfile(
    _In_ PZP_INPUT_METHOD_API Api,
    _In_ PCWSTR Identity,
    _In_ DWORD Flags)
{
    DWORD Error;

    SetLastError(ERROR_SUCCESS);
    if (Api->Install(Identity, Flags)) return S_OK;
    Error = GetLastError();
    return Error == ERROR_SUCCESS ? E_FAIL : HRESULT_FROM_WIN32(Error);
}

static
HRESULT
ZpInputMethod_QueryInteractive(
    _Out_ PLOGICAL Interactive)
{
    USEROBJECTFLAGS ObjectFlags;
    DWORD Length;

    if (!GetUserObjectInformationW(GetProcessWindowStation(),
                                   UOI_FLAGS,
                                   &ObjectFlags,
                                   sizeof(ObjectFlags),
                                   &Length))
    {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    *Interactive = BooleanFlagOn(ObjectFlags.dwFlags, WSF_VISIBLE);
    return S_OK;
}

static
ZP_STATUS
ZpAdministration_ControlInputMethod(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    ZP_INPUT_METHOD_PROFILE_LIST Profiles = { 0 };
    PZP_INPUT_METHOD_LAYOUT_OR_TIP_PROFILE Layouts = NULL, Layout = NULL;
    TF_INPUTPROCESSORPROFILE* Profile;
    ITfInputProcessorProfiles* ProfileManager;
    ITfInputProcessorProfileMgr* Manager;
    ZP_INPUT_METHOD_API Api = { 0 };
    PWSTR Identity;
    LOGICAL Interactive, Uninitialize;
    HRESULT Result;
    UINT LayoutCount = 0, Index, EnabledCount = 0;

    if (Control->Identity.Length == 0 ||
        Control->Identity.Length >= MAX_PATH ||
        Control->Argument.Length != 0 ||
        Control->Secret.Length != 0)
    {
        return ZpStatus_FromNtStatus(STATUS_INVALID_PARAMETER);
    }
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = ZpInputMethod_OpenProfiles(&ProfileManager, &Manager, &Uninitialize);
    if (FAILED(Result)) goto CleanupIdentity;
    Result = ZpInputMethod_LoadApi(&Api);
    if (SUCCEEDED(Result)) Result = ZpInputMethod_EnumerateProfiles(Manager, &Profiles);
    if (SUCCEEDED(Result)) Result = ZpInputMethod_EnumerateEnabled(&Api, &Layouts, &LayoutCount);
    for (Index = 0; SUCCEEDED(Result) && Index < LayoutCount; Index++)
    {
        if (wcsnlen(Layouts[Index].Identity, ARRAYSIZE(Layouts[Index].Identity)) ==
            ARRAYSIZE(Layouts[Index].Identity))
        {
            Result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            break;
        }
        if (!BooleanFlagOn(Layouts[Index].Flags, ZP_INPUT_METHOD_LOT_DISABLED)) EnabledCount++;
        if (CompareStringOrdinal(Layouts[Index].Identity, -1, Identity, -1, TRUE) == CSTR_EQUAL)
            Layout = &Layouts[Index];
    }
    if (SUCCEEDED(Result) && Layout == NULL) Result = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    Profile = SUCCEEDED(Result) ? ZpInputMethod_FindProfile(&Profiles, Layout) : NULL;
    if (SUCCEEDED(Result))
    {
        switch (Control->Action)
        {
            case ZpAdministrationActionEnable:
                Result = !BooleanFlagOn(Layout->Flags, ZP_INPUT_METHOD_LOT_DISABLED) ?
                             S_OK :
                             ZpInputMethod_InstallProfile(&Api, Layout->Identity, 0);
                break;
            case ZpAdministrationActionDisable:
                if (BooleanFlagOn(Layout->Flags, ZP_INPUT_METHOD_LOT_DISABLED)) Result = S_OK;
                else if (Profile == NULL) Result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                else if (BooleanFlagOn(Profile->dwFlags, TF_IPP_FLAG_ACTIVE))
                    Result = HRESULT_FROM_WIN32(ERROR_BUSY);
                else if (EnabledCount <= 1)
                    Result = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
                else Result = ZpInputMethod_InstallProfile(&Api,
                                                           Layout->Identity,
                                                           ZP_INPUT_METHOD_ILOT_DISABLED);
                break;
            case ZpAdministrationActionActivate:
                if (BooleanFlagOn(Layout->Flags, ZP_INPUT_METHOD_LOT_DISABLED))
                    Result = HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
                else if (Profile == NULL) Result = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                else
                {
                    Result = ZpInputMethod_QueryInteractive(&Interactive);
                    if (SUCCEEDED(Result) && !Interactive)
                        Result = HRESULT_FROM_WIN32(ERROR_REQUIRES_INTERACTIVE_WINDOWSTATION);
                    if (SUCCEEDED(Result))
                    {
                        Result = ITfInputProcessorProfileMgr_ActivateProfile(
                            Manager,
                            Profile->dwProfileType,
                            Profile->langid,
                            &Profile->clsid,
                            &Profile->guidProfile,
                            Profile->hkl,
                            TF_IPPMF_FORSESSION | TF_IPPMF_DONTCARECURRENTINPUTLANGUAGE);
                    }
                }
                break;
            case ZpAdministrationActionSetDefault:
                Result = !BooleanFlagOn(Layout->Flags, ZP_INPUT_METHOD_LOT_DISABLED) ?
                             ZpInputMethod_InstallProfile(&Api,
                                                          Layout->Identity,
                                                          ZP_INPUT_METHOD_ILOT_DEFPROFILE) :
                             HRESULT_FROM_WIN32(ERROR_INVALID_STATE);
                break;
            default:
                Result = E_NOTIMPL;
                break;
        }
    }
    Mem_Free(Layouts);
    Mem_Free(Profiles.Items);
    if (Api.Module != NULL) FreeLibrary(Api.Module);
    ITfInputProcessorProfileMgr_Release(Manager);
    ITfInputProcessorProfiles_Release(ProfileManager);
    if (Uninitialize) CoUninitialize();
CleanupIdentity:
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}
