# KNSoft.MakeLifeEasier candidates

These candidates are intentionally not implemented yet. They should be reviewed and added to `KNSoft.MakeLifeEasier` together, then consumed by ZPigeon through the updated MLE package.

## `IO_CreatePipe`

Add a minimal native anonymous byte-stream pipe helper to the MLE I/O module:

```c
NTSTATUS
NTAPI
IO_CreatePipe(
    _Out_ PHANDLE ReadHandle,
    _Out_ PHANDLE WriteHandle,
    _In_ ULONG BufferSize);
```

Implementation:

- Generate a process-unique name below `\Device\NamedPipe\`; use a monotonic 64-bit sequence combined with the process ID and retry only `STATUS_OBJECT_NAME_COLLISION`.
- Create the read/server endpoint with `NtCreateNamedPipeFile`, byte-stream type and mode, queued completion, one instance, `FILE_SYNCHRONOUS_IO_NONALERT`, and `OBJ_DONT_REPARSE`.
- Open the write/client endpoint with `NtOpenFile`, matching share access and synchronous options.
- Request only `FILE_READ_DATA | SYNCHRONIZE` for the read endpoint and `FILE_WRITE_DATA | SYNCHRONIZE` for the write endpoint.
- Use `BufferSize` for inbound/outbound quota; reject zero rather than adding a default.
- Publish output handles only after both syscalls succeed. On failure, close every created handle with `NtClose`; do not initialize outputs or add a Win32 fallback.
- Keep handles non-inheritable. ConPTY receives them explicitly through `CreatePseudoConsole`.

ZPigeon use: replace both `CreatePipe` calls in the Client-side Terminal executor. The read endpoint feeds ConPTY input and the write endpoint is retained by ZPigeon; the second pipe reverses those roles for ConPTY output.

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
