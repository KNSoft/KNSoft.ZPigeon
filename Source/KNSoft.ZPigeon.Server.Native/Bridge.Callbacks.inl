static
VOID
NTAPI
ZpNative_ServerStateCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_SERVER_STATE State,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Context);
    RtlAcquireSRWLockExclusive(&ZpNativeLock);
    ZpNativeState = State;
    ZpNativeStateStatus = Status;
    RtlReleaseSRWLockExclusive(&ZpNativeLock);
    SetEvent(ZpNativeStateEvent);
}

static
VOID
NTAPI
ZpNative_ServerConnectionCallback(
    _In_ ZP_SERVER_HANDLE Server,
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_ ZP_CONNECTION_PHASE Phase,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CLIENT_ENTRY Client = NULL;
    PLIST_ENTRY Entry;
    NTSTATUS NtStatus;

    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
    if (Phase == ZpConnectionPhaseReady)
    {
        Client = Mem_Alloc(sizeof(*Client));
        if (Client == NULL)
        {
            ZpServer_DisconnectConnection(Connection,
                                          ZpStatus_FromNtStatus(STATUS_NO_MEMORY));
            return;
        }
        NtStatus = ZpServer_QueryConnectionClientPublicKey(Connection,
                                                           Client->PublicKey);
        if (!NT_SUCCESS(NtStatus))
        {
            Mem_Free(Client);
            ZpServer_DisconnectConnection(Connection,
                                          ZpStatus_FromNtStatus(NtStatus));
            return;
        }
        Client->ClientId = (ULONGLONG)InterlockedIncrement64(&ZpNativeNextClientId);
        Client->Connection = Connection;
        ZpConnection_AddRef(Connection);
        RtlAcquireSRWLockExclusive(&ZpNativeLock);
        InsertTailList(&ZpNativeClients, &Client->ListEntry);
        InsertTailList(&ZpNativeClientIdBuckets[ZpNative_ClientIdBucket(Client->ClientId)],
                       &Client->IdHashEntry);
        InsertTailList(
            &ZpNativeClientConnectionBuckets[ZpNative_ClientConnectionBucket(Connection)],
            &Client->ConnectionHashEntry);
        RtlReleaseSRWLockExclusive(&ZpNativeLock);
        SetEvent(ZpNativeClientChangeEvent);
        return;
    }
    if (Phase != ZpConnectionPhaseClosed) return;
    RtlAcquireSRWLockExclusive(&ZpNativeLock);
    for (Entry = ZpNativeClientConnectionBuckets[ZpNative_ClientConnectionBucket(Connection)].Flink;
         Entry != &ZpNativeClientConnectionBuckets[ZpNative_ClientConnectionBucket(Connection)];
         Entry = Entry->Flink)
    {
        Client = CONTAINING_RECORD(Entry, ZP_NATIVE_CLIENT_ENTRY, ConnectionHashEntry);
        if (Client->Connection == Connection)
        {
            RemoveEntryList(&Client->ListEntry);
            RemoveEntryList(&Client->IdHashEntry);
            RemoveEntryList(&Client->ConnectionHashEntry);
            break;
        }
        Client = NULL;
    }
    RtlReleaseSRWLockExclusive(&ZpNativeLock);
    if (Client != NULL)
    {
        ZpConnection_Release(Client->Connection);
        Mem_Free(Client);
        SetEvent(ZpNativeClientChangeEvent);
    }
}

static
ZP_CONNECTION_HANDLE
ZpNative_GetConnection(
    _In_ ULONGLONG ClientId)
{
    ZP_CONNECTION_HANDLE Connection = NULL;
    PZP_NATIVE_CLIENT_ENTRY Client;
    PLIST_ENTRY Entry;

    RtlAcquireSRWLockShared(&ZpNativeLock);
    for (Entry = ZpNativeClientIdBuckets[ZpNative_ClientIdBucket(ClientId)].Flink;
         Entry != &ZpNativeClientIdBuckets[ZpNative_ClientIdBucket(ClientId)];
         Entry = Entry->Flink)
    {
        Client = CONTAINING_RECORD(Entry, ZP_NATIVE_CLIENT_ENTRY, IdHashEntry);
        if (Client->ClientId == ClientId)
        {
            Connection = Client->Connection;
            ZpConnection_AddRef(Connection);
            break;
        }
    }
    RtlReleaseSRWLockShared(&ZpNativeLock);
    return Connection;
}

static
VOID
ZpNative_FreeCallbackContext(
    _In_ PZP_NATIVE_CALLBACK_CONTEXT CallbackContext)
{
    RtlAcquireSRWLockExclusive(&ZpNativeLock);
    CallbackContext->Active = FALSE;
    RemoveEntryList(&CallbackContext->OperationEntry);
    RtlReleaseSRWLockExclusive(&ZpNativeLock);
    ZpConnection_Release(CallbackContext->Connection);
    if (InterlockedDecrement(&CallbackContext->ReferenceCount) == 0)
    {
        Mem_Free(CallbackContext);
    }
}

static
VOID
NTAPI
ZpNative_SystemInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SYSTEM_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.SystemInfo(
        Status,
        ZpStatus_IsSuccess(Status) ? Info->Architecture : 0,
        ZpStatus_IsSuccess(Status) ? Info->MajorVersion : 0,
        ZpStatus_IsSuccess(Status) ? Info->MinorVersion : 0,
        ZpStatus_IsSuccess(Status) ? Info->BuildNumber : 0,
        ZpStatus_IsSuccess(Status) ? Info->ProcessorCount : 0,
        ZpStatus_IsSuccess(Status) ? Info->PhysicalMemoryBytes : 0,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Info->ComputerName.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Info->ComputerName.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FilePageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_FILE_RECORD Records = NULL;
    ZP_FILE_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Page->Files.Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Page->Files.Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Page->Files.Count;
         Index++)
    {
        DecodeStatus = ZpFile_GetNextRecord(&Page->Files, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Attributes = Record.Info.Attributes;
        Records[Index].Size = Record.Info.Size;
        Records[Index].CreationTime = Record.Info.CreationTime;
        Records[Index].LastAccessTime = Record.Info.LastAccessTime;
        Records[Index].LastWriteTime = Record.Info.LastWriteTime;
        Records[Index].Name = (PCWCH)Record.Name.Buffer;
        Records[Index].NameLength = Record.Name.Length;
        Records[Index].HasChildren = Record.Info.HasChildren;
    }
    CallbackContext->Callback.FilePage(
        Status,
        ZpStatus_IsSuccess(Status) ? Page->EnumerationId : 0,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Page->Files.Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FilePreviewCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    const VOID* Buffer;
    ULONG Length;

    if (Context == NULL)
    {
        ZpRequest_Close(Request);
        return;
    }
    CallbackContext = Context;
    if (ZpStatus_IsSuccess(Status))
    {
        if (Data == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
            Buffer = NULL;
            Length = 0;
        }
        else
        {
            Buffer = Data->Buffer;
            Length = Data->Length;
        }
    }
    else
    {
        Buffer = NULL;
        Length = 0;
    }
    CallbackContext->Callback.FilePreview(Status,
                                          Buffer,
                                          Length,
                                          CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_PortableDevicesCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PORTABLE_DEVICE_LIST_VIEW Devices,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_PORTABLE_DEVICE_RECORD Records = NULL;
    ZP_PORTABLE_DEVICE_RECORD_VIEW Record;
    ULONG Count = 0, Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        if (Devices != NULL) Count = Devices->Count;
        else Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    if (ZpStatus_IsSuccess(Status) && Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Count; Index++)
    {
        DecodeStatus = ZpPortable_GetNextDevice(Devices, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Id = (PCWCH)Record.Id.Buffer;
        Records[Index].IdLength = Record.Id.Length;
        Records[Index].Name = (PCWCH)Record.Name.Buffer;
        Records[Index].NameLength = Record.Name.Length;
        Records[Index].Manufacturer = (PCWCH)Record.Manufacturer.Buffer;
        Records[Index].ManufacturerLength = Record.Manufacturer.Length;
        Records[Index].Model = (PCWCH)Record.Model.Buffer;
        Records[Index].ModelLength = Record.Model.Length;
    }
    CallbackContext->Callback.PortableDevices(Status,
                                               ZpStatus_IsSuccess(Status) ? Records : NULL,
                                               ZpStatus_IsSuccess(Status) ? Count : 0,
                                               CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_PortableObjectsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PORTABLE_OBJECT_PAGE_VIEW Objects,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_PORTABLE_OBJECT_RECORD Records = NULL;
    ZP_PORTABLE_OBJECT_RECORD_VIEW Record;
    ULONG Count = 0, Index, NextOffset = 0, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        if (Objects != NULL)
        {
            Count = Objects->Count;
            NextOffset = Objects->NextOffset;
        }
        else Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
    }
    if (ZpStatus_IsSuccess(Status) && Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Count; Index++)
    {
        DecodeStatus = ZpPortable_GetNextObject(Objects, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Size = Record.Size;
        Records[Index].ModifiedTime = Record.ModifiedTime;
        Records[Index].Capacity = Record.Capacity;
        Records[Index].FreeSpace = Record.FreeSpace;
        Records[Index].Flags = Record.Flags;
        Records[Index].Id = (PCWCH)Record.Id.Buffer;
        Records[Index].IdLength = Record.Id.Length;
        Records[Index].PersistentId = (PCWCH)Record.PersistentId.Buffer;
        Records[Index].PersistentIdLength = Record.PersistentId.Length;
        Records[Index].Name = (PCWCH)Record.Name.Buffer;
        Records[Index].NameLength = Record.Name.Length;
    }
    CallbackContext->Callback.PortableObjects(Status,
                                               ZpStatus_IsSuccess(Status) ? Records : NULL,
                                               ZpStatus_IsSuccess(Status) ? Count : 0,
                                               ZpStatus_IsSuccess(Status) ? NextOffset : 0,
                                               CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FileInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_INFO Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.FileInfo(
        Status,
        ZpStatus_IsSuccess(Status) ? Info->Attributes : 0,
        ZpStatus_IsSuccess(Status) ? Info->Size : 0,
        ZpStatus_IsSuccess(Status) ? Info->CreationTime : 0,
        ZpStatus_IsSuccess(Status) ? Info->LastAccessTime : 0,
        ZpStatus_IsSuccess(Status) ? Info->LastWriteTime : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FileHashCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_HASH_VIEW Hash,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.FileHash(
        Status,
        ZpStatus_IsSuccess(Status) ? Hash->FileSize : 0,
        ZpStatus_IsSuccess(Status) ? Hash->Digest.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Hash->Digest.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FileOwnersCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_OWNER_LIST_VIEW Owners,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_FILE_OWNER_RECORD Records = NULL;
    ZP_FILE_OWNER_RECORD_VIEW Owner;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Owners->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Owners->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Owners->Count; Index++)
    {
        DecodeStatus = ZpFile_GetNextOwnerRecord(Owners, &Offset, &Owner);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].ProcessId = Owner.ProcessId;
        Records[Index].ImagePathStatus = Owner.ImagePathStatus;
        Records[Index].CommandLineStatus = Owner.CommandLineStatus;
        Records[Index].ImageName = (PCWCH)Owner.ImageName.Buffer;
        Records[Index].ImageNameLength = Owner.ImageName.Length;
        Records[Index].ImagePath = (PCWCH)Owner.ImagePath.Buffer;
        Records[Index].ImagePathLength = Owner.ImagePath.Length;
        Records[Index].CommandLine = (PCWCH)Owner.CommandLine.Buffer;
        Records[Index].CommandLineLength = Owner.CommandLine.Length;
        Records[Index].ServiceNames = (PCWCH)Owner.ServiceNames.Buffer;
        Records[Index].ServiceNamesLength = Owner.ServiceNames.Length;
    }
    CallbackContext->Callback.FileOwners(Status,
                                         ZpStatus_IsSuccess(Status) ? Records : NULL,
                                         ZpStatus_IsSuccess(Status) ? Owners->Count : 0,
                                         CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FileOwnerControlCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_FILE_OWNER_CONTROL_RESULT_VIEW* Results,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_FILE_OWNER_CONTROL_RESULT Values = NULL;
    ULONG Index;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        Values = Mem_Alloc((SIZE_T)Results->Count * sizeof(*Values));
        if (Values == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Results->Count; Index++)
    {
        DecodeStatus = ZpFile_GetOwnerControlResult(Results, Index, &Values[Index]);
        if (!NT_SUCCESS(DecodeStatus)) Status = ZpStatus_FromNtStatus(DecodeStatus);
    }
    CallbackContext->Callback.FileOwnerControl(Status,
                                               ZpStatus_IsSuccess(Status) ? Values : NULL,
                                               ZpStatus_IsSuccess(Status) ? Results->Count : 0,
                                               CallbackContext->Context);
    Mem_Free(Values);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FileDownloadsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_FILE_DOWNLOAD_LIST_VIEW Downloads,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_FILE_DOWNLOAD_RECORD Records = NULL;
    ZP_FILE_DOWNLOAD_RECORD_VIEW Download;
    ULONG Count, Index, Offset = 0;
    NTSTATUS DecodeStatus;

    _Analysis_assume_(CallbackContext != NULL);
    if (!ZpStatus_IsSuccess(Status))
    {
        CallbackContext->Callback.FileDownloads(Status, NULL, 0, CallbackContext->Context);
        ZpRequest_Close(Request);
        ZpNative_FreeCallbackContext(CallbackContext);
        return;
    }
    if (Downloads == NULL)
    {
        CallbackContext->Callback.FileDownloads(ZpStatus_FromNtStatus(STATUS_DATA_ERROR),
                                                 NULL,
                                                 0,
                                                 CallbackContext->Context);
        ZpRequest_Close(Request);
        ZpNative_FreeCallbackContext(CallbackContext);
        return;
    }
    Count = Downloads->Count;
    if (Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Count * sizeof(*Records));
        if (Records == NULL)
        {
            CallbackContext->Callback.FileDownloads(ZpStatus_FromNtStatus(STATUS_NO_MEMORY),
                                                     NULL,
                                                     0,
                                                     CallbackContext->Context);
            ZpRequest_Close(Request);
            ZpNative_FreeCallbackContext(CallbackContext);
            return;
        }
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Count; Index++)
    {
        DecodeStatus = ZpFile_GetNextDownloadRecord(Downloads, &Offset, &Download);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Engine = Download.Engine;
        Records[Index].State = Download.State;
        Records[Index].Result = Download.Result;
        Records[Index].TransferredBytes = Download.TransferredBytes;
        Records[Index].TotalBytes = Download.TotalBytes;
        Records[Index].Id = (PCWCH)Download.Id.Buffer;
        Records[Index].IdLength = Download.Id.Length;
        Records[Index].Url = (PCWCH)Download.Url.Buffer;
        Records[Index].UrlLength = Download.Url.Length;
        Records[Index].Path = (PCWCH)Download.Path.Buffer;
        Records[Index].PathLength = Download.Path.Length;
        Records[Index].ErrorText = (PCWCH)Download.ErrorText.Buffer;
        Records[Index].ErrorTextLength = Download.ErrorText.Length;
    }
    CallbackContext->Callback.FileDownloads(Status,
                                             ZpStatus_IsSuccess(Status) ? Records : NULL,
                                             ZpStatus_IsSuccess(Status) ? Count : 0,
                                             CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PROCESS_LIST_VIEW Processes,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_PROCESS_RECORD Records = NULL;
    ZP_PROCESS_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Processes->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Processes->Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Processes->Count;
         Index++)
    {
        DecodeStatus = ZpProcess_GetNextRecord(Processes, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].ProcessId = Record.ProcessId;
        Records[Index].ParentProcessId = Record.ParentProcessId;
        Records[Index].SessionId = Record.SessionId;
        Records[Index].ThreadCount = Record.ThreadCount;
        Records[Index].HandleCount = Record.HandleCount;
        Records[Index].Flags = Record.Flags;
        Records[Index].MachineType = Record.MachineType;
        Records[Index].PriorityClass = Record.PriorityClass;
        Records[Index].CreateTime = Record.CreateTime;
        Records[Index].UserTime = Record.UserTime;
        Records[Index].KernelTime = Record.KernelTime;
        Records[Index].WorkingSetBytes = Record.WorkingSetBytes;
        Records[Index].PrivateBytes = Record.PrivateBytes;
        Records[Index].ImageName = (PCWCH)Record.ImageName.Buffer;
        Records[Index].ImageNameLength = Record.ImageName.Length;
        Records[Index].UserName = (PCWCH)Record.UserName.Buffer;
        Records[Index].UserNameLength = Record.UserName.Length;
        Records[Index].ImagePath = (PCWCH)Record.ImagePath.Buffer;
        Records[Index].ImagePathLength = Record.ImagePath.Length;
        Records[Index].ServiceNames = (PCWCH)Record.ServiceNames.Buffer;
        Records[Index].ServiceNamesLength = Record.ServiceNames.Length;
    }
    CallbackContext->Callback.ProcessList(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Processes->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_PROCESS_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ProcessInfo(Status,
                                          ZpStatus_IsSuccess(Status) ? Info : NULL,
                                          CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessModulesCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PROCESS_MODULE_LIST_VIEW Modules,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_PROCESS_MODULE_RECORD Records = NULL;
    ZP_PROCESS_MODULE_RECORD_VIEW Module;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Modules->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Modules->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Modules->Count; Index++)
    {
        DecodeStatus = ZpProcess_GetNextModule(Modules, &Offset, &Module);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].BaseAddress = Module.BaseAddress;
        Records[Index].EntryPoint = Module.EntryPoint;
        Records[Index].LoadTime = Module.LoadTime;
        Records[Index].SizeOfImage = Module.SizeOfImage;
        Records[Index].LoadReason = Module.LoadReason;
        Records[Index].Path = (PCWCH)Module.Path.Buffer;
        Records[Index].PathLength = Module.Path.Length;
    }
    CallbackContext->Callback.ProcessModules(
        Status,
        ZpStatus_IsSuccess(Status) ? Modules->MachineType : IMAGE_FILE_MACHINE_UNKNOWN,
        ZpStatus_IsSuccess(Status) ? Modules->MachineBits : 0,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Modules->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessDumpCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_STRING_VIEW Path,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ProcessDump(Status,
                                          ZpStatus_IsSuccess(Status) ? (PCWCH)Path->Buffer : NULL,
                                          ZpStatus_IsSuccess(Status) ? Path->Length : 0,
                                          CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessMemoryCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ProcessMemory(Status,
                                             ZpStatus_IsSuccess(Status) ? Data->Buffer : NULL,
                                             ZpStatus_IsSuccess(Status) ? Data->Length : 0,
                                             CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessMemoryAllocationsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PROCESS_MEMORY_ALLOCATION_MAP_VIEW Map,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_PROCESS_MEMORY_ALLOCATION Allocations = NULL;
    ZP_PROCESS_MEMORY_ALLOCATION_VIEW Allocation;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Map->Count != 0)
    {
        Allocations = Mem_Alloc((SIZE_T)Map->Count * sizeof(*Allocations));
        if (Allocations == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Map->Count; Index++)
    {
        DecodeStatus = ZpProcess_ReadMemoryAllocation(Map, &Offset, &Allocation);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Allocations[Index].AllocationBase = Allocation.AllocationBase;
        Allocations[Index].RegionSize = Allocation.RegionSize;
        Allocations[Index].CommitSize = Allocation.CommitSize;
        Allocations[Index].WorkingSetBytes = Allocation.WorkingSetBytes;
        Allocations[Index].PrivateWorkingSetBytes = Allocation.PrivateWorkingSetBytes;
        Allocations[Index].SharedWorkingSetBytes = Allocation.SharedWorkingSetBytes;
        Allocations[Index].ShareableWorkingSetBytes = Allocation.ShareableWorkingSetBytes;
        Allocations[Index].LockedWorkingSetBytes = Allocation.LockedWorkingSetBytes;
        Allocations[Index].SharedOriginalBytes = Allocation.SharedOriginalBytes;
        Allocations[Index].Type = Allocation.Type;
        Allocations[Index].AllocationProtect = Allocation.AllocationProtect;
        Allocations[Index].RegionType = Allocation.RegionType;
        Allocations[Index].Priority = Allocation.Priority;
        Allocations[Index].RegionCount = Allocation.RegionCount;
        Allocations[Index].RegionStatus = Allocation.RegionStatus;
        Allocations[Index].WorkingSetStatus = Allocation.WorkingSetStatus;
        Allocations[Index].MappedPathStatus = Allocation.MappedPathStatus;
        Allocations[Index].MappedPath = (PCWCH)Allocation.MappedPath.Buffer;
        Allocations[Index].MappedPathLength = Allocation.MappedPath.Length;
    }
    CallbackContext->Callback.ProcessMemoryAllocations(
        Status,
        ZpStatus_IsSuccess(Status) ? Map->SnapshotId : 0,
        ZpStatus_IsSuccess(Status) ? Allocations : NULL,
        ZpStatus_IsSuccess(Status) ? Map->Count : 0,
        CallbackContext->Context);
    Mem_Free(Allocations);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ProcessMemoryRegionsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_PROCESS_MEMORY_MAP_VIEW Map,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_PROCESS_MEMORY_REGION Regions = NULL;
    ZP_PROCESS_MEMORY_REGION_VIEW Region;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Map->Count != 0)
    {
        Regions = Mem_Alloc((SIZE_T)Map->Count * sizeof(*Regions));
        if (Regions == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Map->Count; Index++)
    {
        DecodeStatus = ZpProcess_ReadMemoryMapRegion(Map, &Offset, &Region);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Regions[Index].BaseAddress = Region.BaseAddress;
        Regions[Index].RegionSize = Region.RegionSize;
        Regions[Index].CommitSize = Region.CommitSize;
        Regions[Index].WorkingSetBytes = Region.WorkingSetBytes;
        Regions[Index].PrivateWorkingSetBytes = Region.PrivateWorkingSetBytes;
        Regions[Index].SharedWorkingSetBytes = Region.SharedWorkingSetBytes;
        Regions[Index].ShareableWorkingSetBytes = Region.ShareableWorkingSetBytes;
        Regions[Index].LockedWorkingSetBytes = Region.LockedWorkingSetBytes;
        Regions[Index].SharedOriginalBytes = Region.SharedOriginalBytes;
        Regions[Index].State = Region.State;
        Regions[Index].Protect = Region.Protect;
        Regions[Index].Priority = Region.Priority;
        Regions[Index].WorkingSetStatus = Region.WorkingSetStatus;
    }
    CallbackContext->Callback.ProcessMemoryRegions(
        Status,
        ZpStatus_IsSuccess(Status) ? Regions : NULL,
        ZpStatus_IsSuccess(Status) ? Map->Count : 0,
        CallbackContext->Context);
    Mem_Free(Regions);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_StringCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_STRING_VIEW Value,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.String(Status,
                                     ZpStatus_IsSuccess(Status) ? (PCWCH)Value->Buffer : NULL,
                                     ZpStatus_IsSuccess(Status) ? Value->Length : 0,
                                     CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_SecurityDescriptorCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_SECURITY_DESCRIPTOR_VIEW Descriptor,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.SecurityDescriptor(
        Status,
        ZpStatus_IsSuccess(Status) ? Descriptor->Sddl.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Descriptor->Sddl.Length : 0,
        ZpStatus_IsSuccess(Status) ? Descriptor->DaclProtected : FALSE,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_FileVolumeCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_FILE_VOLUME_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.FileVolume(
        Status,
        ZpStatus_IsSuccess(Status) ? Info->TotalBytes : 0,
        ZpStatus_IsSuccess(Status) ? Info->FreeBytes : 0,
        ZpStatus_IsSuccess(Status) ? Info->SerialNumber : 0,
        ZpStatus_IsSuccess(Status) ? Info->MaximumComponentLength : 0,
        ZpStatus_IsSuccess(Status) ? Info->FileSystemFlags : 0,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Info->Label.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Info->Label.Length : 0,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Info->FileSystem.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Info->FileSystem.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ExecutionSessionsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EXECUTION_SESSION_LIST_VIEW Sessions,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_EXECUTION_SESSION_RECORD Records = NULL;
    ZP_EXECUTION_SESSION_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Sessions->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Sessions->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Sessions->Count; Index++)
    {
        DecodeStatus = ZpExecution_GetNextSession(Sessions, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].SessionId = Record.SessionId;
        Records[Index].State = Record.State;
        Records[Index].Flags = Record.Flags;
        Records[Index].StationName = (PCWCH)Record.StationName.Buffer;
        Records[Index].StationNameLength = Record.StationName.Length;
        Records[Index].UserName = (PCWCH)Record.UserName.Buffer;
        Records[Index].UserNameLength = Record.UserName.Length;
    }
    CallbackContext->Callback.ExecutionSessions(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Sessions->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ExecutionEnvironmentCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EXECUTION_ENVIRONMENT_VIEW Environment,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_EXECUTION_RUNTIME_RECORD Records = NULL;
    ZP_EXECUTION_RUNTIME_RECORD_VIEW Record;
    ULONG Index, Offset = 0, VersionIndex;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Environment->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Environment->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Environment->Count; Index++)
    {
        DecodeStatus = ZpExecution_GetNextRuntime(Environment, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Kind = Record.Kind;
        Records[Index].Machine = Record.Image.Machine;
        Records[Index].Subsystem = Record.Image.Subsystem;
        for (VersionIndex = 0; VersionIndex < ARRAYSIZE(Record.Image.Version); VersionIndex++)
        {
            Records[Index].Version[VersionIndex] = Record.Image.Version[VersionIndex];
        }
        Records[Index].Path = (PCWCH)Record.Path.Buffer;
        Records[Index].PathLength = Record.Path.Length;
    }
    CallbackContext->Callback.ExecutionEnvironment(
        Status,
        ZpStatus_IsSuccess(Status) ? Environment->Flags : 0,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Environment->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ExecutionImageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EXECUTION_IMAGE_INFO Image,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    static const USHORT EmptyVersion[4] = { 0 };

    CallbackContext->Callback.ExecutionImage(
        Status,
        ZpStatus_IsSuccess(Status) ? Image->Machine : 0,
        ZpStatus_IsSuccess(Status) ? Image->Subsystem : 0,
        ZpStatus_IsSuccess(Status) ? Image->Version : EmptyVersion,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ExecutionJobsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EXECUTION_JOB_LIST_VIEW Jobs,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_EXECUTION_JOB_RECORD Records = NULL;
    ZP_EXECUTION_JOB_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Jobs->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Jobs->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Jobs->Count; Index++)
    {
        DecodeStatus = ZpExecution_GetNextJob(Jobs, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].JobId = Record.JobId;
        Records[Index].CreateTime = Record.CreateTime;
        Records[Index].ExitTime = Record.ExitTime;
        Records[Index].ProcessId = Record.ProcessId;
        Records[Index].SessionId = Record.SessionId;
        Records[Index].ExitCode = Record.ExitCode;
        Records[Index].Flags = Record.Flags;
        Records[Index].Engine = Record.Engine;
        Records[Index].Identity = Record.Identity;
        Records[Index].State = Record.State;
        Records[Index].FileName = (PCWCH)Record.FileName.Buffer;
        Records[Index].FileNameLength = Record.FileName.Length;
    }
    CallbackContext->Callback.ExecutionJobs(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Jobs->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ExecutionStagingCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_STRING_VIEW Path,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ExecutionStaging(
        Status,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Path->Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Path->Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_WindowListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_WINDOW_LIST_VIEW Windows,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_WINDOW_RECORD Records = NULL;
    ZP_WINDOW_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Windows->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Windows->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Windows->Count; Index++)
    {
        DecodeStatus = ZpWindow_GetNextRecord(Windows, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Handle = Record.Handle;
        Records[Index].ParentHandle = Record.ParentHandle;
        Records[Index].ProcessId = Record.ProcessId;
        Records[Index].ThreadId = Record.ThreadId;
        Records[Index].Style = Record.Style;
        Records[Index].ExStyle = Record.ExStyle;
        Records[Index].Flags = Record.Flags;
        Records[Index].Caption = (PCWCH)Record.Caption.Buffer;
        Records[Index].CaptionLength = Record.Caption.Length;
        Records[Index].ClassName = (PCWCH)Record.ClassName.Buffer;
        Records[Index].ClassNameLength = Record.ClassName.Length;
    }
    CallbackContext->Callback.WindowList(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Windows->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_WindowInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_WINDOW_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.WindowInfo(Status,
                                          ZpStatus_IsSuccess(Status) ? Info : NULL,
                                          CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_WindowCaptureCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Image,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.WindowCapture(
        Status,
        ZpStatus_IsSuccess(Status) ? Image->Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Image->Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseWindowCapture(
    _Inout_ PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream)
{
    if (InterlockedDecrement(&Stream->ReferenceCount) == 0)
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_WindowCaptureOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Stream->Channel = Channel;
        Stream->ReferenceCount = 2;
    }
    Stream->OpenCallback(Status,
                         ZpStatus_IsSuccess(Status) ? Stream : NULL,
                         Stream->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_WindowCaptureDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream = Context;

    if (!Stream->DataCallback(Data->Buffer, Data->Length, Stream->Context))
    {
        ZpChannel_Cancel(Channel);
    }
}

static
VOID
NTAPI
ZpNative_WindowCaptureCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_WINDOW_CAPTURE_STREAM Stream = Context;

    RtlAcquireSRWLockExclusive(&Stream->Lock);
    Stream->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Stream->Lock);
    Stream->CloseCallback(Status, Stream->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseWindowCapture(Stream);
}

static
VOID
NTAPI
ZpNative_AudioDevicesCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_AUDIO_DEVICE_LIST_VIEW Devices,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_AUDIO_DEVICE_RECORD Records = NULL;
    ZP_AUDIO_DEVICE_VIEW Device;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Devices->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Devices->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Devices->Count; Index++)
    {
        DecodeStatus = ZpAudio_GetNextDevice(Devices, &Offset, &Device);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Flow = Device.Flow;
        Records[Index].State = Device.State;
        Records[Index].Flags = Device.Flags;
        Records[Index].Volume = Device.Volume;
        Records[Index].Id = (PCWCH)Device.Id.Buffer;
        Records[Index].IdLength = Device.Id.Length;
        Records[Index].Name = (PCWCH)Device.Name.Buffer;
        Records[Index].NameLength = Device.Name.Length;
    }
    CallbackContext->Callback.AudioDevices(Status,
                                            ZpStatus_IsSuccess(Status) ? Records : NULL,
                                            ZpStatus_IsSuccess(Status) ? Devices->Count : 0,
                                            CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_AudioSessionsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_AUDIO_SESSION_LIST_VIEW Sessions,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_AUDIO_SESSION_RECORD Records = NULL;
    ZP_AUDIO_SESSION_VIEW Session;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Sessions->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Sessions->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Sessions->Count; Index++)
    {
        DecodeStatus = ZpAudio_GetNextSession(Sessions, &Offset, &Session);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].ProcessId = Session.ProcessId;
        Records[Index].State = Session.State;
        Records[Index].Flags = Session.Flags;
        Records[Index].Volume = Session.Volume;
        Records[Index].DeviceId = (PCWCH)Session.DeviceId.Buffer;
        Records[Index].DeviceIdLength = Session.DeviceId.Length;
        Records[Index].Id = (PCWCH)Session.Id.Buffer;
        Records[Index].IdLength = Session.Id.Length;
        Records[Index].Name = (PCWCH)Session.Name.Buffer;
        Records[Index].NameLength = Session.Name.Length;
    }
    CallbackContext->Callback.AudioSessions(Status,
                                             ZpStatus_IsSuccess(Status) ? Records : NULL,
                                             ZpStatus_IsSuccess(Status) ? Sessions->Count : 0,
                                             CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseAudioStream(
    _Inout_ PZP_NATIVE_AUDIO_STREAM Stream)
{
    if (InterlockedDecrement(&Stream->ReferenceCount) == 0)
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_AudioStreamOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_AUDIO_STREAM Stream = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Stream->Channel = Channel;
        Stream->ReferenceCount = 2;
    }
    Stream->OpenCallback(Status, ZpStatus_IsSuccess(Status) ? Stream : NULL, Stream->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_AudioStreamDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_AUDIO_STREAM Stream = Context;

    if (!Stream->DataCallback(Data->Buffer, Data->Length, Stream->Context)) ZpChannel_Cancel(Channel);
}

static
VOID
NTAPI
ZpNative_AudioStreamCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_AUDIO_STREAM Stream = Context;

    RtlAcquireSRWLockExclusive(&Stream->Lock);
    Stream->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Stream->Lock);
    Stream->CloseCallback(Status, Stream->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseAudioStream(Stream);
}

static
VOID
NTAPI
ZpNative_VideoDevicesCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_VIDEO_DEVICE_LIST_VIEW Devices,
    _In_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_VIDEO_DEVICE_RECORD Records = NULL;
    PZP_VIDEO_FORMAT Formats = NULL;
    ZP_VIDEO_DEVICE_VIEW Device;
    ULONG Count = 0, TotalFormats = 0, Index, FormatIndex, Offset = 0, RecordFormatIndex, DeviceFormatOffset;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        if (Devices == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
        }
        else
        {
            Count = Devices->Count;
            for (Index = 0; Index < Count; Index++)
            {
                DecodeStatus = ZpVideo_GetNextDevice(Devices, &Offset, &Device);
                if (!NT_SUCCESS(DecodeStatus))
                {
                    Status = ZpStatus_FromNtStatus(DecodeStatus);
                    break;
                }
                TotalFormats += Device.FormatCount;
            }
            Records = ZpStatus_IsSuccess(Status) && Count != 0 ?
                          Mem_Alloc((SIZE_T)Count * sizeof(*Records) +
                                    (SIZE_T)TotalFormats * sizeof(*Formats)) : NULL;
            if (ZpStatus_IsSuccess(Status) && Count != 0 && Records == NULL)
            {
                Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            }
            else if (ZpStatus_IsSuccess(Status))
            {
                Formats = Add2Ptr(Records, (SIZE_T)Count * sizeof(*Records));
                Offset = 0;
                RecordFormatIndex = 0;
                for (Index = 0; Index < Count; Index++)
                {
                    DecodeStatus = ZpVideo_GetNextDevice(Devices, &Offset, &Device);
                    if (!NT_SUCCESS(DecodeStatus))
                    {
                        Status = ZpStatus_FromNtStatus(DecodeStatus);
                        break;
                    }
                    Records[Index].Id = (PCWCH)Device.Id.Buffer;
                    Records[Index].IdLength = Device.Id.Length;
                    Records[Index].Name = (PCWCH)Device.Name.Buffer;
                    Records[Index].NameLength = Device.Name.Length;
                    Records[Index].Formats = &Formats[RecordFormatIndex];
                    Records[Index].FormatCount = Device.FormatCount;
                    DeviceFormatOffset = 0;
                    for (FormatIndex = 0; FormatIndex < Device.FormatCount; FormatIndex++)
                    {
                        DecodeStatus = ZpVideo_GetNextFormat(&Device,
                                                             &DeviceFormatOffset,
                                                             &Formats[RecordFormatIndex++]);
                        if (!NT_SUCCESS(DecodeStatus))
                        {
                            Status = ZpStatus_FromNtStatus(DecodeStatus);
                            break;
                        }
                    }
                    if (!ZpStatus_IsSuccess(Status)) break;
                }
            }
        }
    }
    CallbackContext->Callback.VideoDevices(Status,
                                            ZpStatus_IsSuccess(Status) ? Records : NULL,
                                            ZpStatus_IsSuccess(Status) ? Count : 0,
                                            CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_SerialPortsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_SERIAL_PORT_LIST_VIEW Ports,
    _In_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_SERIAL_PORT_RECORD Records = NULL;
    ZP_SERIAL_PORT_VIEW Port;
    ULONG Count = 0, Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        if (Ports == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
        }
        else
        {
            Count = Ports->Count;
            Records = Count != 0 ? Mem_Alloc((SIZE_T)Count * sizeof(*Records)) : NULL;
            if (Count != 0 && Records == NULL)
            {
                Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            }
            else for (Index = 0; Index < Count; Index++)
            {
                DecodeStatus = ZpSerial_GetNextPort(Ports, &Offset, &Port);
                if (!NT_SUCCESS(DecodeStatus))
                {
                    Status = ZpStatus_FromNtStatus(DecodeStatus);
                    break;
                }
                Records[Index].Name = (PCWCH)Port.Name.Buffer;
                Records[Index].NameLength = Port.Name.Length;
                Records[Index].Device = (PCWCH)Port.Device.Buffer;
                Records[Index].DeviceLength = Port.Device.Length;
            }
        }
    }
    CallbackContext->Callback.SerialPorts(Status,
                                           ZpStatus_IsSuccess(Status) ? Records : NULL,
                                           ZpStatus_IsSuccess(Status) ? Count : 0,
                                           CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_RecordingCapabilitiesCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ ULONG Codecs,
    _In_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.RecordingCapabilities(Status, Codecs, CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_RecordingRecordsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_RECORDING_LIST_VIEW List,
    _In_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_RECORDING_RECORD Records = NULL;
    ZP_RECORDING_RECORD_VIEW Record;
    ULONG Count = 0, Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status))
    {
        if (List == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_DATA_ERROR);
        }
        else
        {
            Count = List->Count;
            Records = Count != 0 ? Mem_Alloc((SIZE_T)Count * sizeof(*Records)) : NULL;
            if (Count != 0 && Records == NULL)
            {
                Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
            }
            else for (Index = 0; Index < Count; Index++)
            {
                DecodeStatus = ZpRecording_GetNextRecord(List, &Offset, &Record);
                if (!NT_SUCCESS(DecodeStatus))
                {
                    Status = ZpStatus_FromNtStatus(DecodeStatus);
                    break;
                }
                Records[Index].RecordingId = Record.RecordingId;
                Records[Index].Source = Record.Source;
                Records[Index].Codec = Record.Codec;
                Records[Index].State = Record.State;
                Records[Index].Status = Record.Status;
                Records[Index].StartTime = Record.StartTime;
                Records[Index].Duration = Record.Duration;
                Records[Index].FileSize = Record.FileSize;
                Records[Index].Path = (PCWCH)Record.Path.Buffer;
                Records[Index].PathLength = Record.Path.Length;
            }
        }
    }
    CallbackContext->Callback.RecordingRecords(Status,
                                                ZpStatus_IsSuccess(Status) ? Records : NULL,
                                                ZpStatus_IsSuccess(Status) ? Count : 0,
                                                CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseVideoStream(
    _In_ PZP_NATIVE_VIDEO_STREAM Stream)
{
    if (InterlockedDecrement(&Stream->ReferenceCount) == 0)
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_VideoStreamOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ PVOID Context)
{
    PZP_NATIVE_VIDEO_STREAM Stream = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Stream->Channel = Channel;
        Stream->ReferenceCount = 2;
    }
    Stream->OpenCallback(Status, ZpStatus_IsSuccess(Status) ? Stream : NULL, Stream->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Stream->Connection);
        Mem_Free(Stream);
    }
}

static
VOID
NTAPI
ZpNative_VideoStreamDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_ PVOID Context)
{
    PZP_NATIVE_VIDEO_STREAM Stream = Context;

    if (!Stream->DataCallback(Data->Buffer, Data->Length, Stream->Context)) ZpChannel_Cancel(Channel);
}

static
VOID
NTAPI
ZpNative_VideoStreamCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_ PVOID Context)
{
    PZP_NATIVE_VIDEO_STREAM Stream = Context;

    RtlAcquireSRWLockExclusive(&Stream->Lock);
    Stream->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Stream->Lock);
    Stream->CloseCallback(Status, Stream->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseVideoStream(Stream);
}

static
VOID
NTAPI
ZpNative_ServiceListCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_SERVICE_LIST_VIEW Services,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_SERVICE_RECORD Records = NULL;
    ZP_SERVICE_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Services->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Services->Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Services->Count;
         Index++)
    {
        DecodeStatus = ZpService_GetNextRecord(Services, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].ServiceType = Record.ServiceType;
        Records[Index].CurrentState = Record.CurrentState;
        Records[Index].ControlsAccepted = Record.ControlsAccepted;
        Records[Index].ProcessId = Record.ProcessId;
        Records[Index].StartType = Record.StartType;
        Records[Index].ServiceName = (PCWCH)Record.ServiceName.Buffer;
        Records[Index].ServiceNameLength = Record.ServiceName.Length;
        Records[Index].DisplayName = (PCWCH)Record.DisplayName.Buffer;
        Records[Index].DisplayNameLength = Record.DisplayName.Length;
        Records[Index].Description = (PCWCH)Record.Description.Buffer;
        Records[Index].DescriptionLength = Record.Description.Length;
        Records[Index].StartName = (PCWCH)Record.StartName.Buffer;
        Records[Index].StartNameLength = Record.StartName.Length;
    }
    CallbackContext->Callback.ServiceList(
        Status,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Services->Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_ServiceInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_SERVICE_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.ServiceInfo(Status,
                                          ZpStatus_IsSuccess(Status) ? Info : NULL,
                                          CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseFileTransfer(
    _Inout_ PZP_NATIVE_FILE_TRANSFER Transfer)
{
    if (InterlockedDecrement(&Transfer->ReferenceCount) == 0)
    {
        ZpConnection_Release(Transfer->Connection);
        Mem_Free(Transfer);
    }
}

static
VOID
NTAPI
ZpNative_FileReadOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_ ULONGLONG Offset,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    UNREFERENCED_PARAMETER(Offset);
    if (ZpStatus_IsSuccess(Status))
    {
        Transfer->Channel = Channel;
        Transfer->ReferenceCount = 2;
    }
    Transfer->OpenCallback(Status,
                           ZpStatus_IsSuccess(Status) ? Transfer : NULL,
                           FileSize,
                           Transfer->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Transfer->Connection);
        Mem_Free(Transfer);
    }
}

static
VOID
NTAPI
ZpNative_FileWriteOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONGLONG FileSize,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Transfer->Channel = Channel;
        Transfer->ReferenceCount = 2;
    }
    Transfer->OpenCallback(Status,
                           ZpStatus_IsSuccess(Status) ? Transfer : NULL,
                           FileSize,
                           Transfer->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Transfer->Connection);
        Mem_Free(Transfer);
    }
}

static
VOID
NTAPI
ZpNative_FileDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    if (!Transfer->DataCallback(Data->Buffer, Data->Length, Transfer->Context))
    {
        ZpChannel_Cancel(Channel);
    }
}

static
VOID
NTAPI
ZpNative_FileWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    UNREFERENCED_PARAMETER(Channel);
    Transfer->WritableCallback(CreditBytes, Transfer->Context);
}

static
VOID
NTAPI
ZpNative_FileCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer = Context;

    RtlAcquireSRWLockExclusive(&Transfer->Lock);
    Transfer->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Transfer->Lock);
    Transfer->CloseCallback(Status, Transfer->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseFileTransfer(Transfer);
}

static
VOID
NTAPI
ZpNative_StatusCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.Status(Status, CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_AdministrationCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_ADMINISTRATION_LIST_VIEW List,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_ADMINISTRATION_RECORD Records = NULL;
    ZP_ADMINISTRATION_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && List->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)List->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < List->Count; Index++)
    {
        DecodeStatus = ZpAdministration_GetNextRecord(List, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Kind = Record.Kind;
        Records[Index].State = Record.State;
        Records[Index].Flags = Record.Flags;
        Records[Index].Value = Record.Value;
#define ZP_NATIVE_ADMINISTRATION_STRING(Field) \
        Records[Index].Field = (PCWCH)Record.Field.Buffer; \
        Records[Index].Field##Length = Record.Field.Length
        ZP_NATIVE_ADMINISTRATION_STRING(Identity);
        ZP_NATIVE_ADMINISTRATION_STRING(Name);
        ZP_NATIVE_ADMINISTRATION_STRING(Description);
        ZP_NATIVE_ADMINISTRATION_STRING(Detail);
#undef ZP_NATIVE_ADMINISTRATION_STRING
        Records[Index].Data = Record.Data.Buffer;
        Records[Index].DataLength = Record.Data.Length;
    }
    CallbackContext->Callback.Administration(Status,
                                               ZpStatus_IsSuccess(Status) ? Records : NULL,
                                               ZpStatus_IsSuccess(Status) ? List->Count : 0,
                                               CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_AdministrationDataCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BUFFER_VIEW Data,
    _In_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    const VOID* Buffer = NULL;
    ULONG Length = 0;

    if (ZpStatus_IsSuccess(Status))
    {
        _Analysis_assume_(Data != NULL);
        Buffer = Data->Buffer;
        Length = Data->Length;
    }
    CallbackContext->Callback.AdministrationData(Status,
                                                  Buffer,
                                                  Length,
                                                  CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_BrowserCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BROWSER_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_BROWSER_RECORD Records = NULL;
    ZP_BROWSER_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Page->Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Page->Count * sizeof(*Records));
        if (Records == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Page->Count; Index++)
    {
        DecodeStatus = ZpBrowser_GetNextRecord(Page, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Kind = Record.Kind;
        Records[Index].Browser = Record.Browser;
        Records[Index].Id = Record.Id;
        Records[Index].Data = Record.Data;
#define ZP_NATIVE_BROWSER_STRING(Field) \
        Records[Index].Field = (PCWCH)Record.Field.Buffer; \
        Records[Index].Field##Length = Record.Field.Length
        ZP_NATIVE_BROWSER_STRING(Identity);
        ZP_NATIVE_BROWSER_STRING(Name);
        ZP_NATIVE_BROWSER_STRING(Location);
        ZP_NATIVE_BROWSER_STRING(Detail);
#undef ZP_NATIVE_BROWSER_STRING
    }
    CallbackContext->Callback.Browser(Status,
                                      ZpStatus_IsSuccess(Status) ? Page->NextCursor : 0,
                                      ZpStatus_IsSuccess(Status) ? Records : NULL,
                                      ZpStatus_IsSuccess(Status) ? Page->Count : 0,
                                      CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_BrowserProfileInspectionCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BROWSER_PROFILE_INSPECTION Inspection,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.BrowserProfileInspection(Status,
                                                        Inspection,
                                                        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_BrowserDocumentCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_BROWSER_DOCUMENT_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_BROWSER_DOCUMENT_NODE Nodes = NULL;
    ZP_BROWSER_DOCUMENT_NODE_VIEW Node;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Page->Count != 0)
    {
        Nodes = Mem_Alloc((SIZE_T)Page->Count * sizeof(*Nodes));
        if (Nodes == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Page->Count; Index++)
    {
        DecodeStatus = ZpBrowser_GetNextDocumentNode(Page, &Offset, &Node);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Nodes[Index].Id = Node.Id;
        Nodes[Index].Type = Node.Type;
        Nodes[Index].Flags = Node.Flags;
        Nodes[Index].Name = (PCWCH)Node.Name.Buffer;
        Nodes[Index].NameLength = Node.Name.Length;
        Nodes[Index].Value = (PCWCH)Node.Value.Buffer;
        Nodes[Index].ValueLength = Node.Value.Length;
    }
    CallbackContext->Callback.BrowserDocument(
        Status,
        ZpStatus_IsSuccess(Status) ? Page->SnapshotId : 0,
        ZpStatus_IsSuccess(Status) ? Page->ParentType : 0,
        ZpStatus_IsSuccess(Status) ? Page->NextCursor : 0,
        ZpStatus_IsSuccess(Status) ? Nodes : NULL,
        ZpStatus_IsSuccess(Status) ? Page->Count : 0,
        CallbackContext->Context);
    Mem_Free(Nodes);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_WmiCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_WMI_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_WMI_ROW Rows = NULL;
    PZP_NATIVE_WMI_CELL Cells = NULL, CellCursor;
    ZP_WMI_ROW_VIEW Row;
    ZP_WMI_CELL Cell;
    ULONG RowIndex, CellIndex, CellCount = 0, RowOffset = 0, CellOffset;
    NTSTATUS DecodeStatus;

    for (RowIndex = 0; ZpStatus_IsSuccess(Status) && RowIndex < Page->RowCount; RowIndex++)
    {
        DecodeStatus = ZpWmi_GetNextRow(Page, &RowOffset, &Row);
        if (!NT_SUCCESS(DecodeStatus) || CellCount > MAXULONG - Row.CellCount)
        {
            Status = ZpStatus_FromNtStatus(
                NT_SUCCESS(DecodeStatus) ? STATUS_INTEGER_OVERFLOW : DecodeStatus);
            break;
        }
        CellCount += Row.CellCount;
    }
    if (ZpStatus_IsSuccess(Status) && Page->RowCount != 0)
    {
        Rows = Mem_Alloc((SIZE_T)Page->RowCount * sizeof(*Rows));
        Cells = CellCount == 0 ? NULL : Mem_Alloc((SIZE_T)CellCount * sizeof(*Cells));
        if (Rows == NULL || (CellCount != 0 && Cells == NULL))
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    CellCursor = Cells;
    RowOffset = 0;
    for (RowIndex = 0; ZpStatus_IsSuccess(Status) && RowIndex < Page->RowCount; RowIndex++)
    {
        DecodeStatus = ZpWmi_GetNextRow(Page, &RowOffset, &Row);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Rows[RowIndex].Cells = CellCursor;
        Rows[RowIndex].CellCount = Row.CellCount;
        CellOffset = 0;
        for (CellIndex = 0; CellIndex < Row.CellCount; CellIndex++, CellCursor++)
        {
            DecodeStatus = ZpWmi_GetNextCell(&Row, &CellOffset, &Cell);
            if (!NT_SUCCESS(DecodeStatus))
            {
                Status = ZpStatus_FromNtStatus(DecodeStatus);
                break;
            }
            CellCursor->Type = Cell.Type;
            CellCursor->Name = Cell.Name;
            CellCursor->NameLength = Cell.NameLength;
            CellCursor->Value = Cell.Value;
            CellCursor->ValueLength = Cell.ValueLength;
        }
        if (!NT_SUCCESS(DecodeStatus)) break;
    }
    CallbackContext->Callback.Wmi(Status,
                                  ZpStatus_IsSuccess(Status) ? Rows : NULL,
                                  ZpStatus_IsSuccess(Status) ? Page->RowCount : 0,
                                  CallbackContext->Context);
    Mem_Free(Cells);
    Mem_Free(Rows);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_EventLogCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_EVENT_LOG_PAGE_VIEW* Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_EVENT_LOG_RECORD Records = NULL;
    ZP_EVENT_LOG_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus = STATUS_SUCCESS;

    if (ZpStatus_IsSuccess(Status) && Page->Records.Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Page->Records.Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Page->Records.Count;
         Index++)
    {
        DecodeStatus = ZpEventLog_GetNextRecord(&Page->Records, &Offset, &Record);
        if (NT_SUCCESS(DecodeStatus))
        {
            Records[Index].Bookmark = (PCWCH)Record.Bookmark.Buffer;
            Records[Index].BookmarkLength = Record.Bookmark.Length;
            Records[Index].Xml = (PCWCH)Record.Xml.Buffer;
            Records[Index].XmlLength = Record.Xml.Length;
        }
        else
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
        }
    }
    CallbackContext->Callback.EventLog(
        Status,
        ZpStatus_IsSuccess(Status) ? Page->HasMore : FALSE,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Page->NextBookmark.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Page->NextBookmark.Length : 0,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Page->Records.Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_EventLogChannelsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_EVENT_LOG_CHANNEL_LIST_VIEW Channels,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_STRING_VIEW Values = NULL;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus = STATUS_SUCCESS;

    if (ZpStatus_IsSuccess(Status) && Channels->Count != 0)
    {
        Values = Mem_Alloc((SIZE_T)Channels->Count * sizeof(*Values));
        if (Values == NULL) Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
    }
    for (Index = 0; ZpStatus_IsSuccess(Status) && Index < Channels->Count; Index++)
    {
        DecodeStatus = ZpEventLog_GetNextChannel(Channels, &Offset, &Values[Index]);
        if (!NT_SUCCESS(DecodeStatus)) Status = ZpStatus_FromNtStatus(DecodeStatus);
    }
    CallbackContext->Callback.EventLogChannels(Status,
                                                ZpStatus_IsSuccess(Status) ? Values : NULL,
                                                ZpStatus_IsSuccess(Status) ? Channels->Count : 0,
                                                CallbackContext->Context);
    Mem_Free(Values);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_EventLogChannelInfoCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ const ZP_EVENT_LOG_CHANNEL_INFO_VIEW* Info,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.EventLogChannelInfo(
        Status,
        ZpStatus_IsSuccess(Status) ? Info->Enabled : FALSE,
        ZpStatus_IsSuccess(Status) ? Info->Type : 0,
        ZpStatus_IsSuccess(Status) ? Info->RetentionMode : 0,
        ZpStatus_IsSuccess(Status) ? Info->MaximumSize : 0,
        ZpStatus_IsSuccess(Status) ? Info->FileSize : 0,
        ZpStatus_IsSuccess(Status) ? Info->CreationTime : 0,
        ZpStatus_IsSuccess(Status) ? Info->LastAccessTime : 0,
        ZpStatus_IsSuccess(Status) ? Info->LastWriteTime : 0,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Info->LogFilePath.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Info->LogFilePath.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_RegistryKeyPageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_REGISTRY_KEY_RECORD Records = NULL;
    ZP_REGISTRY_KEY_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Page->Records.Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Page->Records.Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Page->Records.Count;
         Index++)
    {
        DecodeStatus = ZpRegistry_GetNextKeyRecord(&Page->Records, &Offset, &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Name = (PCWCH)Record.Name.Buffer;
        Records[Index].NameLength = Record.Name.Length;
        Records[Index].LastWriteTime = Record.LastWriteTime;
        Records[Index].HasChildren = Record.HasChildren;
    }
    CallbackContext->Callback.RegistryKeyPage(
        Status,
        ZpStatus_IsSuccess(Status) ? Page->HasMore : FALSE,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Page->NextCursor.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Page->NextCursor.Length : 0,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Page->Records.Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_RegistryValuePageCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_PAGE_VIEW Page,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;
    PZP_NATIVE_REGISTRY_VALUE_RECORD Records = NULL;
    ZP_REGISTRY_VALUE_RECORD_VIEW Record;
    ULONG Index, Offset = 0;
    NTSTATUS DecodeStatus;

    if (ZpStatus_IsSuccess(Status) && Page->Records.Count != 0)
    {
        Records = Mem_Alloc((SIZE_T)Page->Records.Count * sizeof(*Records));
        if (Records == NULL)
        {
            Status = ZpStatus_FromNtStatus(STATUS_NO_MEMORY);
        }
    }
    for (Index = 0;
         ZpStatus_IsSuccess(Status) && Index < Page->Records.Count;
         Index++)
    {
        DecodeStatus = ZpRegistry_GetNextValueRecord(&Page->Records,
                                                      &Offset,
                                                      &Record);
        if (!NT_SUCCESS(DecodeStatus))
        {
            Status = ZpStatus_FromNtStatus(DecodeStatus);
            break;
        }
        Records[Index].Name = (PCWCH)Record.Name.Buffer;
        Records[Index].NameLength = Record.Name.Length;
        Records[Index].Type = Record.Type;
        Records[Index].DataLength = Record.DataLength;
        Records[Index].Preview = Record.Preview.Buffer;
        Records[Index].PreviewLength = Record.Preview.Length;
    }
    CallbackContext->Callback.RegistryValuePage(
        Status,
        ZpStatus_IsSuccess(Status) ? Page->HasMore : FALSE,
        ZpStatus_IsSuccess(Status) ? (PCWCH)Page->NextCursor.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Page->NextCursor.Length : 0,
        ZpStatus_IsSuccess(Status) ? Records : NULL,
        ZpStatus_IsSuccess(Status) ? Page->Records.Count : 0,
        CallbackContext->Context);
    Mem_Free(Records);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_RegistryValueCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_VALUE_VIEW Value,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.RegistryValue(
        Status,
        ZpStatus_IsSuccess(Status) ? Value->Type : 0,
        ZpStatus_IsSuccess(Status) ? Value->Data.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Value->Data.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_RegistryRangeCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ PCZP_REGISTRY_RANGE_VIEW Range,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.RegistryRange(
        Status,
        ZpStatus_IsSuccess(Status) ? Range->TotalLength : 0,
        ZpStatus_IsSuccess(Status) ? Range->Data.Buffer : NULL,
        ZpStatus_IsSuccess(Status) ? Range->Data.Length : 0,
        CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
NTAPI
ZpNative_TerminalShellsCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_ BYTE Shells,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext = Context;

    CallbackContext->Callback.TerminalShells(Status,
                                             Shells,
                                             CallbackContext->Context);
    ZpRequest_Close(Request);
    ZpNative_FreeCallbackContext(CallbackContext);
}

static
VOID
ZpNative_ReleaseTerminal(
    _Inout_ PZP_NATIVE_TERMINAL Terminal)
{
    if (InterlockedDecrement(&Terminal->ReferenceCount) == 0)
    {
        ZpConnection_Release(Terminal->Connection);
        Mem_Free(Terminal);
    }
}

static
VOID
NTAPI
ZpNative_TerminalCreateCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG ProcessId,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TERMINAL Terminal = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Terminal->Channel = Channel;
        Terminal->ReferenceCount = 2;
    }
    Terminal->CreateCallback(Status,
                             ZpStatus_IsSuccess(Status) ? Terminal : NULL,
                             ProcessId,
                             Terminal->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Terminal->Connection);
        Mem_Free(Terminal);
    }
}

static
VOID
NTAPI
ZpNative_TerminalDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TERMINAL Terminal = Context;

    if (!Terminal->DataCallback(Data->Buffer,
                                Data->Length,
                                Terminal->Context))
    {
        ZpChannel_Cancel(Channel);
    }
}

static
VOID
NTAPI
ZpNative_TerminalWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TERMINAL Terminal = Context;

    UNREFERENCED_PARAMETER(Channel);
    Terminal->WritableCallback(CreditBytes, Terminal->Context);
}

static
VOID
NTAPI
ZpNative_TerminalCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TERMINAL Terminal = Context;

    RtlAcquireSRWLockExclusive(&Terminal->Lock);
    Terminal->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Terminal->Lock);
    Terminal->CloseCallback(Status, Terminal->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseTerminal(Terminal);
}

static
VOID
ZpNative_ReleaseTunnel(
    _Inout_ PZP_NATIVE_TUNNEL Tunnel)
{
    if (InterlockedDecrement(&Tunnel->ReferenceCount) == 0)
    {
        ZpConnection_Release(Tunnel->Connection);
        Mem_Free(Tunnel);
    }
}

static
VOID
NTAPI
ZpNative_TunnelOpenCallback(
    _In_ ZP_REQUEST_HANDLE Request,
    _In_ ZP_STATUS Status,
    _In_opt_ ZP_CHANNEL_HANDLE Channel,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TUNNEL Tunnel = Context;

    if (ZpStatus_IsSuccess(Status))
    {
        Tunnel->Channel = Channel;
        Tunnel->ReferenceCount = 2;
    }
    Tunnel->OpenCallback(Status,
                         ZpStatus_IsSuccess(Status) ? Tunnel : NULL,
                         Tunnel->Context);
    ZpRequest_Close(Request);
    if (!ZpStatus_IsSuccess(Status))
    {
        ZpConnection_Release(Tunnel->Connection);
        Mem_Free(Tunnel);
    }
}

static
VOID
NTAPI
ZpNative_TunnelDataCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ PCZP_BUFFER_VIEW Data,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TUNNEL Tunnel = Context;

    if (!Tunnel->DataCallback(Data->Buffer, Data->Length, Tunnel->Context))
    {
        ZpChannel_Cancel(Channel);
    }
}

static
VOID
NTAPI
ZpNative_TunnelWritableCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ULONG CreditBytes,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TUNNEL Tunnel = Context;

    UNREFERENCED_PARAMETER(Channel);
    Tunnel->WritableCallback(CreditBytes, Tunnel->Context);
}

static
VOID
NTAPI
ZpNative_TunnelCloseCallback(
    _In_ ZP_CHANNEL_HANDLE Channel,
    _In_ ZP_STATUS Status,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_TUNNEL Tunnel = Context;

    RtlAcquireSRWLockExclusive(&Tunnel->Lock);
    Tunnel->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Tunnel->Lock);
    Tunnel->CloseCallback(Status, Tunnel->Context);
    ZpChannel_Close(Channel);
    ZpNative_ReleaseTunnel(Tunnel);
}

static
PZP_NATIVE_CALLBACK_CONTEXT
ZpNative_CreateCallbackContext(
    _In_ ZP_CONNECTION_HANDLE Connection,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;

    CallbackContext = Mem_Alloc(sizeof(*CallbackContext));
    if (CallbackContext != NULL)
    {
        // The submission reference keeps synchronous completions alive until the request call returns.
        CallbackContext->ReferenceCount = 2;
        CallbackContext->Active = TRUE;
        CallbackContext->Connection = Connection;
        CallbackContext->Request = NULL;
        CallbackContext->Context = Context;
        RtlAcquireSRWLockExclusive(&ZpNativeLock);
        InsertTailList(&ZpNativeOperations, &CallbackContext->OperationEntry);
        RtlReleaseSRWLockExclusive(&ZpNativeLock);
    }
    return CallbackContext;
}
