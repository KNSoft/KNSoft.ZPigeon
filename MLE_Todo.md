# KNSoft.MakeLifeEasier candidates

These candidates remain pending Owner review. `IO_CreatePipe` is locally prototyped in the parent MLE repository and the ZPigeon package copy, but is not committed there; `PS_CreateProcessEx` is not implemented.

## `IO_CreatePipe`

Add a minimal native asynchronous anonymous byte-stream pipe helper to the MLE I/O module. This follows the implementation used by Microsoft Windows Terminal rather than synthesizing a discoverable pipe name:

```c
NTSTATUS
NTAPI
IO_CreatePipe(
    _In_ HANDLE PipeDirectoryHandle,
    _Out_ PHANDLE Handle,
    _Out_ PHANDLE PeerHandle,
    _In_ ULONG Mode,
    _In_ ULONG BufferSize);
```

Reference implementation for review:

```c
NTSTATUS
NTAPI
IO_CreatePipe(
    _In_ HANDLE PipeDirectoryHandle,
    _Out_ PHANDLE Handle,
    _Out_ PHANDLE PeerHandle,
    _In_ ULONG Mode,
    _In_ ULONG BufferSize)
{
    UNICODE_STRING EmptyName = { 0 };
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER Timeout;
    ACCESS_MASK HandleAccess, PeerAccess;
    ULONG HandleShare, PeerShare;
    HANDLE LocalHandle, LocalPeerHandle;
    NTSTATUS Status;

    switch (Mode)
    {
    case FILE_PIPE_INBOUND:
        HandleAccess = SYNCHRONIZE | GENERIC_READ | FILE_WRITE_ATTRIBUTES;
        PeerAccess = SYNCHRONIZE | GENERIC_WRITE | FILE_READ_ATTRIBUTES;
        HandleShare = FILE_SHARE_WRITE;
        PeerShare = FILE_SHARE_READ;
        break;
    case FILE_PIPE_OUTBOUND:
        HandleAccess = SYNCHRONIZE | GENERIC_WRITE | FILE_READ_ATTRIBUTES;
        PeerAccess = SYNCHRONIZE | GENERIC_READ | FILE_WRITE_ATTRIBUTES;
        HandleShare = FILE_SHARE_READ;
        PeerShare = FILE_SHARE_WRITE;
        break;
    case FILE_PIPE_FULL_DUPLEX:
        HandleAccess = PeerAccess = SYNCHRONIZE | GENERIC_READ | GENERIC_WRITE;
        HandleShare = PeerShare = FILE_SHARE_READ | FILE_SHARE_WRITE;
        break;
    default:
        return STATUS_INVALID_PARAMETER;
    }

    NT_InitObject(&ObjectAttributes,
                  &EmptyName,
                  OBJ_CASE_INSENSITIVE,
                  PipeDirectoryHandle);
    Status = NtCreateNamedPipeFile(&LocalHandle,
                                   HandleAccess,
                                   &ObjectAttributes,
                                   &IoStatusBlock,
                                   HandleShare,
                                   FILE_CREATE,
                                   0,
                                   FILE_PIPE_BYTE_STREAM_TYPE,
                                   FILE_PIPE_BYTE_STREAM_MODE,
                                   FILE_PIPE_QUEUE_OPERATION,
                                   1,
                                   BufferSize,
                                   BufferSize,
                                   NT_MillisecondsToTimeout(&Timeout, 1000));
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ObjectAttributes.RootDirectory = LocalHandle;
    Status = NtCreateFile(&LocalPeerHandle,
                          PeerAccess,
                          &ObjectAttributes,
                          &IoStatusBlock,
                          NULL,
                          0,
                          PeerShare,
                          FILE_OPEN,
                          FILE_NON_DIRECTORY_FILE,
                          NULL,
                          0);
    if (!NT_SUCCESS(Status))
    {
        NtClose(LocalHandle);
        return Status;
    }

    *Handle = LocalHandle;
    *PeerHandle = LocalPeerHandle;
    return STATUS_SUCCESS;
}
```

Implementation:

- Accept only `FILE_PIPE_INBOUND`, `FILE_PIPE_OUTBOUND`, or `FILE_PIPE_FULL_DUPLEX`. `Mode` describes `Handle`; derive the opposite access and matching share access for `PeerHandle`.
- Require the caller to supply an open `\Device\NamedPipe\` directory handle. MLE does not open, cache, close, or own that handle; a high-frequency caller may cache it, while a low-frequency caller may open it only around `IO_CreatePipe`.
- Use an empty `UNICODE_STRING` relative to that directory in `NtCreateNamedPipeFile`. Do not generate a name, ACL, sequence, process-ID suffix, collision retry, or Win32 fallback. The resulting anonymous pipe cannot be enumerated through NPFS directory queries.
- Create `Handle` asynchronously with byte-stream type and mode, queued completion, one instance, and the caller's `BufferSize` as both quotas. Use a one-second relative default timeout.
- For inbound mode, request `SYNCHRONIZE | GENERIC_READ | FILE_WRITE_ATTRIBUTES` on `Handle` and `SYNCHRONIZE | GENERIC_WRITE | FILE_READ_ATTRIBUTES` on `PeerHandle`; reverse these for outbound mode. Request read/write access and sharing on both endpoints for full duplex.
- Open `PeerHandle` with `NtCreateFile`, using `Handle` as `RootDirectory`, the same empty name, `FILE_OPEN`, and `FILE_NON_DIRECTORY_FILE`. Do not use `NtOpenFile` or a named path.
- Keep both endpoints non-inheritable. Publish output handles only after both calls succeed; close partial handles with `NtClose` on failure. Do not initialize failed outputs.

ZPigeon use:

- For each Terminal creation, open `\Device\NamedPipe\` with `NtCreateFile`, `SYNCHRONIZE | GENERIC_READ`, shared read/write, and `FILE_SYNCHRONOUS_IO_NONALERT`; close the directory handle immediately after `IO_CreatePipe`. Terminal creation is not a hot path, so ZPigeon does not cache it.
- Create two 128 KiB asynchronous pipes: `FILE_PIPE_OUTBOUND` for ConPTY input and `FILE_PIPE_INBOUND` for ConPTY output. Pass their peer endpoints to `CreatePseudoConsole` and retain the two local endpoints in the Client Terminal channel. These pipes are local to ZPigeon Client and ConPTY; they are unrelated to the remote ZPigeon Server/Client transport.
- Windows Terminal normally links the repository's `src/winconpty` implementation and passes one duplex endpoint twice to `ConptyCreatePseudoConsole`; only its Windows-internal build maps that call to the system `CreatePseudoConsole`. ZPigeon uses the Windows system ConPTY API, where reusing one duplex endpoint caused sustained `cmd.exe` output to terminate after its initial banner with `STATUS_CONTROL_C_EXIT` on the validated build. Two directional pipes pass the public API distinct streams while retaining the same asynchronous I/O behavior.
- Use a dedicated output thread, a manual-reset event-backed `NtReadFile`, and process/event waits. Remove `FilePipeLocalInformation` polling and timed delays.
- Serialize input writes with independent event/`IO_STATUS_BLOCK` state, preserve pending input data until completion, and never allow more than one input write in flight. The next receive waits for the prior write; accepted input replenishes the corresponding channel window without hidden buffering.
- On Server channel close or transport disconnect, close the pseudoconsole, cancel pipe I/O with `NtCancelIoFileEx`, wait for the output thread, and release the channel. Reconnection never resumes an old Terminal channel.
- The Terminal root process means the first process launched from the Server-supplied command line, such as `cmd.exe` or `powershell.exe`; it is not the ZPigeon Client process. Root-process exit is local session state, whereas Server channel/connection loss is the remote ownership boundary.

## `PS_CreateProcessEx`

Add a native process creation helper alongside the existing `PS_CreateProcess`; do not overload the existing Win32-oriented contract:

```c
NTSTATUS
NTAPI
PS_CreateProcessEx(
    _In_ PCUNICODE_STRING ImagePath,
    _In_ PCUNICODE_STRING CommandLine,
    _In_opt_ PCUNICODE_STRING CurrentDirectory,
    _In_opt_ PVOID Environment,
    _In_ ULONG ProcessFlags,
    _In_ ULONG ThreadFlags,
    _In_reads_opt_(AttributeCount) const PS_ATTRIBUTE* Attributes,
    _In_ ULONG AttributeCount,
    _Out_ PHANDLE ProcessHandle,
    _Out_ PHANDLE ThreadHandle,
    _Out_opt_ PCLIENT_ID ClientId);
```

Implementation:

- Build normalized `RTL_USER_PROCESS_PARAMETERS` with `RtlCreateProcessParametersEx`; use the current process environment when `Environment` is null.
- Build one contiguous `PS_ATTRIBUTE_LIST`. Always include `PS_ATTRIBUTE_IMAGE_NAME`, then append caller attributes after validating the count and allocation size.
- Call `NtCreateUserProcess` with minimum useful process/thread access, caller flags, zeroed `PS_CREATE_INFO` with `Size` set, and the constructed attribute list.
- Return the `CLIENT_ID` from `PS_ATTRIBUTE_CLIENT_ID` when requested; add that attribute internally rather than requiring the caller to supply it.
- Destroy process parameters and free the attribute list on every path. Publish handles only on success; close partial handles with `NtClose`.
- Do not add `CreateProcessW`, `CreateProcessInternalW`, legacy-console, or older-Windows fallback paths.

ConPTY constraint: NDK defines `ProcThreadAttributePseudoConsole` for a Win32 `PROC_THREAD_ATTRIBUTE_LIST`, but the native `PS_ATTRIBUTE_NUM` set has no verified pseudo-console counterpart. Do not invent or alias a `PS_ATTRIBUTE` value. Before using this helper for Terminal, verify the private Base/ConPTY conversion contract on supported Windows 10+ builds. If no stable native contract exists, keep `PS_CreateProcessEx` as a general native helper and isolate the Terminal child launch behind the minimum `CreateProcessW` extended-startup call; `CreatePseudoConsole`, `ResizePseudoConsole`, and `ClosePseudoConsole` remain the other ConPTY-layer APIs.
