typedef struct _ZP_KEYBOARD_CAPTURE
{
    KBDLLHOOKSTRUCT Event;
    WPARAM Message;
    BOOLEAN Captured;
} ZP_KEYBOARD_CAPTURE, *PZP_KEYBOARD_CAPTURE;

static __declspec(thread) PZP_KEYBOARD_CAPTURE ZpKeyboardCapture;

static
LRESULT
CALLBACK
ZpAdministration_KeyboardHook(
    _In_ INT Code,
    _In_ WPARAM WParam,
    _In_ LPARAM LParam)
{
    if (Code == HC_ACTION && ZpKeyboardCapture != NULL && !ZpKeyboardCapture->Captured)
    {
        ZpKeyboardCapture->Event = *(PKBDLLHOOKSTRUCT)LParam;
        ZpKeyboardCapture->Message = WParam;
        ZpKeyboardCapture->Captured = TRUE;
    }
    return CallNextHookEx(NULL, Code, WParam, LParam);
}

static
ZP_STATUS
ZpAdministration_WaitKeyboard(
    _In_ PCZP_STRING_VIEW Identity,
    _Outptr_result_bytebuffer_(*ResponseLength) PBYTE* Response,
    _Out_ PULONG ResponseLength)
{
    ZP_ADMINISTRATION_BUILDER Builder = { 0 };
    ZP_KEYBOARD_CAPTURE Capture = { 0 };
    HHOOK Hook;
    MSG Message;
    WCHAR Key[64], VirtualKey[16], ScanCode[16];
    ULONGLONG Deadline;
    DWORD SessionId, Wait, Timeout;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Identity);
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &SessionId))
    {
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    if (SessionId == 0) return ZpStatus_FromNtStatus(STATUS_NOT_SUPPORTED);
    ZpKeyboardCapture = &Capture;
    Hook = SetWindowsHookExW(WH_KEYBOARD_LL,
                             ZpAdministration_KeyboardHook,
                             GetModuleHandleW(NULL),
                             0);
    if (Hook == NULL)
    {
        ZpKeyboardCapture = NULL;
        return ZpStatus_FromCode(ZpStatusWin32, GetLastError());
    }
    Deadline = GetTickCount64() + 5000;
    while (!Capture.Captured)
    {
        Timeout = (DWORD)min(Deadline > GetTickCount64() ? Deadline - GetTickCount64() : 0, MAXDWORD);
        Wait = MsgWaitForMultipleObjectsEx(0, NULL, Timeout, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        if (Wait == WAIT_TIMEOUT) break;
        if (Wait == WAIT_FAILED)
        {
            Status = NTSTATUS_FROM_WIN32(GetLastError());
            break;
        }
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }
    }
    UnhookWindowsHookEx(Hook);
    ZpKeyboardCapture = NULL;
    if (NT_SUCCESS(Status) && Capture.Captured)
    {
        _ultow_s(Capture.Event.vkCode, VirtualKey, ARRAYSIZE(VirtualKey), 10);
        _ultow_s(Capture.Event.scanCode, ScanCode, ARRAYSIZE(ScanCode), 10);
        if (GetKeyNameTextW((Capture.Event.scanCode << 16) |
                                (Capture.Event.flags & LLKHF_EXTENDED ? 1 << 24 : 0),
                            Key,
                            ARRAYSIZE(Key)) == 0)
        {
            _snwprintf_s(Key, ARRAYSIZE(Key), _TRUNCATE, L"VK 0x%02lX", Capture.Event.vkCode);
        }
        Status = ZpAdministration_AddRecord(&Builder,
                                             ZpAdministrationKindKeyboardEvent,
                                             (ULONG)Capture.Message,
                                             Capture.Event.flags,
                                             Capture.Event.time,
                                             VirtualKey,
                                             Key,
                                             ScanCode,
                                             NULL);
    }
    if (NT_SUCCESS(Status)) Status = ZpAdministration_EncodeBuilder(&Builder, Response, ResponseLength);
    ZpAdministration_FreeBuilder(&Builder);
    return ZpStatus_FromNtStatus(Status);
}
