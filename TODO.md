# KNSoft.ZPigeon TODO

本文件记录已经确认有产品价值、但当前迭代明确不实施的项目工作，以及等待 Owner 审核的 KNSoft.MakeLifeEasier 候选。项目尚未发布，不为未完成设计保留兼容层。

## 近期工程工作

- 修正 Client 分模块日志缓存容量：当前缓存槽位少于可能生成的日志文件数，可能静默遗漏部分模块日志。
- 为 Client 日志增加明确的单文件上限、轮换数量和总磁盘预算，禁止无限追加。
- 补齐 QUIC 诊断日志：统一 MsQuic 事件、连接标识和 ZPigeon 请求上下文，确认 Debug/Release 的采集开销、敏感字段边界及故障导出方式。
- 统一整治代码分析告警：分别运行原生 `/analyze` 与托管 Roslyn/.NET Analyzer，逐项区分真实问题、
  已确认误报和第三方头文件问题；修正真实问题后再使用最小范围抑制，不恢复全局 `RunCodeAnalysis` 入口。
- 将远程浏览器 CDP 适配器下沉到原生 Client，在浏览器回环边界直接解析 screencast 事件并解码 JPEG，
  彻底消除 CDP Base64 在 Tunnel 两端的编解码 CPU；当前 Tunnel 自适应压缩已避免其线速膨胀，Web 也不
  再生成 UTF-16 Base64 中间字符串或向管理浏览器发送 Base64。
- 为所有 Release C/C++ 项目启用全程序优化，并以 `KNSoftQuicIntegration=StaticLTCGPGO`、非增量链接、
  `/OPT:REF` 和 `/OPT:ICF` 链接 Client；保留 Release PDB。一次本地对比中，Client 在保留静态 CRT 和静态
  MsQuic 的情况下由 2,503,168 字节降至 757,760 字节。
- Windows 可选功能的非管理员层级：当前使用受支持的 `Win32_OptionalFeature` 保证非管理员可枚举；公开
  DISM 父级查询在线系统时需要提升权限，而 OptionalFeatures/CBS 的层级接口是版本相关私有 ABI，暂不接入。

## 产品能力

1. 持久 Client Registry：以客户端公钥指纹为主键，保存名称、标签、备注、首次/最后在线时间、最后地址、OS 和 Client 构建版本。
2. 运行时 Capability Snapshot：记录协商后的核心 Revision、逐模块 Version、Transport 和可用的系统能力，页面只展示真实可执行的操作。
3. Fleet Dashboard：汇总在线状态、版本、健康度、待重启、更新与告警，并支持筛选和标签视图。
4. 批量作业：固定作业类型、目标集合、并发上限、灰度批次、逐 Client 结果和重启需求；不引入任意脚本式编排。
5. 操作审计：记录操作者、目标 Client、操作类型、参数摘要、开始/结束时间和原始结果域，不记录密码、密钥或文件内容。
6. Client 诊断中心：导出连接状态、传输统计、模块版本、近期错误和日志包，便于远程定位弱网与权限问题。
7. 资产快照与差异：对软硬件、服务、网络和安全配置生成有界快照，支持差异查看与导出。
8. 健康告警：对离线、磁盘/电池异常、连续请求失败、版本落后和待重启状态产生可去重告警。
9. Agent 安装、配置与更新：支持用户身份、普通用户、管理员和 SYSTEM 服务四种运行上下文；服务与交互会话辅助进程的完整架构另行设计。
10. Enrollment：为未知客户端公钥增加一次性入组凭据、审批、撤销和重新入组流程，不能把自签名身份等同于部署授权。
11. AI 管理入口：基于现有 Server 管理能力接入 MCP 等入口，为局域网内已授权终端提供 AI 辅助管理。

## KNSoft.MakeLifeEasier 候选

These candidates remain pending Owner review. `IO_CreatePipe` is locally prototyped in the parent MLE repository and the ZPigeon package copy, but is not committed there; `PS_CreateProcessEx` is not implemented.

### File owner query and forced handle release

The ZPigeon File module currently implements this directly. Consider moving the reusable NT handle work into MLE after Owner review:

```c
typedef struct _IO_PROCESS_FILE_HANDLE_RESULT
{
    ULONG ProcessId;
    NTSTATUS Status;
    ULONG ClosedHandleCount;
} IO_PROCESS_FILE_HANDLE_RESULT, *PIO_PROCESS_FILE_HANDLE_RESULT;

NTSTATUS
NTAPI
IO_QueryFileProcessIds(
    _In_ HANDLE FileHandle,
    _Outptr_result_buffer_(*ProcessCount) PULONG* ProcessIds,
    _Out_ PULONG ProcessCount);

NTSTATUS
NTAPI
IO_CloseProcessFileHandles(
    _In_ HANDLE FileHandle,
    _In_reads_(ProcessCount) const ULONG* ProcessIds,
    _In_ ULONG ProcessCount,
    _Out_writes_(ProcessCount) PIO_PROCESS_FILE_HANDLE_RESULT Results);
```

Implementation:

- Require `FileHandle` to have `FILE_READ_ATTRIBUTES | SYNCHRONIZE`; support files and directories and do not require
  `FILE_NON_DIRECTORY_FILE` or `FILE_DIRECTORY_FILE`.
- Query `FileProcessIdsUsingFileInformation` with a geometrically growing buffer and return the kernel-provided PID order
  unchanged. Do not filter or deduplicate PIDs.
- For forced release, query `FileIdInformation` for the target and `SystemExtendedHandleInformation` once. Determine the file
  object type index from `FileHandle`, then scan only matching type/PID entries.
- Open each target process with `PROCESS_DUP_HANDLE`. Duplicate a candidate with only `SYNCHRONIZE` (`FileIdInformation`
  requires no file access right), compare its `FILE_ID_INFORMATION`, then close a confirmed source handle with
  `NtDuplicateObject(DUPLICATE_CLOSE_SOURCE)`.
- Wait for `STATUS_PENDING` file-information queries and use the resulting `IO_STATUS_BLOCK.Status`. Skip candidates that
  cannot be duplicated or identified; report an error only after a matching handle cannot be closed. Return
  `STATUS_NOT_FOUND` when no matching handle remains.
- Refuse the current process to avoid closing MLE/caller-owned handles. Do not add process creation-time validation,
  Restart Manager fallback, filesystem locality checks, Win32 handle APIs, or compatibility paths.
- This operation is inherently racy because a remote numeric handle can be closed and reused after enumeration. Callers must
  require explicit destructive-operation confirmation and present per-process raw `NTSTATUS` results.

### `IO_CreatePipe`

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

### `PS_CreateProcessEx`

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
