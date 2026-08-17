#define COBJMACROS
#include <taskschd.h>
#include <stdio.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Taskschd.lib")

static ZP_STATUS ZpAdministration_EnumerateTasksImpl(PBYTE*, PULONG);
static ZP_STATUS ZpAdministration_ControlTaskImpl(PCZP_ADMINISTRATION_CONTROL_VIEW);

ZP_STATUS
ZpAdministration_EnumerateTasks(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    return ZpAdministration_EnumerateTasksImpl(Response, ResponseLength);
}

static
ZP_STATUS
ZpAdministration_ControlTask(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    return ZpAdministration_ControlTaskImpl(Control);
}

static
HRESULT
ZpAdministration_OpenTaskService(
    _Out_ ITaskService** Service,
    _Out_ PLOGICAL Uninitialize)
{
    VARIANT Empty;
    HRESULT Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);

    *Uninitialize = SUCCEEDED(Result);
    if (FAILED(Result) && Result != RPC_E_CHANGED_MODE) return Result;
    Result = CoCreateInstance(&CLSID_TaskScheduler,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_ITaskService,
                              (PVOID*)Service);
    if (FAILED(Result)) return Result;
    VariantInit(&Empty);
    Result = ITaskService_Connect(*Service, Empty, Empty, Empty, Empty);
    if (FAILED(Result)) ITaskService_Release(*Service);
    return Result;
}

static
HRESULT
ZpAdministration_AddTask(
    _In_ IRegisteredTask* Task,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    VARIANT_BOOL Enabled;
    TASK_STATE State;
    BSTR Path = NULL, Name = NULL, Xml = NULL;
    DATE NextRun;
    LONG LastResult;
    WCHAR ResultText[16];
    HRESULT Result;
    NTSTATUS Status;

    Result = IRegisteredTask_get_Path(Task, &Path);
    if (SUCCEEDED(Result)) Result = IRegisteredTask_get_Name(Task, &Name);
    if (SUCCEEDED(Result)) Result = IRegisteredTask_get_State(Task, &State);
    if (SUCCEEDED(Result)) Result = IRegisteredTask_get_Enabled(Task, &Enabled);
    if (SUCCEEDED(Result)) Result = IRegisteredTask_get_NextRunTime(Task, &NextRun);
    if (SUCCEEDED(Result)) Result = IRegisteredTask_get_LastTaskResult(Task, &LastResult);
    if (SUCCEEDED(Result)) Result = IRegisteredTask_get_Xml(Task, &Xml);
    if (SUCCEEDED(Result))
    {
        _snwprintf_s(ResultText, ARRAYSIZE(ResultText), _TRUNCATE, L"0x%08lX", LastResult);
        Status = ZpAdministration_AddRecord(Builder,
                                             ZpAdministrationKindTask,
                                             State,
                                             Enabled == VARIANT_TRUE ? 1 : 0,
                                             ZpAdministration_DateToFileTime(NextRun),
                                             Path,
                                             Name,
                                             ResultText,
                                             Xml);
        if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    SysFreeString(Xml);
    SysFreeString(Name);
    SysFreeString(Path);
    return Result;
}

static
HRESULT
ZpAdministration_EnumerateTaskFolder(
    _In_ ITaskFolder* Folder,
    _In_ ULONG Depth,
    _Inout_ PZP_ADMINISTRATION_BUILDER Builder)
{
    IRegisteredTaskCollection* Tasks;
    ITaskFolderCollection* Folders;
    IRegisteredTask* Task;
    ITaskFolder* Child;
    VARIANT Index;
    LONG Count;
    BSTR FolderPath = NULL;
    HRESULT Result;
    NTSTATUS Status;

    Result = ITaskFolder_get_Path(Folder, &FolderPath);
    if (SUCCEEDED(Result))
    {
        Status = ZpAdministration_AddRecord(Builder,
                                            ZpAdministrationKindTaskFolder,
                                            0,
                                            0,
                                            0,
                                            FolderPath,
                                            FolderPath,
                                            NULL,
                                            NULL);
        if (!NT_SUCCESS(Status)) Result = HRESULT_FROM_NT(Status);
    }
    SysFreeString(FolderPath);
    if (FAILED(Result)) return Result;

    Result = ITaskFolder_GetTasks(Folder, TASK_ENUM_HIDDEN, &Tasks);
    if (FAILED(Result)) return Result;
    Result = IRegisteredTaskCollection_get_Count(Tasks, &Count);
    VariantInit(&Index);
    Index.vt = VT_I4;
    for (Index.lVal = 1; SUCCEEDED(Result) && Index.lVal <= Count; Index.lVal++)
    {
        Result = IRegisteredTaskCollection_get_Item(Tasks, Index, &Task);
        if (SUCCEEDED(Result))
        {
            Result = ZpAdministration_AddTask(Task, Builder);
            IRegisteredTask_Release(Task);
        }
    }
    IRegisteredTaskCollection_Release(Tasks);
    if (FAILED(Result) || Depth == 32)
    {
        return FAILED(Result) ? Result : HRESULT_FROM_NT(STATUS_STACK_OVERFLOW);
    }
    Result = ITaskFolder_GetFolders(Folder, 0, &Folders);
    if (FAILED(Result)) return Result;
    Result = ITaskFolderCollection_get_Count(Folders, &Count);
    VariantInit(&Index);
    Index.vt = VT_I4;
    for (Index.lVal = 1; SUCCEEDED(Result) && Index.lVal <= Count; Index.lVal++)
    {
        Result = ITaskFolderCollection_get_Item(Folders, Index, &Child);
        if (SUCCEEDED(Result))
        {
            Result = ZpAdministration_EnumerateTaskFolder(Child, Depth + 1, Builder);
            ITaskFolder_Release(Child);
        }
    }
    ITaskFolderCollection_Release(Folders);
    return Result;
}

static
ZP_STATUS
ZpAdministration_EnumerateTasksImpl(
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ITaskService* Service;
    ITaskFolder* Root = NULL;
    BSTR Path;
    LOGICAL Uninitialize = FALSE;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;

    Result = ZpAdministration_OpenTaskService(&Service, &Uninitialize);
    if (SUCCEEDED(Result))
    {
        Path = SysAllocString(L"\\");
        Result = Path == NULL ? E_OUTOFMEMORY : ITaskService_GetFolder(Service, Path, &Root);
        SysFreeString(Path);
        if (SUCCEEDED(Result))
        {
            Result = ZpAdministration_EnumerateTaskFolder(Root, 0, &Builder);
            ITaskFolder_Release(Root);
        }
        ITaskService_Release(Service);
    }
    if (SUCCEEDED(Result)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    if (Uninitialize) CoUninitialize();
    return SUCCEEDED(Result) ? ZpStatus_FromNtStatus(Status) : ZpStatus_FromCode(ZpStatusHResult, Result);
}

static
ZP_STATUS
ZpAdministration_ControlTaskImpl(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    ITaskService* Service;
    ITaskFolder* Root = NULL;
    IRegisteredTask* Task = NULL;
    IRunningTask* Running;
    VARIANT Empty;
    PWSTR Identity;
    BSTR Path;
    LOGICAL Uninitialize = FALSE;
    HRESULT Result;

    Identity = ZpAdministration_CopyView(&Control->Identity);
    if (Identity == NULL) return ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    Result = ZpAdministration_OpenTaskService(&Service, &Uninitialize);
    if (SUCCEEDED(Result))
    {
        Path = SysAllocString(L"\\");
        Result = Path == NULL ? E_OUTOFMEMORY : ITaskService_GetFolder(Service, Path, &Root);
        SysFreeString(Path);
        if (SUCCEEDED(Result))
        {
            Path = SysAllocString(Identity);
            Result = Path == NULL ? E_OUTOFMEMORY : ITaskFolder_GetTask(Root, Path, &Task);
            SysFreeString(Path);
            if (SUCCEEDED(Result))
            {
                VariantInit(&Empty);
                switch (Control->Action)
                {
                    case ZpAdministrationActionRun:
                        Result = IRegisteredTask_Run(Task, Empty, &Running);
                        if (SUCCEEDED(Result)) IRunningTask_Release(Running);
                        break;
                    case ZpAdministrationActionStop:
                        Result = IRegisteredTask_Stop(Task, 0);
                        break;
                    case ZpAdministrationActionEnable:
                        Result = IRegisteredTask_put_Enabled(Task, VARIANT_TRUE);
                        break;
                    case ZpAdministrationActionDisable:
                        Result = IRegisteredTask_put_Enabled(Task, VARIANT_FALSE);
                        break;
                    case ZpAdministrationActionDelete:
                        IRegisteredTask_Release(Task);
                        Task = NULL;
                        Path = SysAllocString(Identity);
                        Result = Path == NULL ? E_OUTOFMEMORY : ITaskFolder_DeleteTask(Root, Path, 0);
                        SysFreeString(Path);
                        break;
                    default:
                        Result = E_NOTIMPL;
                        break;
                }
                if (Task != NULL) IRegisteredTask_Release(Task);
            }
            ITaskFolder_Release(Root);
        }
        ITaskService_Release(Service);
    }
    if (Uninitialize) CoUninitialize();
    Mem_Free(Identity);
    return ZpStatus_FromCode(ZpStatusHResult, Result);
}
