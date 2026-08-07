# KNSoft.ZPigeon

KNSoft.ZPigeon 是面向 Windows 10 及以上系统的纯 C 远程管理 SDK。第一版使用 QUIC 与 TLS 1.3，提供持久化客户端身份、模块协商、异步 Request、流式 Channel、实时 Subscription，以及 System、Process、Service、File、Terminal、EventLog 和 Registry 模块。

项目目前输出三个静态库：

- `KNSoft.ZPigeon.Protocol`：Transport 无关的帧、消息和模块 Codec；
- `KNSoft.ZPigeon.Client.SDK`：客户端连接、重试、请求和 Channel/Subscription 生命周期；
- `KNSoft.ZPigeon.Server.SDK`：QUIC 监听、认证、授权及 Windows 原生管理操作。

完整协议与安全模型见 [Design.md](Design.md)，当前实现状态见 [Progress.md](Progress.md)。

## 构建与测试

需要 Visual Studio C++ 工具链、Windows SDK，以及项目 `packages.config` 中声明的 KNSoft.MakeLifeEasier、KNSoft.NDK 和 KNSoft.Quic 原生 NuGet 包。

在 Visual Studio Developer PowerShell 中执行：

```powershell
msbuild Source\KNSoft.ZPigeon.slnx /t:Restore
msbuild Source\KNSoft.ZPigeon.slnx /t:Rebuild /p:Configuration=Debug /p:Platform=x64 /m
Source\OutDir\x64\Debug\UnitTest.exe -Run
```

当前 x86/x64、Debug/Release 全矩阵均为 0 编译/链接警告，335/335 项断言通过。

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
    NTSTATUS Status,
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
    NTSTATUS Status,
    PVOID Context)
{
    UNREFERENCED_PARAMETER(Server);
    UNREFERENCED_PARAMETER(Connection);
    UNREFERENCED_PARAMETER(Phase);
    UNREFERENCED_PARAMETER(Status);
    UNREFERENCED_PARAMETER(Context);
}

NTSTATUS
StartServer(
    PCCERT_CONTEXT Certificate,
    ZP_SERVER_HANDLE* Server)
{
    static const ZP_MODULE_RECORD Modules[] = {
        { ZP_SYSTEM_MODULE_ID, ZP_SYSTEM_MODULE_VERSION, 0 },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION, 0 }
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
    if (NT_SUCCESS(Status))
    {
        Status = ZpServer_Start(*Server);
    }
    return Status;
}
```

未配置 `AuthorizeCallback` 时，Read 操作默认允许，Control 操作默认拒绝。生产程序应显式配置授权回调，依据认证后的 `ClientId`、访问级别、模块、操作及原始 Payload 收窄权限。

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
    NTSTATUS Status,
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
        { ZP_SYSTEM_MODULE_ID, ZP_SYSTEM_MODULE_VERSION, 0 },
        { ZP_FILE_MODULE_ID, ZP_FILE_MODULE_VERSION, 0 }
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

等待 `ZpClientStateReady` 后再发业务请求。停止顺序为 `ZpClient_Stop`，等待 `ZpClientStateStopped`，最后调用 `ZpClient_Close`。

## 异步 Handle 与 Buffer 规则

- `ZpClient_*` 请求 API 成功返回时会交付 `ZP_REQUEST_HANDLE`；调用方不再需要取消或查询它时调用 `ZpRequest_Close` 释放自己的引用。
- 完成回调可能在发起 API 返回前同步发生；回调可直接 `ZpRequest_Close`，应用不得依赖“函数先返回、回调后发生”的时序。
- Request 回调恰好完成一次。超时在 Client 本地完成为 `STATUS_IO_TIMEOUT`，并尽力向 Server 发送 Cancel。
- 回调参数中的 View/Buffer 只在当前回调返回前有效；需要长期持有时由应用自行复制。
- Channel 和 Subscription 也使用引用计数 Handle；本地取消与远端结束均只产生一次终止回调，随后分别调用 `ZpChannel_Close` 或 `ZpSubscription_Close` 释放调用方引用。
- `ZpChannel_Send` 不做隐藏排队；额度不足时返回 `STATUS_RETRY`，应等待 Writable 回调后重试。

## 第一版资源边界

- Frame Body 最大 16 MiB，ChannelData 单帧最大 1 MiB；
- 每连接默认最多 64 个 Request、64 MiB Request Payload、16 个 Channel 和 16 个 Subscription；
- File 与 Registry 排序快照有明确条目数和内存上限；
- 达到 Server 配额通常返回 `STATUS_QUOTA_EXCEEDED`，不因单个合法但超限的请求终止连接。

## License

MIT，见 [LICENSE](LICENSE)。
