# KNSoft.ZPigeon

KNSoft.ZPigeon 是面向 Windows 10 及以上系统的远程管理项目。原生 SDK 使用纯 C；第一版使用 QUIC 与 TLS 1.3，提供持久化客户端身份、模块协商、异步 Request、流式 Channel，以及 System、Process、Service、File、Terminal、EventLog 和 Registry 模块。

同一个 Solution 直接输出：

- `KNSoft.ZPigeon.Protocol`：Transport 无关的帧、消息和模块 Codec；
- `KNSoft.ZPigeon.Client.SDK`：被控端连接、重试、请求执行和本机管理操作；
- `KNSoft.ZPigeon.Server.SDK`：管理端监听、认证、连接持有和异步操作发起。
- `KNSoft.ZPigeon.Client.exe`：启动 Client SDK，连接本地 Server，并把网络及各模块日志分别写入同目录 `logs`；
- `KNSoft.ZPigeon.Server.Native.dll`：供托管程序直接调用 Server SDK 的 C ABI 桥；
- `KNSoft.ZPigeon.Server.Managed`：封装 Native DLL 的可复用 .NET Server SDK 与 Terminal 会话 API；
- `KNSoft.ZPigeon.Web`：只负责本地回环 WebSocket 和 UI 的 C# Web 管理端。

ZPigeon 本身不生成 NuGet 包；三个原生依赖仍由现有 `packages.config` 提供。

完整协议与安全模型见 [Design.md](Design.md)，当前实现状态见 [Progress.md](Progress.md)，第一版交付门槛见 [Release.md](Release.md)。

> 产品角色固定为 Client 运行在被控主机、Server 作为管理端。所有现有管理模块均由 Server 发起、Client 本机执行；Client 不发起管理业务 Request。当前状态和后续入口见 [Handoff.md](Handoff.md)。

## 构建与测试

需要 Visual Studio 2026 C++ 工具链、Windows SDK 10.0.26100.0、.NET 10 SDK，以及项目 `packages.config` 中声明的 KNSoft.MakeLifeEasier、KNSoft.NDK 和 KNSoft.Quic 原生依赖。

在 Visual Studio Developer PowerShell 中执行：

```powershell
msbuild Source\KNSoft.ZPigeon.slnx /t:Restore
msbuild Source\KNSoft.ZPigeon.slnx /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m
Source\OutDir\x64\Debug\UnitTest.exe -Run
```

当前改动已通过 x64 Debug/Release 全 Solution Rebuild；Debug 332/332、Release 331/331 测试及两个配置的 ConsumerTest 均通过，并完成 x64 Debug 的 Web、Managed/Native Server、QUIC、Client 实际 localhost 冒烟。Debug 比 Release 多一项仅验证 Debug 5 秒重连间隔的断言。

## 本地试用

1. 运行 `Source\OutDir\x64\Debug\KNSoft.ZPigeon.Web.exe`；首次运行会在同目录生成仅供本地试用的根证书与 Server 证书。
2. 运行同目录的 `KNSoft.ZPigeon.Client.exe`。
3. 打开 `http://127.0.0.1:5080`。Web 和 Server 仅监听本地回环；Client 连接 `127.0.0.1:4433`。

当前页面可探测远端主机上的 `cmd`、Windows PowerShell 和 PowerShell，新建/关闭多个 Shell、切换标签并进行完整 ConPTY 命令交互；也提供文件、进程、服务和注册表可视化管理。文件页支持分页浏览、可修改属性、CRC32/MD5/SHA-1/SHA-256、流式上传/下载、重命名和删除；进程页支持实时 CPU/内存等信息、映像路径和命令行详情及结束进程，并在页面不活跃时停止刷新；服务页支持属性、启动和停止。EventLog 支持 Bookmark 分页查询、频道启停和清除，不包含实时订阅。

Client 的 `network.log` 与各业务模块日志位于同目录 `logs`。试用 EXE 使用当前用户范围的持久 CNG 身份密钥；SDK 默认仍使用机器范围，服务化部署不改变原安全边界。

## 最小 Server 生命周期

下面示例只展示 SDK 入口。`Certificate` 必须是包含可用私钥的 Server 证书；其名称需覆盖 `ServerName`。`Start` 和 `Stop` 是异步状态转换，应用应在状态回调中等待 Running/Stopped，再进入下一阶段。

```c
#include <KNSoft/ZPigeon/Server.h>
#include <KNSoft/ZPigeon/File.h>
#include <KNSoft/ZPigeon/System.h>

static
VOID
NTAPI
ServerStateCallback(
    ZP_SERVER_HANDLE Server,
    ZP_SERVER_STATE State,
    ZP_STATUS Status,
    PVOID Context)
{
    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
}

static
VOID
NTAPI
ServerConnectionCallback(
    ZP_SERVER_HANDLE Server,
    ZP_CONNECTION_HANDLE Connection,
    ZP_CONNECTION_PHASE Phase,
    ZP_STATUS Status,
    PVOID Context)
{
    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Connection);
    UNREFERENCED_PARAMETER(Phase);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
}

ZP_STATUS
StartServer(
    PCCERT_CONTEXT Certificate,
    ZP_SERVER_HANDLE* Server)
{
    static const ZP_MODULE_RECORD Modules[] = {
        { ZP_SYSTEM_MODULE_ID, ZP_SYSTEM_MODULE_VERSION },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION }
    };
    static const ZP_LISTENER_ENDPOINT Listener = {
        ZpTransportQuic, L"0.0.0.0", 4433, NULL
    };
    ZP_SERVER_DEPLOYMENT Deployment = {
        L"server.example", Certificate
    };
    ZP_SERVER_CONFIG Config = { 0 };
    NTSTATUS Status;

    Config.Size = sizeof(Config);
    Config.Listeners = &Listener;
    Config.ListenerCount = 1;
    Config.Deployments = &Deployment;
    Config.DeploymentCount = 1;
    Config.Modules = Modules;
    Config.ModuleCount = ARRAYSIZE(Modules);
    Config.StateCallback = ServerStateCallback;
    Config.ConnectionCallback = ServerConnectionCallback;

    Status = ZpServer_Create(&Config, Server);
    return NT_SUCCESS(Status) ?
               ZpServer_Start(*Server) :
               ZpStatus_FromNtStatus(Status);
}
```

连接进入 `ZpConnectionPhaseReady` 后，Server 应调用 `ZpConnection_AddRef` 持有连接，并通过 `ZpServer_*` API 对该 Client 发起操作；收到 Closed 后释放持有的连接。通过部署根认证的 Server 对 Client 拥有完整管理能力，不再建立操作级 Read/Control 授权层。

停止顺序为 `ZpServer_Stop`，等待 `ZpServerStateStopped`，最后调用 `ZpServer_Close`。

## 最小 Client 生命周期

Client 配置接收 Deployment 根证书的 DER Buffer，而不是证书句柄。`ServerName` 同时参与 SNI 和证书名称验证；`Host` 可为实际 IP 或 DNS 名称。`ClientKeyName = NULL` 时使用默认持久化 P-256 身份键名。

```c
#include <KNSoft/ZPigeon/Client.h>

static
VOID
NTAPI
ClientStateCallback(
    ZP_CLIENT_HANDLE Client,
    ZP_CLIENT_STATE State,
    ZP_STATUS Status,
    PVOID Context)
{
    UNREFERENCED_PARAMETER(Client);
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
}

NTSTATUS
StartClient(
    const BYTE* RootCertificateDer,
    ULONG RootCertificateDerLength,
    ZP_CLIENT_HANDLE* Client)
{
    static const ZP_MODULE_RECORD Modules[] = {
        { ZP_SYSTEM_MODULE_ID, ZP_SYSTEM_MODULE_VERSION },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION }
    };
    static const ZP_ENDPOINT Endpoint = {
        ZpTransportQuic,
        L"127.0.0.1",
        4433,
        L"server.example",
        NULL
    };
    ZP_CLIENT_CONFIG Config = { 0 };
    NTSTATUS Status;

    Config.Size = sizeof(Config);
    Config.Endpoints = &Endpoint;
    Config.EndpointCount = 1;
    Config.DeploymentRootCertificate = RootCertificateDer;
    Config.DeploymentRootCertificateLength = RootCertificateDerLength;
    Config.Modules = Modules;
    Config.ModuleCount = ARRAYSIZE(Modules);
    Config.StateCallback = ClientStateCallback;

    Status = ZpClient_Create(&Config, Client);
    if (NT_SUCCESS(Status))
    {
        Status = ZpClient_Start(*Client);
    }
    return Status;
}
```

`ZpClientStateReady` 表示被控端已可接收并执行 Server 请求；Client 不发起管理业务请求。停止顺序为 `ZpClient_Stop`，等待 `ZpClientStateStopped`，最后调用 `ZpClient_Close`。

异步操作、连接状态和 Channel 结束均使用自然对齐的 `ZP_STATUS`：`Type` 为 16 位，原始 `Code` 为 32 位。线上按字段编码为 6 字节，不传结构体填充。当前类型包括 NTSTATUS、Win32、Winsock、HRESULT、Security、QUIC 和 ProcessExit；SDK 不把来源码映射成另一套错误码。同步的本地参数、Handle 和提交错误仍直接返回 `NTSTATUS`。

## 异步 Handle 与 Buffer 规则

- 异步 API 的输出 Handle 只在函数返回成功时有效；同步拒绝不会触发完成回调。调用方不得读取失败调用留下的输出值。
- `ZpServer_*` 请求 API 成功返回时会交付 `ZP_REQUEST_HANDLE`；调用方不再需要取消或查询它时调用 `ZpRequest_Close` 释放自己的引用。
- 完成回调可能在发起 API 返回前同步发生；回调可直接 `ZpRequest_Close`，应用不得依赖“函数先返回、回调后发生”的时序。
- Request 回调恰好完成一次。超时在 Server 本地完成为 `STATUS_IO_TIMEOUT`，并尽力向 Client 发送 Cancel。
- 回调参数中的 View/Buffer 只在当前回调返回前有效；需要长期持有时由应用自行复制。
- 回调可能来自任意 SDK 工作线程或 Transport 回调线程；不同连接和对象可并发回调。SDK 不在对象锁内调用应用回调，应用仍应避免长期阻塞并自行同步共享状态。
- Client/Server 的 `Close` 不能在其回调栈内执行，此时会返回 `STATUS_DEVICE_BUSY`；Request、Channel 的调用方引用可以在对应回调中通过各自 `Close` 释放，释放后不得再次使用该 Handle。
- Channel 使用引用计数 Handle；本地取消与远端结束只产生一次终止回调，最迟在终止回调返回后调用 `ZpChannel_Close` 释放调用方引用。
- `ZpChannel_Send` 不做隐藏排队；额度不足时返回 `STATUS_RETRY`，应等待 Writable 回调后重试。

## 第一版资源边界

- Frame Body 最大 16 MiB，ChannelData 单帧最大 1 MiB；
- 每连接默认最多 64 个 Request、64 MiB Request Payload 和 16 个 Channel；
- File 与 Registry 排序快照有明确条目数和内存上限；
- 达到对应连接侧配额通常返回 `STATUS_QUOTA_EXCEEDED`，不因单个合法但超限的请求终止连接。

## License

MIT，见 [LICENSE](LICENSE)。
