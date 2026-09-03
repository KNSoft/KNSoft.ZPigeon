NTSTATUS
NTAPI
ZpNative_EnumerateAdministration(
    _In_ ULONGLONG ClientId,
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_ ZP_NATIVE_ADMINISTRATION_CALLBACK Callback,
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
    CallbackContext->Callback.Administration = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateAdministration(Connection,
                                         ModuleId,
                                         OperationId,
                                         (ModuleId == ZP_UPDATE_MODULE_ID ||
                                           (ModuleId == ZP_SOFTWARE_MODULE_ID &&
                                            (OperationId == ZP_ADMINISTRATION_OPERATION_ENUMERATE_FEATURES ||
                                             OperationId == ZP_ADMINISTRATION_OPERATION_ENUMERATE_PACKAGE_PROVIDERS))) ?
                                              ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS :
                                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                         ZpNative_AdministrationCallback,
                                         CallbackContext,
                                         &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryAdministration(
    _In_ ULONGLONG ClientId,
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_reads_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_ ZP_NATIVE_ADMINISTRATION_CALLBACK Callback,
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
    CallbackContext->Callback.Administration = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryAdministration(Connection,
                                     ModuleId,
                                     OperationId,
                                     Identity,
                                     IdentityLength,
                                     ModuleId == ZP_SOFTWARE_MODULE_ID &&
                                             OperationId == ZP_ADMINISTRATION_OPERATION_QUERY_PACKAGES ?
                                         ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS :
                                         ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_AdministrationCallback,
                                     CallbackContext,
                                     &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryAdministrationData(
    _In_ ULONGLONG ClientId,
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_reads_opt_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_ ZP_NATIVE_ADMINISTRATION_DATA_CALLBACK Callback,
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
    CallbackContext->Callback.AdministrationData = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryAdministrationData(Connection,
                                         ModuleId,
                                         OperationId,
                                         Identity,
                                         IdentityLength,
                                         ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                         ZpNative_AdministrationDataCallback,
                                         CallbackContext,
                                         &Request));
}

NTSTATUS
NTAPI
ZpNative_ControlAdministration(
    _In_ ULONGLONG ClientId,
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_ BYTE Action,
    _In_reads_opt_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_reads_opt_(SecretLength) PCWCH Secret,
    _In_ ULONG SecretLength,
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
        ZpServer_ControlAdministration(Connection,
                                       ModuleId,
                                       OperationId,
                                       Action,
                                       Identity,
                                       IdentityLength,
                                       Argument,
                                       ArgumentLength,
                                       Secret,
                                       SecretLength,
                                       ModuleId == ZP_UPDATE_MODULE_ID ?
                                           ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS :
                                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                       ZpNative_StatusCallback,
                                        CallbackContext,
                                        &Request));
}

NTSTATUS
NTAPI
ZpNative_ControlAdministrationResult(
    _In_ ULONGLONG ClientId,
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_ BYTE Action,
    _In_reads_opt_(IdentityLength) PCWCH Identity,
    _In_ ULONG IdentityLength,
    _In_reads_opt_(ArgumentLength) PCWCH Argument,
    _In_ ULONG ArgumentLength,
    _In_reads_opt_(SecretLength) PCWCH Secret,
    _In_ ULONG SecretLength,
    _In_ ZP_NATIVE_ADMINISTRATION_DATA_CALLBACK Callback,
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
    CallbackContext->Callback.AdministrationData = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_ControlAdministrationResult(
            Connection,
            ModuleId,
            OperationId,
            Action,
            Identity,
            IdentityLength,
            Argument,
            ArgumentLength,
            Secret,
            SecretLength,
            ModuleId == ZP_SOFTWARE_MODULE_ID &&
                    OperationId == ZP_ADMINISTRATION_OPERATION_CONTROL_FEATURE ?
                ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS :
                ZP_NATIVE_TIMEOUT_MILLISECONDS,
            ZpNative_AdministrationDataCallback,
            CallbackContext,
            &Request));
}

NTSTATUS
NTAPI
ZpNative_ControlAdministrationData(
    _In_ ULONGLONG ClientId,
    _In_ BYTE ModuleId,
    _In_ BYTE OperationId,
    _In_ BYTE Action,
    _In_ ULONG Flags,
    _In_reads_bytes_(IdentityLength) const VOID* Identity,
    _In_ ULONG IdentityLength,
    _In_reads_bytes_opt_(DataLength) const VOID* Data,
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
        ZpServer_ControlAdministrationData(Connection,
                                           ModuleId,
                                           OperationId,
                                           Action,
                                           Flags,
                                           Identity,
                                           IdentityLength,
                                           Data,
                                           DataLength,
                                           ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                           ZpNative_StatusCallback,
                                           CallbackContext,
                                           &Request));
}

NTSTATUS
NTAPI
ZpNative_EnumerateBrowsers(
    _In_ ULONGLONG ClientId,
    _In_ ZP_NATIVE_BROWSER_CALLBACK Callback,
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
    CallbackContext->Callback.Browser = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_EnumerateBrowsers(Connection,
                                   ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                   ZpNative_BrowserCallback,
                                   CallbackContext,
                                   &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryBrowser(
    _In_ ULONGLONG ClientId,
    _In_ BYTE Browser,
    _In_ BYTE Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_reads_opt_(UserDataLength) PCWCH UserData,
    _In_ ULONG UserDataLength,
    _In_ ULONGLONG Cursor,
    _In_ ULONG Limit,
    _In_ ZP_NATIVE_BROWSER_CALLBACK Callback,
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
    CallbackContext->Callback.Browser = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryBrowser(Connection,
                              Browser,
                              Kind,
                              Profile,
                              ProfileLength,
                              UserData,
                              UserDataLength,
                              Cursor,
                              Limit,
                              ZP_NATIVE_TIMEOUT_MILLISECONDS,
                              ZpNative_BrowserCallback,
                              CallbackContext,
                              &Request));
}

NTSTATUS
NTAPI
ZpNative_InspectBrowserProfile(
    _In_ ULONGLONG ClientId,
    _In_ BYTE Browser,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_reads_opt_(UserDataLength) PCWCH UserData,
    _In_ ULONG UserDataLength,
    _In_ ZP_NATIVE_BROWSER_PROFILE_INSPECTION_CALLBACK Callback,
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
    CallbackContext->Callback.BrowserProfileInspection = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_InspectBrowserProfile(Connection,
                                       Browser,
                                       Profile,
                                       ProfileLength,
                                       UserData,
                                       UserDataLength,
                                       ZP_NATIVE_LONG_OPERATION_TIMEOUT_MILLISECONDS,
                                       ZpNative_BrowserProfileInspectionCallback,
                                       CallbackContext,
                                       &Request));
}

NTSTATUS
NTAPI
ZpNative_OpenBrowserDocument(
    _In_ ULONGLONG ClientId,
    _In_ BYTE Browser,
    _In_ BYTE Kind,
    _In_reads_(ProfileLength) PCWCH Profile,
    _In_ ULONG ProfileLength,
    _In_reads_opt_(UserDataLength) PCWCH UserData,
    _In_ ULONG UserDataLength,
    _In_ ZP_NATIVE_BROWSER_DOCUMENT_CALLBACK Callback,
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
    CallbackContext->Callback.BrowserDocument = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_OpenBrowserDocument(Connection,
                                     Browser,
                                     Kind,
                                     Profile,
                                     ProfileLength,
                                     UserData,
                                     UserDataLength,
                                     ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                     ZpNative_BrowserDocumentCallback,
                                     CallbackContext,
                                     &Request));
}

NTSTATUS
NTAPI
ZpNative_QueryBrowserDocumentNode(
    _In_ ULONGLONG ClientId,
    _In_ ULONG SnapshotId,
    _In_ ULONG NodeId,
    _In_ ULONG Cursor,
    _In_ ULONG Limit,
    _In_ ZP_NATIVE_BROWSER_DOCUMENT_CALLBACK Callback,
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
    CallbackContext->Callback.BrowserDocument = Callback;
    return ZpNative_SendStatusRequest(
        CallbackContext,
        ZpServer_QueryBrowserDocumentNode(Connection,
                                          SnapshotId,
                                          NodeId,
                                          Cursor,
                                          Limit,
                                          ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                          ZpNative_BrowserDocumentCallback,
                                          CallbackContext,
                                          &Request));
}

NTSTATUS
NTAPI
ZpNative_CloseBrowserDocument(
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
        ZpServer_CloseBrowserDocument(Connection,
                                      SnapshotId,
                                      ZP_NATIVE_TIMEOUT_MILLISECONDS,
                                      ZpNative_StatusCallback,
                                      CallbackContext,
                                      &Request));
}

static
NTSTATUS
ZpNative_Wmi(
    _In_ ULONGLONG ClientId,
    _In_ BYTE OperationId,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_opt_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ZP_CONNECTION_HANDLE Connection;
    PZP_NATIVE_CALLBACK_CONTEXT CallbackContext;
    ZP_REQUEST_HANDLE Request;
    NTSTATUS Status;

    if (Callback == NULL) return STATUS_INVALID_PARAMETER;
    Connection = ZpNative_GetConnection(ClientId);
    if (Connection == NULL) return STATUS_DEVICE_NOT_CONNECTED;
    CallbackContext = ZpNative_CreateCallbackContext(Connection, Context);
    if (CallbackContext == NULL)
    {
        ZpConnection_Release(Connection);
        return STATUS_NO_MEMORY;
    }
    CallbackContext->Callback.Wmi = Callback;
    Status = OperationId == ZP_WMI_OPERATION_ENUMERATE_NAMESPACES ?
                 ZpServer_EnumerateWmiNamespaces(Connection,
                                                 Namespace,
                                                 NamespaceLength,
                                                 ZP_NATIVE_WMI_TIMEOUT_MILLISECONDS,
                                                 ZpNative_WmiCallback,
                                                 CallbackContext,
                                                 &Request) :
             OperationId == ZP_WMI_OPERATION_ENUMERATE_CLASSES ?
                 ZpServer_EnumerateWmiClasses(Connection,
                                              Namespace,
                                              NamespaceLength,
                                              ZP_NATIVE_WMI_TIMEOUT_MILLISECONDS,
                                              ZpNative_WmiCallback,
                                              CallbackContext,
                                              &Request) :
                 ZpServer_QueryWmi(Connection,
                                   Namespace,
                                   NamespaceLength,
                                   Query,
                                   QueryLength,
                                   Limit,
                                   Flags,
                                   ZP_NATIVE_WMI_TIMEOUT_MILLISECONDS,
                                   ZpNative_WmiCallback,
                                   CallbackContext,
                                   &Request);
    return ZpNative_SendStatusRequest(CallbackContext, Status);
}

NTSTATUS
NTAPI
ZpNative_EnumerateWmiNamespaces(
    _In_ ULONGLONG ClientId,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return ZpNative_Wmi(ClientId, ZP_WMI_OPERATION_ENUMERATE_NAMESPACES,
                        Namespace,
                        NamespaceLength,
                        NULL,
                        0,
                        ZP_WMI_MAX_ROWS,
                        0,
                        Callback,
                        Context);
}

NTSTATUS
NTAPI
ZpNative_EnumerateWmiClasses(
    _In_ ULONGLONG ClientId,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return ZpNative_Wmi(ClientId, ZP_WMI_OPERATION_ENUMERATE_CLASSES,
                        Namespace,
                        NamespaceLength,
                        NULL,
                        0,
                        ZP_WMI_MAX_ROWS,
                        0,
                        Callback,
                        Context);
}

NTSTATUS
NTAPI
ZpNative_QueryWmi(
    _In_ ULONGLONG ClientId,
    _In_reads_(NamespaceLength) PCWCH Namespace,
    _In_ ULONG NamespaceLength,
    _In_reads_(QueryLength) PCWCH Query,
    _In_ ULONG QueryLength,
    _In_ ULONG Limit,
    _In_ ULONG Flags,
    _In_ ZP_NATIVE_WMI_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return ZpNative_Wmi(ClientId, ZP_WMI_OPERATION_QUERY,
                        Namespace,
                        NamespaceLength,
                        Query,
                        QueryLength,
                        Limit,
                        Flags,
                        Callback,
                        Context);
}
