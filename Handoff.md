# KNSoft.ZPigeon 项目交接

更新时间：2026-08-19

## 当前结论

项目角色已经纠正为：Client 运行在被控 Windows 主机并主动连接，Server 持有已认证 Connection 并发起全部管理操作。Client 不发起管理业务 Request；收到反向 Response/Event 之外的错误方向业务消息时按协议错误关闭连接。

当前 15 个协议模块均遵循 Server 控制、Client 本机执行的统一方向。Web 管理端已经覆盖系统、网络、存储、任务、硬件、软件和远程访问七类功能。旧的 Client 管理 API、Server 本机业务执行器、Read/Control 授权分类、派生 ClientId、Channel 奇偶 ID 规则及重复 Network 目录均已删除。EventLog 不建立实时订阅，只保留查询、频道管理、清除和基于增量查询的流式导出。

## 长期原则

- 代码保持极简：删除无调用路径、无当前产品价值或只服务旧设计的抽象、状态和兜底。
- 优先效率与安全；同步拒绝不创建异步对象，不把未完成操作伪报成功。
- 最低 Windows 10，直接使用 Win10 及以上能力，不承担旧系统兼容、协议降级或未发布版本兼容。
- 优先 KNSoft.NDK 的 NT 定义和 NT 系统调用；优先复用 KNSoft.MakeLifeEasier，不在本库复制通用函数。
- 可抽到 MLE 的能力必须先询问 Owner。`Mem_ReAlloc` 与 `IO_CreatePipe` 只在父级 MLE 本地实现并同步当前引用副本，不提交；`PS_CreateProcessEx` 仍只保留在 `MLE_Todo.md` 供 Owner 审核。
- 遵循 `.editorconfig`、SAL、原编码和 CRLF；工程文件只做必要的路径与编译项修改。

## 当前源码边界

```text
Source/
|-- KNSoft.ZPigeon.Protocol/Core/   Core Frame/Codec/Message
|-- Network/                        C/S 共用认证、Connection、QUIC 基础
|-- Modules/<Module>/             System/Process/Service/File/Terminal/EventLog/Registry/
|   |                             Window/Administration/Execution/Tunnel/Browser/Wmi/Audio
|   |-- Protocol.c                  模块 Codec
|   |-- Client.c                    Client 本机执行器
|   `-- Server.c                    Server 控制 API/响应解码
|-- KNSoft.ZPigeon.Client.SDK/
|   |-- Core/                       入站 Request、本机 Channel
|   `-- Transport/                  Client 生命周期、重连、QUIC
|-- KNSoft.ZPigeon.Server.SDK/
|   |-- Core/                       出站 Request、Channel
|   `-- Transport/                  Server 生命周期、监听、QUIC
|-- KNSoft.ZPigeon.Client/          Client SDK 启动 EXE 与分模块日志
|-- KNSoft.ZPigeon.Server.Native/   C# 可调用的 Server C DLL
|-- KNSoft.ZPigeon.Server.Managed/  可复用的 .NET Server SDK/Terminal 会话层
|-- KNSoft.ZPigeon.Web/             本地回环 WebSocket/UI 适配层
`-- SDK/                            共用 Handle 基础实现
```

Transport 不解析模块 Payload，不调用 Registry、SCM、进程、文件、ConPTY 或 Event Log API。模块不包含 `HQUIC`，也不直接驱动 Transport。

Managed SDK 封装与 Web 无关的远程操作和长生命周期会话；Web 层只负责 HTTP、WebSocket 和页面交互。文件、终端、隧道、图像及音频等连续数据通过 Channel 流式传输，不在 Web 层复制模块协议实现或完整缓存大型内容。

## 协议与对象规则

- Request 只由 Server 创建；Client 只执行并返回 Response。
- Channel 只由 Client 创建；Server 持有公共 Handle 并控制窗口、发送、取消和关闭。
- RequestId、ChannelId 在各自连接命名空间中从 1 单调递增，非零、永不复用，不做奇偶或方向预留。
- Client 以最高已见 RequestId 拒绝重复或倒序 Request；Server 以下一分配值识别已发送 Request。已结束 Request 的迟到 Response/Cancel 幂等忽略，未来 ID 仍是协议错误，不建立 tombstone 表。
- 已结束或被 Server 配额拒绝的 Channel 只以最高已见 ID 表示，不建立额外 tombstone 表；迟到 Window/Close 可忽略，未来 ID 和未知 Data 仍是协议错误。
- Server 在发送会创建 Channel 的 Request 前锁内预留本地名额，避免 Client 先创建文件句柄或 ConPTY 后再被 Server 配额拒绝。
- Server 正常停止时 Connection 生命周期可报告成功，但所有尚未完成的 Request/Channel 以 `STATUS_CONNECTION_DISCONNECTED` 完成，禁止伪成功。
- Response、状态回调和 ChannelClose 使用 `ZP_STATUS`：Type 为 16 位，原始 Code 为 32 位，线上固定编码为 6 字节且不传结构体填充；保留 NTSTATUS、Win32、Winsock、HRESULT、Security、QUIC 和 ProcessExit 错误域，不跨域映射；同步本地提交错误仍返回 `NTSTATUS`。
- 模块版本必须完全相同才参与协商；不保留旧 Decoder、自动降级或兼容分支。
- 模块记录仅包含 ID 和 Version；未被业务读取的 Capabilities 已删除。产品从不发送的 Disconnect 消息也已删除，连接关闭直接使用 QUIC 生命周期。

## 已验证内容

- Visual Studio 2026 下 x64 Debug 全 Solution 构建通过，最新 UnitTest 为 374/374；x86 配置已删除，ARM64 后续按需加入；
- Web、Managed、Native、QUIC 与 Client 本地闭环已经跑通，QUIC 启用 KeepAlive，并保留 ProcessExit、NTSTATUS、Win32、Winsock、HRESULT、Security、QUIC 和 WebSocket 等原始状态域；
- Terminal、File、Registry、Process、Service、EventLog、Window、WMI、Audio、Video、Tunnel、Browser、Execution 和 Administration 管理路径均已接入 Web；
- 网络共享、网络适配器、IPv4/IPv6 路由表及 TCP/UDP 端点已经完成真实只读联调；网卡启用/禁用只验证调用路径，未在开发机执行；
- 固件读取按 CPUID、SMBIOS 和 ACPI 分页签组织并按需获取；UEFI 变量和启动项写入能力只编码，不在开发机执行破坏性测试；
- 最新 Client SDK 静态分析未发现 NetworkStatus 新增实现的问题；仓库仍有其他既有静态分析告警需要后续独立审计。

## 后续开发入口

1. 继续以 Owner 的实际试用反馈为优先，修正功能完整性和交互一致性。
2. 审计模块内部重复分配、重复 Encode 模式和可复用 MLE 候选；发现通用抽象先向 Owner 提案，不直接修改 MLE。
3. 保持 QUIC、TLS/TCP、WSS 的 Transport 边界；EventLog 不恢复系统级实时订阅。

当前没有外部阻塞。发布前仍需完成当前代码的 x64 Release、干净环境和普通用户权限验证。
