#define COBJMACROS
#include <wuapi.h>
#include <stdio.h>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "OleAut32.lib")
#pragma comment(lib, "Wuguid.lib")

static
ZP_STATUS
ZpAdministration_EnumerateUpdates(
    _In_ BOOLEAN Online,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    IUpdateSession* Session = NULL;
    IUpdateSearcher* Searcher = NULL;
    ISearchResult* Search = NULL;
    IUpdateCollection* Updates = NULL;
    IUpdateHistoryEntryCollection* History = NULL;
    IUpdateHistoryEntry* HistoryEntry = NULL;
    IUpdate* Update;
    IUpdate2* Update2;
    IUpdateIdentity* Identity = NULL;
    BSTR Criteria = NULL, Id = NULL, Title = NULL, Description = NULL;
    VARIANT_BOOL Downloaded, Mandatory, Reboot;
    DECIMAL Size;
    DATE Date;
    LONG Count, HistoryCount, Index, Error;
    UpdateOperation Operation;
    OperationResultCode ResultCode;
    WCHAR ErrorText[16];
    LOGICAL Uninitialize = FALSE;
    HRESULT Result;
    NTSTATUS Status = STATUS_SUCCESS;

    Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    Uninitialize = SUCCEEDED(Result);
    if (FAILED(Result) && Result != RPC_E_CHANGED_MODE) goto Cleanup;
    Result = CoCreateInstance(&CLSID_UpdateSession,
                              NULL,
                              CLSCTX_INPROC_SERVER,
                              &IID_IUpdateSession,
                              (PVOID*)&Session);
    if (FAILED(Result)) goto Cleanup;
    Result = IUpdateSession_CreateUpdateSearcher(Session, &Searcher);
    if (FAILED(Result)) goto Cleanup;
    Result = IUpdateSearcher_put_Online(Searcher, Online ? VARIANT_TRUE : VARIANT_FALSE);
    if (FAILED(Result)) goto Cleanup;
    Criteria = SysAllocString(L"IsInstalled=0 and IsHidden=0");
    Result = Criteria == NULL ? E_OUTOFMEMORY : IUpdateSearcher_Search(Searcher, Criteria, &Search);
    if (FAILED(Result)) goto Cleanup;
    Result = ISearchResult_get_Updates(Search, &Updates);
    if (FAILED(Result)) goto Cleanup;
    Result = IUpdateCollection_get_Count(Updates, &Count);
    for (Index = 0; SUCCEEDED(Result) && NT_SUCCESS(Status) && Index < Count; Index++)
    {
        Result = IUpdateCollection_get_Item(Updates, Index, &Update);
        if (FAILED(Result)) break;
        Result = IUpdate_get_Identity(Update, &Identity);
        if (SUCCEEDED(Result)) Result = IUpdateIdentity_get_UpdateID(Identity, &Id);
        if (SUCCEEDED(Result)) Result = IUpdate_get_Title(Update, &Title);
        if (SUCCEEDED(Result)) Result = IUpdate_get_Description(Update, &Description);
        if (SUCCEEDED(Result)) Result = IUpdate_get_IsDownloaded(Update, &Downloaded);
        if (SUCCEEDED(Result)) Result = IUpdate_get_IsMandatory(Update, &Mandatory);
        if (SUCCEEDED(Result)) Result = IUpdate_get_MaxDownloadSize(Update, &Size);
        Reboot = VARIANT_FALSE;
        if (SUCCEEDED(Result) && SUCCEEDED(IUpdate_QueryInterface(Update, &IID_IUpdate2, (PVOID*)&Update2)))
        {
            Result = IUpdate2_get_RebootRequired(Update2, &Reboot);
            IUpdate2_Release(Update2);
        }
        if (SUCCEEDED(Result))
        {
            Status = ZpAdministration_AddRecord(
                &Builder,
                ZpAdministrationKindUpdate,
                0,
                (Downloaded == VARIANT_TRUE ? 1UL : 0) |
                    (Mandatory == VARIANT_TRUE ? 2UL : 0) |
                    (Reboot == VARIANT_TRUE ? 4UL : 0),
                (ULONGLONG)Size.Lo64,
                Id,
                Title,
                Description,
                NULL);
        }
        SysFreeString(Description);
        SysFreeString(Title);
        SysFreeString(Id);
        Description = Title = Id = NULL;
        IUpdateIdentity_Release(Identity);
        IUpdate_Release(Update);
        Identity = NULL;
    }
    if (SUCCEEDED(Result) && NT_SUCCESS(Status))
    {
        Result = IUpdateSearcher_GetTotalHistoryCount(Searcher, &HistoryCount);
    }
    if (SUCCEEDED(Result) && HistoryCount != 0)
    {
        Result = IUpdateSearcher_QueryHistory(Searcher, 0, HistoryCount, &History);
    }
    if (SUCCEEDED(Result) && History != NULL)
    {
        Result = IUpdateHistoryEntryCollection_get_Count(History, &Count);
        for (Index = 0; SUCCEEDED(Result) && NT_SUCCESS(Status) && Index < Count; Index++)
        {
            Result = IUpdateHistoryEntryCollection_get_Item(History, Index, &HistoryEntry);
            if (SUCCEEDED(Result)) Result = IUpdateHistoryEntry_get_UpdateIdentity(HistoryEntry, &Identity);
            if (SUCCEEDED(Result)) Result = IUpdateIdentity_get_UpdateID(Identity, &Id);
            if (SUCCEEDED(Result)) Result = IUpdateHistoryEntry_get_Title(HistoryEntry, &Title);
            if (SUCCEEDED(Result)) Result = IUpdateHistoryEntry_get_Description(HistoryEntry, &Description);
            if (SUCCEEDED(Result)) Result = IUpdateHistoryEntry_get_Date(HistoryEntry, &Date);
            if (SUCCEEDED(Result)) Result = IUpdateHistoryEntry_get_Operation(HistoryEntry, &Operation);
            if (SUCCEEDED(Result)) Result = IUpdateHistoryEntry_get_ResultCode(HistoryEntry, &ResultCode);
            if (SUCCEEDED(Result)) Result = IUpdateHistoryEntry_get_HResult(HistoryEntry, &Error);
            if (SUCCEEDED(Result))
            {
                _snwprintf_s(ErrorText, ARRAYSIZE(ErrorText), _TRUNCATE, L"0x%08lX", Error);
                Status = ZpAdministration_AddRecord(&Builder,
                                                     ZpAdministrationKindUpdateHistory,
                                                     ResultCode,
                                                     Operation,
                                                     ZpAdministration_DateToFileTime(Date),
                                                     Id,
                                                     Title,
                                                     Description,
                                                     ErrorText);
            }
            SysFreeString(Description);
            SysFreeString(Title);
            SysFreeString(Id);
            Description = Title = Id = NULL;
            if (Identity != NULL) IUpdateIdentity_Release(Identity);
            IUpdateHistoryEntry_Release(HistoryEntry);
            Identity = NULL;
            HistoryEntry = NULL;
        }
    }
    if (SUCCEEDED(Result) && NT_SUCCESS(Status))
    {
        Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    }

Cleanup:
    SysFreeString(Description);
    SysFreeString(Title);
    SysFreeString(Id);
    SysFreeString(Criteria);
    if (Identity != NULL) IUpdateIdentity_Release(Identity);
    if (HistoryEntry != NULL) IUpdateHistoryEntry_Release(HistoryEntry);
    if (History != NULL) IUpdateHistoryEntryCollection_Release(History);
    if (Updates != NULL) IUpdateCollection_Release(Updates);
    if (Search != NULL) ISearchResult_Release(Search);
    if (Searcher != NULL) IUpdateSearcher_Release(Searcher);
    if (Session != NULL) IUpdateSession_Release(Session);
    ZpAdministration_FreeBuilder(&Builder);
    if (Uninitialize) CoUninitialize();
    return SUCCEEDED(Result) ? ZpStatus_FromNtStatus(Status) : ZpStatus_FromCode(ZpStatusHResult, Result);
}

static
ZP_STATUS
ZpAdministration_ControlUpdate(
    _In_ PCZP_ADMINISTRATION_CONTROL_VIEW Control)
{
    PBYTE Response;
    ULONG ResponseLength;
    ZP_STATUS Status;

    if (Control->Action != ZpAdministrationActionRefresh &&
        Control->Action != ZpAdministrationActionCheck)
    {
        return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    }
    Status = ZpAdministration_EnumerateUpdates(Control->Action == ZpAdministrationActionCheck,
                                                &Response,
                                                &ResponseLength);
    if (ZpStatus_IsSuccess(Status)) Mem_Free(Response);
    return Status;
}
