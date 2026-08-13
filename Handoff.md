# KNSoft.ZPigeon 项目交接

更新时间：2026-08-14

## 当前结论

项目角色已经纠正为：Client 运行在被控 Windows 主机并主动连接，Server 持有已认证 Connection 并发起全部管理操作。Client 不发起管理业务 Request；收到反向 Response/Event 之外的错误方向业务消息时按协议错误关闭连接。

System、Process、Service、Registry、File、Terminal 和 EventLog 均已迁移为 Server 控制、Client 本机执行。旧的 Client 管理 API、Server 本机业务执行器、Read/Control 授权分类、派生 ClientId、Channel 奇偶 ID 规则及重复 Network 目录均已删除。EventLog 实时订阅和整个 Subscription 对象体系也已删除，只保留查询、频道启停和清除。

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
|-- Modules/<Module>/
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

Managed SDK 已把文件传输、文件/进程/服务查询及控制封装为与 Web 无关的可复用 API；Web 层只负责 HTTP、WebSocket 和页面交互。文件下载以 Channel 逐块写入响应，上传逐块写入远端 Channel，不在 Web 层复制模块协议实现。

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

- Visual Studio 2026 下 x64 Debug/Release 全 Solution Rebuild 均为零警告、零错误，直接产出三个静态库、Client EXE、Server Native DLL、Managed SDK 与 C# Web；x86 配置已删除，ARM64 后续按需加入；
- x64 Debug UnitTest 为 332/332、Release 为 331/331 通过，均包含真实 localhost QUIC 集成；Debug 比 Release 多一项仅验证 Debug 5 秒重连间隔的断言，两个配置的 ConsumerTest 均通过；
- Web/Managed/Native/QUIC/Client 本地回环已实际跑通，System.Info、EventLog、三种本机 Shell 探测、cmd/PowerShell 交互、多会话切换与主动关闭已验证；QUIC 已启用 KeepAlive，Web 保留并显示 ProcessExit、NTSTATUS、Win32、Winsock、HRESULT、Security、QUIC 或 WebSocket 分类及原始码；
- Web 文件、进程和服务页已实际跑通：文件分页、属性、SHA-256、流式上传/下载、重命名和删除；进程 CPU/内存/线程/句柄/会话实时列表、映像路径/命令行详情与结束进程；服务枚举、详情、启动/停止入口和 Win32 错误域透传。进程刷新只在该页前台可见且窗口有焦点时运行，离开或隐藏后停止；详情和结束操作以 PID 与创建时间核对进程身份，避免 PID 复用竞态；
- Registry 默认值空名称分页已修正；Terminal 已改为两个 NT 异步单向管道和专用输出线程，已验证大于 100 KiB 输出、退出码 7、Resize、Cancel 和 ConPTY 最终输出排空。

## 后续开发入口

1. 由 Owner 先试用 Web/Client 本地闭环，优先修正实际体验问题。
2. 继续审计模块内部重复分配、重复 Encode 两遍模式和可复用 MLE 候选；发现通用抽象先向 Owner 提案，不直接修改 MLE。
3. 不扩展当前功能；QUIC/TLS-TCP/WSS 边界继续保留，EventLog 不恢复实时订阅。

当前没有外部阻塞；工作树包含本轮架构纠偏的完整在途改动，不得丢弃或回退。
