#include <KNSoft/MakeLifeEasier/System/Registry.h>

typedef UINT ZP_DISM_SESSION;

#define ZP_DISM_ONLINE_IMAGE L"DISM_{53BFAE52-B167-4E2F-A258-0A37B57FF845}"
#define ZpDismPackageNone 0

typedef struct _ZP_DISM_FEATURE
{
    PCWSTR FeatureName;
    ULONG State;
} ZP_DISM_FEATURE, *PZP_DISM_FEATURE;

typedef struct _ZP_DISM_FEATURE_INFO
{
    PCWSTR FeatureName;
    ULONG State;
    PCWSTR DisplayName;
    PCWSTR Description;
    ULONG RestartRequired;
    PVOID CustomProperty;
    UINT CustomPropertyCount;
} ZP_DISM_FEATURE_INFO, *PZP_DISM_FEATURE_INFO;

C_ASSERT(sizeof(ZP_DISM_FEATURE) == 16);
C_ASSERT(FIELD_OFFSET(ZP_DISM_FEATURE_INFO, DisplayName) == 16);
C_ASSERT(FIELD_OFFSET(ZP_DISM_FEATURE_INFO, CustomProperty) == 40);
C_ASSERT(sizeof(ZP_DISM_FEATURE_INFO) == 56);

typedef HRESULT (WINAPI *ZP_DISM_INITIALIZE)(ULONG, PCWSTR, PCWSTR);
typedef HRESULT (WINAPI *ZP_DISM_SHUTDOWN)(VOID);
typedef HRESULT (WINAPI *ZP_DISM_OPEN_SESSION)(PCWSTR, PCWSTR, PCWSTR, ZP_DISM_SESSION*);
typedef HRESULT (WINAPI *ZP_DISM_CLOSE_SESSION)(ZP_DISM_SESSION);
typedef HRESULT (WINAPI *ZP_DISM_GET_FEATURES)(ZP_DISM_SESSION, PCWSTR, ULONG, PZP_DISM_FEATURE*, PUINT);
typedef HRESULT (WINAPI *ZP_DISM_GET_FEATURE_INFO)(
    ZP_DISM_SESSION,
    PCWSTR,
    PCWSTR,
    ULONG,
    PZP_DISM_FEATURE_INFO*);
typedef HRESULT (WINAPI *ZP_DISM_GET_FEATURE_PARENT)(
    ZP_DISM_SESSION,
    PCWSTR,
    PCWSTR,
    ULONG,
    PZP_DISM_FEATURE*,
    PUINT);
typedef HRESULT (WINAPI *ZP_DISM_ENABLE_FEATURE)(
    ZP_DISM_SESSION,
    PCWSTR,
    PCWSTR,
    ULONG,
    BOOL,
    PCWSTR*,
    UINT,
    BOOL,
    HANDLE,
    PVOID,
    PVOID);
typedef HRESULT (WINAPI *ZP_DISM_DISABLE_FEATURE)(
    ZP_DISM_SESSION,
    PCWSTR,
    PCWSTR,
    BOOL,
    HANDLE,
    PVOID,
    PVOID);
typedef HRESULT (WINAPI *ZP_DISM_DELETE)(PVOID);

typedef struct _ZP_DISM_API
{
    HMODULE Module;
    ZP_DISM_INITIALIZE Initialize;
    ZP_DISM_SHUTDOWN Shutdown;
    ZP_DISM_OPEN_SESSION OpenSession;
    ZP_DISM_CLOSE_SESSION CloseSession;
    ZP_DISM_GET_FEATURES GetFeatures;
    ZP_DISM_GET_FEATURE_INFO GetFeatureInfo;
    ZP_DISM_GET_FEATURE_PARENT GetFeatureParent;
    ZP_DISM_ENABLE_FEATURE EnableFeature;
    ZP_DISM_DISABLE_FEATURE DisableFeature;
    ZP_DISM_DELETE Delete;
} ZP_DISM_API, *PZP_DISM_API;

static SRWLOCK ZpDismLock = SRWLOCK_INIT;

static
HRESULT
ZpDism_Load(
    _Out_ PZP_DISM_API Api)
{
#define ZP_DISM_LOAD(Name, Type) \
    Api->Name = (Type)GetProcAddress(Api->Module, "Dism" #Name); \
    if (Api->Name == NULL) goto Error
    Api->Module = LoadLibraryExW(L"DismApi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (Api->Module == NULL) return HRESULT_FROM_WIN32(GetLastError());
    ZP_DISM_LOAD(Initialize, ZP_DISM_INITIALIZE);
    ZP_DISM_LOAD(Shutdown, ZP_DISM_SHUTDOWN);
    ZP_DISM_LOAD(OpenSession, ZP_DISM_OPEN_SESSION);
    ZP_DISM_LOAD(CloseSession, ZP_DISM_CLOSE_SESSION);
    ZP_DISM_LOAD(GetFeatures, ZP_DISM_GET_FEATURES);
    ZP_DISM_LOAD(GetFeatureInfo, ZP_DISM_GET_FEATURE_INFO);
    ZP_DISM_LOAD(GetFeatureParent, ZP_DISM_GET_FEATURE_PARENT);
    ZP_DISM_LOAD(EnableFeature, ZP_DISM_ENABLE_FEATURE);
    ZP_DISM_LOAD(DisableFeature, ZP_DISM_DISABLE_FEATURE);
    ZP_DISM_LOAD(Delete, ZP_DISM_DELETE);
#undef ZP_DISM_LOAD
    return S_OK;

Error:
    FreeLibrary(Api->Module);
    return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
}

static
HRESULT
ZpDism_Open(
    _Out_ PZP_DISM_API Api,
    _Out_ ZP_DISM_SESSION* Session)
{
    HRESULT Result;

    Result = ZpDism_Load(Api);
    if (FAILED(Result)) return Result;
    Result = Api->Initialize(0, NULL, NULL);
    if (SUCCEEDED(Result))
    {
        Result = Api->OpenSession(ZP_DISM_ONLINE_IMAGE, NULL, NULL, Session);
        if (FAILED(Result)) Api->Shutdown();
    }
    if (FAILED(Result)) FreeLibrary(Api->Module);
    return Result;
}

static
HRESULT
ZpDism_Close(
    _In_ PZP_DISM_API Api,
    _In_ ZP_DISM_SESSION Session,
    _In_ HRESULT Result)
{
    HRESULT Cleanup;

    Cleanup = Api->CloseSession(Session);
    if (SUCCEEDED(Result) && FAILED(Cleanup)) Result = Cleanup;
    Cleanup = Api->Shutdown();
    if (SUCCEEDED(Result) && FAILED(Cleanup)) Result = Cleanup;
    FreeLibrary(Api->Module);
    return Result;
}

static
PWSTR
ZpAdministration_QueryRegistryString(
    _In_ HANDLE Key,
    _In_ PCUNICODE_STRING Name)
{
    PKEY_VALUE_PARTIAL_INFORMATION Data;
    PWSTR Value;
    ULONG Length;

    if (!NT_SUCCESS(Sys_RegQueryData(Key, Name, &Data))) return NULL;
    if ((Data->Type != REG_SZ && Data->Type != REG_EXPAND_SZ) || Data->DataLength % sizeof(WCHAR) != 0)
    {
        Mem_Free(Data);
        return NULL;
    }
    Length = Data->DataLength / sizeof(WCHAR);
    while (Length != 0 && ((PCWCHAR)Data->Data)[Length - 1] == UNICODE_NULL) Length--;
    Value = Mem_Alloc(((SIZE_T)Length + 1) * sizeof(WCHAR));
    if (Value != NULL)
    {
        RtlCopyMemory(Value, Data->Data, (SIZE_T)Length * sizeof(WCHAR));
        Value[Length] = UNICODE_NULL;
    }
    Mem_Free(Data);
    return Value;
}

static
NTSTATUS
ZpAdministration_EnumerateSoftwareKey(
    _In_ HANDLE Root,
    _In_ PCUNICODE_STRING Path,
    _In_ ZP_ADMINISTRATION_KIND Kind,
    _In_ ULONG Flags,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    static const UNICODE_STRING DisplayName = RTL_CONSTANT_STRING(L"DisplayName");
    static const UNICODE_STRING Publisher = RTL_CONSTANT_STRING(L"Publisher");
    static const UNICODE_STRING DisplayVersion = RTL_CONSTANT_STRING(L"DisplayVersion");
    KEY_CACHED_INFORMATION Cached;
    PKEY_BASIC_INFORMATION Information;
    UNICODE_STRING Name;
    HANDLE Key, Child;
    PWSTR Identity, Title, Description, Detail;
    ULONG Index, Length, InformationLength;
    NTSTATUS Status, ChildStatus;

    Status = Sys_RegOpenKeyEx(&Key,
                              Root,
                              KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS,
                              Path);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND) return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status)) return Status;
    Status = NtQueryKey(Key, KeyCachedInformation, &Cached, sizeof(Cached), &Length);
    if (!NT_SUCCESS(Status)) goto CleanupKey;
    InformationLength = FIELD_OFFSET(KEY_BASIC_INFORMATION, Name) + Cached.MaxNameLength;
    Information = Mem_Alloc(InformationLength);
    if (Information == NULL)
    {
        Status = STATUS_NO_MEMORY;
        goto CleanupKey;
    }
    for (Index = 0; NT_SUCCESS(Status); Index++)
    {
        Status = NtEnumerateKey(Key, Index, KeyBasicInformation, Information, InformationLength, &Length);
        if (Status == STATUS_NO_MORE_ENTRIES)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!NT_SUCCESS(Status)) break;
        Name.Buffer = Information->Name;
        Name.Length = (USHORT)Information->NameLength;
        Name.MaximumLength = Name.Length;
        ChildStatus = Sys_RegOpenKeyEx(&Child, Key, KEY_QUERY_VALUE, &Name);
        if (!NT_SUCCESS(ChildStatus)) continue;
        Identity = Mem_Alloc((SIZE_T)Name.Length + sizeof(WCHAR));
        if (Identity == NULL)
        {
            NtClose(Child);
            Status = STATUS_NO_MEMORY;
            break;
        }
        RtlCopyMemory(Identity, Name.Buffer, Name.Length);
        Identity[Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
        Title = Kind == ZpAdministrationKindWindowsApp ? NULL :
                    ZpAdministration_QueryRegistryString(Child, &DisplayName);
        Description = ZpAdministration_QueryRegistryString(Child, &Publisher);
        Detail = ZpAdministration_QueryRegistryString(Child, &DisplayVersion);
        if (Kind == ZpAdministrationKindWindowsApp || Title != NULL)
        {
            Status = ZpAdministration_AddRecord(Builder,
                                                 Kind,
                                                 0,
                                                 Flags,
                                                 0,
                                                 Identity,
                                                 Title != NULL ? Title : Identity,
                                                 Description,
                                                 Detail);
        }
        Mem_Free(Detail);
        Mem_Free(Description);
        Mem_Free(Title);
        Mem_Free(Identity);
        NtClose(Child);
    }
    Mem_Free(Information);
CleanupKey:
    NtClose(Key);
    return Status;
}

static
ZP_STATUS
ZpAdministration_EnumerateSoftware(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    static const UNICODE_STRING MachineUninstall =
        RTL_CONSTANT_STRING(L"\\Registry\\Machine\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    static const UNICODE_STRING UserUninstall =
        RTL_CONSTANT_STRING(L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    static const UNICODE_STRING MachineWow64Uninstall = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall");
    static const UNICODE_STRING WindowsApps = RTL_CONSTANT_STRING(
        L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\"
        L"AppModel\\Repository\\Packages");
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    HANDLE CurrentUser = NULL;
    NTSTATUS Status;

    Status = ZpAdministration_EnumerateSoftwareKey(NULL,
                                                   &MachineUninstall,
                                                   ZpAdministrationKindDesktopProgram,
                                                   1,
                                                   &Builder);
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EnumerateSoftwareKey(NULL,
                                                       &MachineWow64Uninstall,
                                                       ZpAdministrationKindDesktopProgram,
                                                       3,
                                                       &Builder);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtlOpenCurrentUser(KEY_ENUMERATE_SUB_KEYS, &CurrentUser);
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EnumerateSoftwareKey(CurrentUser,
                                                       &UserUninstall,
                                                       ZpAdministrationKindDesktopProgram,
                                                       2,
                                                       &Builder);
        if (NT_SUCCESS(Status))
        {
            Status = ZpAdministration_EnumerateSoftwareKey(CurrentUser,
                                                           &WindowsApps,
                                                           ZpAdministrationKindWindowsApp,
                                                           2,
                                                           &Builder);
        }
        NtClose(CurrentUser);
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_EnumerateFeatures(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    PZP_DISM_FEATURE Features = NULL;
    ZP_DISM_SESSION Session;
    ZP_DISM_API Api;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;
    UINT Count = 0, Index;

    AcquireSRWLockExclusive(&ZpDismLock);
    Result = ZpDism_Open(&Api, &Session);
    if (SUCCEEDED(Result))
    {
        Result = Api.GetFeatures(Session, NULL, ZpDismPackageNone, &Features, &Count);
        for (Index = 0; SUCCEEDED(Result) && NT_SUCCESS(Status) && Index < Count; Index++)
        {
            PZP_DISM_FEATURE_INFO Info;
            PZP_DISM_FEATURE Parents;
            UINT ParentCount;

            Result = Api.GetFeatureInfo(Session,
                                        Features[Index].FeatureName,
                                        NULL,
                                        ZpDismPackageNone,
                                        &Info);
            if (FAILED(Result)) break;
            Result = Api.GetFeatureParent(Session,
                                          Features[Index].FeatureName,
                                          NULL,
                                          ZpDismPackageNone,
                                          &Parents,
                                          &ParentCount);
            if (SUCCEEDED(Result))
            {
                Status = ZpAdministration_AddRecord(&Builder,
                                                     ZpAdministrationKindWindowsFeature,
                                                     Features[Index].State,
                                                     Info->RestartRequired,
                                                     0,
                                                     Features[Index].FeatureName,
                                                     Info->DisplayName,
                                                     Info->Description,
                                                     ParentCount != 0 ? Parents[0].FeatureName : NULL);
                Api.Delete(Parents);
            }
            Api.Delete(Info);
        }
        Api.Delete(Features);
        Result = ZpDism_Close(&Api, Session, Result);
    }
    ReleaseSRWLockExclusive(&ZpDismLock);
    if (FAILED(Result))
    {
        ZpAdministration_FreeBuilder(&Builder);
        return ZpStatus_FromCode(ZpStatusHResult, Result);
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}

static
ZP_STATUS
ZpAdministration_ControlFeature(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    ZP_DISM_SESSION Session;
    ZP_DISM_API Api;
    PWSTR Identity;
    HRESULT Result;

    if (Control->Action != ZpAdministrationActionEnable &&
        Control->Action != ZpAdministrationActionDisable)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    AcquireSRWLockExclusive(&ZpDismLock);
    Result = ZpDism_Open(&Api, &Session);
    if (SUCCEEDED(Result))
    {
        Result = Control->Action == ZpAdministrationActionEnable ?
                     Api.EnableFeature(Session,
                                       Identity,
                                       NULL,
                                       ZpDismPackageNone,
                                       FALSE,
                                       NULL,
                                       0,
                                       TRUE,
                                       NULL,
                                       NULL,
                                       NULL) :
                     Api.DisableFeature(Session, Identity, NULL, FALSE, NULL, NULL, NULL);
        Result = ZpDism_Close(&Api, Session, Result);
    }
    ReleaseSRWLockExclusive(&ZpDismLock);
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}

static
ZP_STATUS
ZpAdministration_ControlSoftware(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    UNREFERENCED_PARAMETER(Control);
    return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
}
