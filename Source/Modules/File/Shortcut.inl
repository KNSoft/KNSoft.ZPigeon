static
NTSTATUS
ZpFile_QueryShortcut(
    _In_ PCZP_STRING_VIEW Path,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    const ULONG Capacity = 32768;
    PUNICODE_STRING PathString;
    PWSTR Target;
    PCWSTR Extension;
    ULONG Length;
    HRESULT Result;
    NTSTATUS Status;
    BOOLEAN Uninitialize = FALSE;

    PathString = ZpFile_CopyPath(Path);
    Target = Mem_Alloc((SIZE_T)Capacity * sizeof(WCHAR));
    if (PathString == NULL || Target == NULL)
    {
        if (PathString != NULL) NT_FreeStringW(PathString);
        Mem_Free(Target);
        return STATUS_NO_MEMORY;
    }
    Extension = wcsrchr(PathString->Buffer, L'.');
    if (Extension != NULL && _wcsicmp(Extension, L".lnk") == 0)
    {
        Result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
        if (Result == RPC_E_CHANGED_MODE) Result = S_OK;
        else if (SUCCEEDED(Result)) Uninitialize = TRUE;
        if (SUCCEEDED(Result)) Result = Shell_GetLinkPath(PathString->Buffer, Target, Capacity);
        Status = SUCCEEDED(Result) ? STATUS_SUCCESS : NTSTATUS_FROM_WIN32(HRESULT_CODE(Result));
        Length = NT_SUCCESS(Status) ? (ULONG)wcslen(Target) : 0;
    }
    else if (Extension != NULL && _wcsicmp(Extension, L".url") == 0)
    {
        Length = GetPrivateProfileStringW(L"InternetShortcut",
                                           L"URL",
                                           NULL,
                                           Target,
                                           Capacity,
                                           PathString->Buffer);
        Status = Length == 0 ? NTSTATUS_FROM_WIN32(ERROR_INVALID_DATA) :
                 Length >= Capacity - 1 ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
        Length = 0;
    }
    if (NT_SUCCESS(Status))
    {
        Status = ZpFile_EncodePath(Target, Length, NULL, 0, ResponseLength);
        if (NT_SUCCESS(Status))
        {
            *Response = Mem_Alloc(*ResponseLength);
            if (*Response == NULL) Status = STATUS_NO_MEMORY;
        }
        if (NT_SUCCESS(Status))
        {
            Status = ZpFile_EncodePath(Target,
                                       Length,
                                       *Response,
                                       *ResponseLength,
                                       ResponseLength);
        }
    }
    if (Uninitialize) CoUninitialize();
    NT_FreeStringW(PathString);
    Mem_Free(Target);
    return Status;
}
