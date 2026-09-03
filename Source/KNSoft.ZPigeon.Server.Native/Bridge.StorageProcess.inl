NTSTATUS
NTAPI
ZpNative_GetSystemInfo(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_SYSTEM_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.SystemInfo = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_GetSystemInfo(Connection,
                               ZP_NATIVE_TIMEOUT_MILLISECONDS,
                               ZpNative_SystemInfoCallback,
                               CallbackContext,
                               &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateFilesPage(
    _In_ ULONGLONG ClientId,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG EnumerationId,
    _In_ ZP_NATIVE_FILE_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FilePage = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateFilesPage(Connection,
                                    Path,
                                    PathLength,
                                    EnumerationId,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_FilePageCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateFilteredFilesPage(
    _In_ ULONGLONG ClientId,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_opt_(FilterLength) PCWCH Filter,
    _In_ ULONG FilterLength,
    _In_ WCHAR Group,
    _In_ ULONG EnumerationId,
    _In_ ZP_NATIVE_FILE_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FilePage = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateFilteredFilesPage(Connection,
                                            Path,
                                            PathLength,
                                            Filter,
                                            FilterLength,
                                            Group,
                                            EnumerationId,
                                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                            ZpNative_FilePageCallback,
                                            CallbackContext,
                                            &Request));
}

NTSTATUS
NTAPI
ZpNative_CloseFileEnumeration(
    _In_ ULONGLONG ClientId,
    _In_ ULONG EnumerationId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_CloseFileEnumeration(Connection,
                                      EnumerationId,
                                      ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                      ZpNative_StatusCallback,
                                      CallbackContext,
                                      &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateArchivePage(
    _In_ ULONGLONG ClientId,
    _In_reads_opt_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG EnumerationId,
    _In_ ZP_NATIVE_FILE_PAGE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FilePage = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateArchivePage(Connection,
                                      Path,
                                      PathLength,
                                      EnumerationId,
                                      ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                      ZpNative_FilePageCallback,
                                      CallbackContext,
                                      &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryShortcut(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.String = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryShortcut(Connection,
                               Path,
                               PathLength,
                               ZP_NATIVE_TIMEOUT_MILLISECONDS,
                               ZpNative_StringCallback,
                               CallbackContext,
                               &Request));
}

NTSTATUS
NTAPI
ZpNative_PreviewImage(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_IMAGE_PREVIEW_QUALITY Quality,
    _In_ ZP_NATIVE_FILE_PREVIEW_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FilePreview = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_PreviewImage(Connection,
                              Path,
                              PathLength,
                              Quality,
                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                              ZpNative_FilePreviewCallback,
                              CallbackContext,
                              &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryFile(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_FILE_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FileInfo = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryFile(Connection,
                           Path,
                           PathLength,
                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                           ZpNative_FileInfoCallback,
                           CallbackContext,
                           &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryFileSecurity(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_SECURITY_DESCRIPTOR_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.SecurityDescriptor = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryFileSecurity(Connection,
                                   Path,
                                   PathLength,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_SecurityDescriptorCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_SetFileSecurity(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(SddlLength) PCWCH Sddl,
    _In_ ULONG SddlLength,
    _In_ BOOLEAN DaclProtected,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_SetFileSecurity(Connection,
                                 Path,
                                 PathLength,
                                 Sddl,
                                 SddlLength,
                                 DaclProtected,
                                 ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                 ZpNative_StatusCallback,
                                 CallbackContext,
                                 &Request));
}

NTSTATUS
NTAPI
ZpNative_ResolveAccountName(
    _In_ ULONGLONG ClientId,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.String = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ResolveAccountName(Connection,
                                    Name,
                                    NameLength,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_StringCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_ResolveAccountSid(
    _In_ ULONGLONG ClientId,
    _In_reads_(SidLength) PCWCH Sid,
    _In_ ULONG SidLength,
    _In_ ZP_NATIVE_STRING_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.String = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ResolveAccountSid(Connection,
                                   Sid,
                                   SidLength,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_StringCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryFileVolume(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_FILE_VOLUME_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FileVolume = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryFileVolume(Connection,
                                 Path,
                                 PathLength,
                                 ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                 ZpNative_FileVolumeCallback,
                                 CallbackContext,
                                 &Request));
}

NTSTATUS
NTAPI
ZpNative_SetFileVolumeLabel(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(LabelLength) PCWCH Label,
    _In_ ULONG LabelLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_SetFileVolumeLabel(Connection,
                                    Path,
                                    PathLength,
                                    Label,
                                    LabelLength,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_StatusCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_HashFile(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_HASH_ALGORITHM Algorithm,
    _In_ ZP_NATIVE_FILE_HASH_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FileHash = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_HashFile(Connection,
                          Path,
                          PathLength,
                          Algorithm,
                          ZP_NATIVE_TIMEOUT_MILLISECONDS,
                          ZpNative_FileHashCallback,
                          CallbackContext,
                          &Request));
}

NTSTATUS
NTAPI
ZpNative_DeleteFile(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_DeleteFile(Connection,
                            Path,
                            PathLength,
                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                            ZpNative_StatusCallback,
                            CallbackContext,
                            &Request));
}

NTSTATUS
NTAPI
ZpNative_RenameFile(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_reads_(NewPathLength) PCWCH NewPath,
    _In_ ULONG NewPathLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_RenameFile(Connection,
                            Path,
                            PathLength,
                            NewPath,
                            NewPathLength,
                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                            ZpNative_StatusCallback,
                            CallbackContext,
                            &Request));
}

NTSTATUS
NTAPI
ZpNative_SetFileAttributes(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONG Attributes,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_SetFileAttributes(Connection,
                                   Path,
                                   PathLength,
                                   Attributes,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_StatusCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryFileOwners(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_FILE_OWNERS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FileOwners = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryFileOwners(Connection,
                                 Path,
                                 PathLength,
                                 ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                 ZpNative_FileOwnersCallback,
                                 CallbackContext,
                                 &Request));
}

NTSTATUS
NTAPI
ZpNative_ControlFileOwners(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_FILE_OWNER_CONTROL Control,
    _In_reads_(ProcessCount) const ULONG* ProcessIds,
    _In_ ULONG ProcessCount,
    _In_ ZP_NATIVE_FILE_OWNER_CONTROL_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FileOwnerControl = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ControlFileOwners(Connection,
                                   Path,
                                   PathLength,
                                   Control,
                                   ProcessIds,
                                   ProcessCount,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_FileOwnerControlCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_StartFileDownload(
    _In_ ULONGLONG ClientId,
    _In_ ZP_FILE_DOWNLOAD_ENGINE Engine,
    _In_ BYTE Flags,
    _In_ const GUID* Id,
    _In_reads_(UrlLength) PCWCH Url,
    _In_ ULONG UrlLength,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_StartFileDownload(Connection,
                                   Engine,
                                   Flags,
                                   Id,
                                   Url,
                                   UrlLength,
                                   Path,
                                   PathLength,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_StatusCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateFileDownloads(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_FILE_DOWNLOADS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.FileDownloads = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateFileDownloads(Connection,
                                        ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                        ZpNative_FileDownloadsCallback,
                                        CallbackContext,
                                        &Request));
}

NTSTATUS
NTAPI
ZpNative_CancelFileDownload(
    _In_ ULONGLONG ClientId,
    _In_ const GUID* Id,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_CancelFileDownload(Connection,
                                    Id,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_StatusCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_OpenFileRead(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_ ZP_NATIVE_FILE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_FILE_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_FILE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer;
    ZP_CONNECTION_HANDLE Connection;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    Transfer = Mem_Alloc(sizeof(*Transfer));
    if (Transfer == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Connection = Connection;
    Transfer->OpenCallback = OpenCallback;
    Transfer->DataCallback = DataCallback;
    Transfer->CloseCallback = CloseCallback;
    Transfer->Context = Context;
    Status = ZpServer_OpenFileRead(Connection,
                                   Path,
                                   PathLength,
                                   Offset,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_FileReadOpenCallback,
                                   ZpNative_FileDataCallback,
                                   ZpNative_FileCloseCallback,
                                   Transfer,
                                   &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Transfer);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_WriteFileRange(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_WriteFileRange(Connection,
                                Path,
                                PathLength,
                                Offset,
                                Data,
                                DataLength,
                                ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                ZpNative_StatusCallback,
                                CallbackContext,
                                &Request));
}

NTSTATUS
NTAPI
ZpNative_OpenFileWrite(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ULONGLONG FileSize,
    _In_ LOGICAL Overwrite,
    _In_ ZP_NATIVE_FILE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_FILE_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_FILE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer;
    ZP_CONNECTION_HANDLE Connection;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (OpenCallback == NULL || WritableCallback == NULL || CloseCallback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    Transfer = Mem_Alloc(sizeof(*Transfer));
    if (Transfer == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Connection = Connection;
    Transfer->OpenCallback = OpenCallback;
    Transfer->WritableCallback = WritableCallback;
    Transfer->CloseCallback = CloseCallback;
    Transfer->Context = Context;
    Status = ZpServer_OpenFileWrite(Connection,
                                    Path,
                                    PathLength,
                                    FileSize,
                                    Overwrite ? ZpFileCreateAlways : ZpFileCreateNew,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_FileWriteOpenCallback,
                                    ZpNative_FileWritableCallback,
                                    ZpNative_FileCloseCallback,
                                    Transfer,
                                    &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Transfer);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_FileSend(
    _In_ ZP_NATIVE_FILE_TRANSFER_HANDLE Transfer,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength)
{
    NTSTATUS Status;

    if (Transfer == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    RtlAcquireSRWLockShared(&Transfer->Lock);
    Status = Transfer->Channel != NULL ?
                 ZpChannel_Send(Transfer->Channel, Data, DataLength) :
                 STATUS_INVALID_DEVICE_STATE;
    RtlReleaseSRWLockShared(&Transfer->Lock);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_CloseFileTransfer(
    _In_ ZP_NATIVE_FILE_TRANSFER_HANDLE Transfer)
{
    ZP_CHANNEL_HANDLE Channel;
    NTSTATUS Status;

    if (Transfer == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (InterlockedExchange(&Transfer->CallerClosed, TRUE))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlAcquireSRWLockExclusive(&Transfer->Lock);
    Channel = Transfer->Channel;
    Transfer->Channel = NULL;
    RtlReleaseSRWLockExclusive(&Transfer->Lock);
    Status = Channel != NULL ? ZpChannel_Cancel(Channel) : STATUS_SUCCESS;
    ZpNative_ReleaseFileTransfer(Transfer);
    return Status;
}

NTSTATUS
NTAPI
ZpNative_EnumeratePortableDevices(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_PORTABLE_DEVICES_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.PortableDevices = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumeratePortableDevices(Connection,
                                          ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                          ZpNative_PortableDevicesCallback,
                                          CallbackContext,
                                          &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumeratePortableObjects(
    _In_ ULONGLONG ClientId,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_opt_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_ ULONG Offset,
    _In_ ZP_NATIVE_PORTABLE_OBJECTS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.PortableObjects = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumeratePortableObjects(Connection,
                                          DeviceId,
                                          DeviceIdLength,
                                          ParentId,
                                          ParentIdLength,
                                          Offset,
                                          ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                          ZpNative_PortableObjectsCallback,
                                          CallbackContext,
                                          &Request));
}

NTSTATUS
NTAPI
ZpNative_CreatePortableFolder(
    _In_ ULONGLONG ClientId,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_CreatePortableFolder(Connection,
                                      DeviceId,
                                      DeviceIdLength,
                                      ParentId,
                                      ParentIdLength,
                                      Name,
                                      NameLength,
                                      ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                      ZpNative_StatusCallback,
                                      CallbackContext,
                                      &Request));
}

NTSTATUS
NTAPI
ZpNative_DeletePortableObject(
    _In_ ULONGLONG ClientId,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_DeletePortableObject(Connection,
                                      DeviceId,
                                      DeviceIdLength,
                                      ObjectId,
                                      ObjectIdLength,
                                      ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                      ZpNative_StatusCallback,
                                      CallbackContext,
                                      &Request));
}

NTSTATUS
NTAPI
ZpNative_RenamePortableObject(
    _In_ ULONGLONG ClientId,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_RenamePortableObject(Connection,
                                      DeviceId,
                                      DeviceIdLength,
                                      ObjectId,
                                      ObjectIdLength,
                                      Name,
                                      NameLength,
                                      ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                      ZpNative_StatusCallback,
                                      CallbackContext,
                                      &Request));
}

NTSTATUS
NTAPI
ZpNative_OpenPortableRead(
    _In_ ULONGLONG ClientId,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ObjectIdLength) PCWCH ObjectId,
    _In_ ULONG ObjectIdLength,
    _In_ ZP_NATIVE_FILE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_FILE_DATA_CALLBACK DataCallback,
    _In_ ZP_NATIVE_FILE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer;
    ZP_CONNECTION_HANDLE Connection;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (OpenCallback == NULL || DataCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    Transfer = Mem_Alloc(sizeof(*Transfer));
    if (Transfer == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Connection = Connection;
    Transfer->OpenCallback = OpenCallback;
    Transfer->DataCallback = DataCallback;
    Transfer->CloseCallback = CloseCallback;
    Transfer->Context = Context;
    Status = ZpServer_OpenPortableRead(Connection,
                                       DeviceId,
                                       DeviceIdLength,
                                       ObjectId,
                                       ObjectIdLength,
                                       ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                       ZpNative_FileReadOpenCallback,
                                       ZpNative_FileDataCallback,
                                       ZpNative_FileCloseCallback,
                                       Transfer,
                                       &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Transfer);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_OpenPortableWrite(
    _In_ ULONGLONG ClientId,
    _In_reads_(DeviceIdLength) PCWCH DeviceId,
    _In_ ULONG DeviceIdLength,
    _In_reads_(ParentIdLength) PCWCH ParentId,
    _In_ ULONG ParentIdLength,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ULONGLONG FileSize,
    _In_ ZP_NATIVE_FILE_OPEN_CALLBACK OpenCallback,
    _In_ ZP_NATIVE_FILE_WRITABLE_CALLBACK WritableCallback,
    _In_ ZP_NATIVE_FILE_CLOSE_CALLBACK CloseCallback,
    _In_opt_ PVOID Context)
{
    PZP_NATIVE_FILE_TRANSFER Transfer;
    ZP_CONNECTION_HANDLE Connection;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (OpenCallback == NULL || WritableCallback == NULL || CloseCallback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    Transfer = Mem_Alloc(sizeof(*Transfer));
    if (Transfer == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Connection = Connection;
    Transfer->OpenCallback = OpenCallback;
    Transfer->WritableCallback = WritableCallback;
    Transfer->CloseCallback = CloseCallback;
    Transfer->Context = Context;
    Status = ZpServer_OpenPortableWrite(Connection,
                                        DeviceId,
                                        DeviceIdLength,
                                        ParentId,
                                        ParentIdLength,
                                        Name,
                                        NameLength,
                                        FileSize,
                                        ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                        ZpNative_FileWriteOpenCallback,
                                        ZpNative_FileWritableCallback,
                                        ZpNative_FileCloseCallback,
                                        Transfer,
                                        &Request);
    if (!NT_SUCCESS(Status))
    {
        ZpConnection_Release(Connection);
        Mem_Free(Transfer);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_EnumerateProcesses(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_PROCESS_LIST_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessList = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateProcesses(Connection,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_ProcessListCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryProcess(
    _In_ ULONGLONG ClientId,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_NATIVE_PROCESS_INFO_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessInfo = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryProcess(Connection,
                              ProcessId,
                              CreateTime,
                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                              ZpNative_ProcessInfoCallback,
                              CallbackContext,
                              &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateProcessModules(
    _In_ ULONGLONG ClientId,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_NATIVE_PROCESS_MODULES_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessModules = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateProcessModules(Connection,
                                         ProcessId,
                                         CreateTime,
                                         ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                         ZpNative_ProcessModulesCallback,
                                         CallbackContext,
                                         &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateProcessHandles(
    _In_ ULONGLONG ClientId,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_NATIVE_PROCESS_HANDLES_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessHandles = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateProcessHandles(Connection,
                                         ProcessId,
                                         CreateTime,
                                         ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                         ZpNative_ProcessHandlesCallback,
                                         CallbackContext,
                                         &Request));
}

static
NTSTATUS
ZpNative_CompleteRequestStart(
    _In_ PZP_NATIVE_CALLBACK_CONTEXT CallbackContext,
    _In_ NTSTATUS Status,
    _When_(NT_SUCCESS(Status), _In_) ZP_REQUEST_HANDLE* Request)
{
    if (NT_SUCCESS(Status))
    {
        RtlAcquireSRWLockExclusive(&ZpNativeLock);
        if (CallbackContext->Active) CallbackContext->Request = *Request;
        RtlReleaseSRWLockExclusive(&ZpNativeLock);
    }
    else
    {
        ZpNative_FreeCallbackContext(CallbackContext);
    }
    if (InterlockedDecrement(&CallbackContext->ReferenceCount) == 0)
    {
        Mem_Free(CallbackContext);
    }
    return Status;
}

NTSTATUS
NTAPI
ZpNative_ControlProcess(
    _In_ ULONGLONG ClientId,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_PROCESS_CONTROL Control,
    _In_ ULONG Value,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL)
    {
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ControlProcess(Connection,
                                ProcessId,
                                CreateTime,
                                Control,
                                Value,
                                ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                ZpNative_StatusCallback,
                                CallbackContext,
                                &Request));
}

NTSTATUS
NTAPI
ZpNative_CreateProcessDump(
    _In_ ULONGLONG ClientId,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONG DumpType,
    _In_ ZP_NATIVE_PROCESS_DUMP_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessDump = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_CreateProcessDump(Connection,
                                   ProcessId,
                                   CreateTime,
                                   DumpType,
                                   ZP_NATIVE_PROCESS_DUMP_TIMEOUT_MILLISECONDS,
                                   ZpNative_ProcessDumpCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_ReadProcessMemory(
    _In_ ULONGLONG ClientId,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_ ULONG Length,
    _In_ ZP_NATIVE_PROCESS_MEMORY_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessMemory = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ReadProcessMemory(Connection,
                                   ProcessId,
                                   CreateTime,
                                   Address,
                                   Length,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_ProcessMemoryCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_WriteProcessMemory(
    _In_ ULONGLONG ClientId,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ULONGLONG Address,
    _In_reads_bytes_(DataLength) const VOID* Data,
    _In_ ULONG DataLength,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_WriteProcessMemory(Connection,
                                    ProcessId,
                                    CreateTime,
                                    Address,
                                    Data,
                                    DataLength,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_StatusCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryProcessMemoryMap(
    _In_ ULONGLONG ClientId,
    _In_ ULONG ProcessId,
    _In_ ULONGLONG CreateTime,
    _In_ ZP_NATIVE_PROCESS_MEMORY_ALLOCATIONS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessMemoryAllocations = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryProcessMemoryMap(Connection,
                                       ProcessId,
                                       CreateTime,
                                       ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                       ZpNative_ProcessMemoryAllocationsCallback,
                                       CallbackContext,
                                       &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryProcessMemoryRegions(
    _In_ ULONGLONG ClientId,
    _In_ ULONG SnapshotId,
    _In_ ULONG AllocationIndex,
    _In_ ZP_NATIVE_PROCESS_MEMORY_REGIONS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ProcessMemoryRegions = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryProcessMemoryRegions(Connection,
                                           SnapshotId,
                                           AllocationIndex,
                                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                           ZpNative_ProcessMemoryRegionsCallback,
                                           CallbackContext,
                                           &Request));
}

NTSTATUS
NTAPI
ZpNative_CloseProcessMemoryMap(
    _In_ ULONGLONG ClientId,
    _In_ ULONG SnapshotId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_CloseProcessMemoryMap(Connection,
                                       SnapshotId,
                                       ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                       ZpNative_StatusCallback,
                                       CallbackContext,
                                       &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateExecutionSessions(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_EXECUTION_SESSIONS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionSessions = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateExecutionSessions(Connection,
                                            ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                            ZpNative_ExecutionSessionsCallback,
                                            CallbackContext,
                                            &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryExecutionEnvironment(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_EXECUTION_ENVIRONMENT_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionEnvironment = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryExecutionEnvironment(Connection,
                                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                           ZpNative_ExecutionEnvironmentCallback,
                                           CallbackContext,
                                           &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryExecutionImage(
    _In_ ULONGLONG ClientId,
    _In_reads_(PathLength) PCWCH Path,
    _In_ ULONG PathLength,
    _In_ ZP_NATIVE_EXECUTION_IMAGE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionImage = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryExecutionImage(Connection,
                                     Path,
                                     PathLength,
                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_ExecutionImageCallback,
                                     CallbackContext,
                                     &Request));
}

NTSTATUS
NTAPI
ZpNative_StartExecution(
    _In_ ULONGLONG ClientId,
    _In_ BYTE Engine,
    _In_ BYTE Identity,
    _In_ ULONG SessionId,
    _In_ ULONG Flags,
    _In_reads_(FileNameLength) PCWCH FileName,
    _In_ ULONG FileNameLength,
    _In_reads_opt_(ArgumentsLength) PCWCH Arguments,
    _In_ ULONG ArgumentsLength,
    _In_reads_opt_(WorkingDirectoryLength) PCWCH WorkingDirectory,
    _In_ ULONG WorkingDirectoryLength,
    _In_reads_opt_(VerbLength) PCWCH Verb,
    _In_ ULONG VerbLength,
    _In_reads_opt_(UserNameLength) PCWCH UserName,
    _In_ ULONG UserNameLength,
    _In_reads_opt_(PasswordLength) PCWCH Password,
    _In_ ULONG PasswordLength,
    _In_reads_opt_(AppContainerSidLength) PCWCH AppContainerSid,
    _In_ ULONG AppContainerSidLength,
    _In_reads_bytes_opt_(CustomTokenLength) const VOID* CustomToken,
    _In_ ULONG CustomTokenLength,
    _In_ ZP_NATIVE_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_EXECUTION_START Start = {
        Engine,
        Identity,
        SessionId,
        Flags,
        FileName,
        FileNameLength,
        Arguments,
        ArgumentsLength,
        WorkingDirectory,
        WorkingDirectoryLength,
        Verb,
        VerbLength,
        UserName,
        UserNameLength,
        Password,
        PasswordLength,
        AppContainerSid,
        AppContainerSidLength,
        CustomToken,
        CustomTokenLength
    };
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionJobs = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_StartExecution(Connection,
                                &Start,
                                ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS,
                                ZpNative_ExecutionJobsCallback,
                                CallbackContext,
                                &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateExecutionJobs(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_EXECUTION_JOBS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionJobs = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateExecutionJobs(Connection,
                                        ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                        ZpNative_ExecutionJobsCallback,
                                        CallbackContext,
                                        &Request));
}

NTSTATUS
NTAPI
ZpNative_TerminateExecution(
    _In_ ULONGLONG ClientId,
    _In_ ULONG JobId,
    _In_ ZP_NATIVE_STATUS_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Status = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_TerminateExecution(Connection,
                                    JobId,
                                    ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                    ZpNative_StatusCallback,
                                    CallbackContext,
                                    &Request));
}

NTSTATUS
NTAPI
ZpNative_CreateExecutionStaging(
    _In_ ULONGLONG ClientId,
    _In_reads_(NameLength) PCWCH Name,
    _In_ ULONG NameLength,
    _In_ ZP_NATIVE_EXECUTION_STAGING_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.ExecutionStaging = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_CreateExecutionStaging(Connection,
                                        Name,
                                        NameLength,
                                        ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                        ZpNative_ExecutionStagingCallback,
                                        CallbackContext,
                                        &Request));
}
