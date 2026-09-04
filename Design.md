# KNSoft.ZPigeon 设计

状态：总体设计基线  
更新时间：2026-09-01

本文档是 KNSoft.ZPigeon SDK 与应用层的设计基线，记录系统边界、架构、协议、身份与安全模型以及待细化的技术问题。待办见 `TODO.md`，发布门禁见 `Release.md`。

## 1. 目标与边界

KNSoft.ZPigeon 是面向合法 Windows 远程管理场景的 C/S 软件。Client SDK 和 Server SDK 支撑系统、网络、存储、任务、硬件、软件和远程访问七类管理能力。

设计基线：

- SDK 使用纯 C，公开接口保持稳定的 C ABI；
- 最低支持 Windows 10，当前只支持 x64；ARM64 后续按需加入；
- 稳定性、安全性和效率优先；
- C 主动连接 S，S 对 C 拥有完整管理能力；
- C/S 之间只建立加密连接，不允许自动降级到明文协议。

明确不做：

- 隐蔽、注入、绕过或监控安全软件等非法能力；
- Controller、ManagedNode、Relay 等额外业务身份；
- 业务角色和操作级权限系统；
- 明文 UDP 管理协议；
- Windows 10 以前系统的兼容；
- 第一版的轮询模式、S 集群和超大规模长连接专项设计。

## 2. 总体架构

第一版采用直接 C/S 和持久连接模型：

```text
Client executors                       Server control APIs
       |                                      |
       +---------- Protocol Dispatcher -------+
                              |
                         Connection
                              |
             QUIC | TLS/TCP | DTLS/UDP Transport
```

网络核心只保留三层：

```text
Transport -> Connection -> Protocol Dispatcher
```

- `Transport` 提供连接、监听、发送、关闭及对应异步通知，不理解业务消息；
- `Connection` 管理单个安全连接的生命周期、接收 Buffer、Frame 切分和握手状态；
- `Protocol Dispatcher` 按消息类型和模块路由已验证的 Payload。

第一版不建立复杂 RPC 框架、通用权限框架或完整虚拟流系统。需要的请求关联、事件和通道能力直接由最小协议字段表达。

业务控制方向固定为 S -> C：S 针对已认证 Connection 发起管理 Request，C 在本机执行并返回 Response 或建立 Channel。C 不向 S 发起业务 Request；方向错误的完整业务消息按协议违规拒绝并计入接收预算，持续违规才关闭连接。

## 3. 项目与代码组织

解决方案的生产代码包含三个原生静态库、两个运行/互操作项目和五个托管项目：

```text
KNSoft.ZPigeon.Protocol
KNSoft.ZPigeon.Client.SDK
KNSoft.ZPigeon.Server.SDK
KNSoft.ZPigeon.Client
KNSoft.ZPigeon.Server.Native
KNSoft.ZPigeon.Server.Managed
KNSoft.ZPigeon.Application
KNSoft.ZPigeon.Tools
KNSoft.ZPigeon.Agent
KNSoft.ZPigeon.Web
```

依赖关系：

```text
Client SDK ----> Protocol
Server SDK ----> Protocol
Client EXE ----> Client SDK + Protocol
Server Native DLL ----> Server SDK + Protocol
Managed SDK ----> Server Native DLL
Application ----> Managed SDK
Tools ----> Application
Agent ----> Tools
Web ----> Agent + Tools + Application + Managed SDK
```

- Protocol 项目由 C/S 共用，且只使用纯 C；
- Client SDK 和 Server SDK 链接 Protocol，不重复编译同一份 Protocol 实现；
- Protocol 不负责连接、线程、密钥存储或具体 Transport。

源码按功能聚合，而不是按三端分别复制目录：

```text
Source/
|-- KNSoft.ZPigeon.Protocol/
|   `-- Core/                       Frame/Codec/Message
|-- Network/
|   |-- Authentication.inl
|   |-- Config.inl
|   |-- Connection.c/.h
|   |-- Transport.h
|   `-- Quic.inl
|-- Modules/
|   |-- System/
|   |   |-- Protocol.c
|   |   |-- Client.c
|   |   `-- Server.c
|   |-- Process/ Service/ File/ Terminal/
|   |-- EventLog/ Registry/ Window/ Administration/
|   `-- Execution/ Tunnel/ Browser/ Wmi/ Audio/
|-- KNSoft.ZPigeon.Client.SDK/
|   |-- Core/
|   `-- Transport/
|-- KNSoft.ZPigeon.Server.SDK/
|   |-- Core/
|   `-- Transport/
|-- KNSoft.ZPigeon.Client/
|-- KNSoft.ZPigeon.Server.Native/
|-- KNSoft.ZPigeon.Server.Managed/
|-- KNSoft.ZPigeon.Application/
|-- KNSoft.ZPigeon.Tools/
|-- KNSoft.ZPigeon.Agent/
|-- KNSoft.ZPigeon.Web/
`-- SDK/
```

各项目通过工程文件选择属于自己的源文件：Protocol 编译模块目录内的 `Protocol.c`，Client SDK 编译 `Client.c`、Client Core 和 Client Transport，Server SDK 编译 `Server.c`、Server Core 和 Server Transport。`Network` 只保留双方共用的网络基础，不出现业务模块；SDK 内不再建立第二套 `Network` 目录。

模块通过最小静态描述符表接入 Core Dispatcher。Dispatcher 只完成模块和操作查找、通用状态校验及生命周期转交，不包含模块 Payload 解码或 Windows 业务 API。Module 不得包含 `HQUIC` 或直接调用 Transport；Transport 不得按 ModuleId 分发或执行业务。

### 3.1 Server 应用层与 AI 入口

Server 上层保持单向依赖：

```text
MCP -----------------> Tools -> Application -> Server.Managed
Web 对话 -> Agent ---> Tools -> Application
REST / UI 适配器 -----------> Application | Server.Managed
```

- `Server.Managed` 是可复用的 .NET SDK，忠实封装 Native 能力、原始状态和流式对象；
- `Application` 组合适合自动化的管理用例，显式接收 `ClientId` 和取消令牌，并统一参数与结果边界；它不依赖 ASP.NET、MCP SDK、模型 Provider 或 UI；
- `Tools` 以 `AIFunction` 定义唯一的模型工具目录、参数 Schema、只读/破坏性语义和敏感性；同一函数分别用于外部 MCP 与内置智能体；
- `Agent` 负责模型、Agent 与会话持久化、模型协议、上下文和函数调用循环，不经 HTTP 回调本机 REST，也不复制工具执行逻辑；
- `Web` 是组合根，显式映射 REST，托管 MCP，并提供模型配置和对话 UI。REST 在语义完全相同时复用 Application；UI 专用分页、二进制传输、WebSocket 和长生命周期会话等适配器可直接使用 Managed SDK，不强行改写成模型工具。

REST、MCP 和内置智能体是三种入口，不是三套业务实现。REST 路由按 UI 和传输需求显式映射；`ToolAudience.ExternalMcp` 与 `ToolAudience.BuiltInAgent` 只决定一个工具面向哪类智能体。模型 Provider 与暴露范围正交，不能以 Provider 类型决定工具权限或目标。

外部 MCP 使用无状态 Streamable HTTP，直接支持 2026-07-28 无握手请求。除 `list_clients` 外，工具都要求显式传入字符串形式的瞬时 `ClientId`，不得依赖服务器端“当前 Client”。内置智能体在创建工具 Schema 时绑定当前页面 Client，并从 Schema 中移除 `clientId`；模型无法选择其他目标。需要由调用方回传的 64 位身份、游标和进程创建时间以十进制字符串跨 JSON 边界传递，避免 JavaScript 数字精度损失。

新增 SDK API 或枚举值不自动成为 REST 或 AI 能力。先按产品语义决定是否增加 Application 用例；需要 AI 调用时只在 Tools 中增加一次定义，MCP 与内置智能体自动同步；需要 Web UI 时再显式增加 REST、流或页面适配。Administration 等聚合入口使用显式操作白名单、固定控制操作与动作组合并限制结果数；需要 Flags、二进制或专用结构载荷的操作不进入通用字符串工具。这样不会把底层面过度暴露，也不会维护两套 MCP/Agent 映射。

模型目录使用仓库内 `Source/3rdParty/models.dev/api.json` 的静态快照，保留上游 Provider ID，并在进程启动时解析一次。`Update.cmd` 只供维护者手动覆盖快照；运行时不联网更新、不引入 npm 包或第二套缓存。模型配置显式保存 Provider、接口协议、Base URL、认证、模型 ID、上下文窗口、最大输出、Reasoning、超时和高级 JSON；高级 JSON 原样合并，但不能覆盖消息、工具等结构字段。

模型、Agent、会话及按序事件保存在 Server 的 SQLite 数据库。Agent 只包含名称、模型、System Prompt、工具白名单、`AGENTS.md`、`TOOLS.md`、`MEMORY.md` 和自定义 Markdown。会话绑定 Client 公钥指纹，保存用户、助手、Tool Call、Tool Result、压缩摘要、错误及归一化和原始 Usage；支持历史搜索、分支和 JSON 导出。每个会话最多一个运行循环；新消息默认排队，Steer 取消当前请求并优先执行，Stop 取消当前请求和未运行队列。上下文达到阈值时自动压缩，也可手动触发；执行顺序独立于消息提交顺序，终止的 Tool Call 会写入错误结果，保证后续协议历史完整。

## 4. Deployment、S 与 C 身份

### 4.1 Deployment

- 一个分组生成一份 C，该 C 可安装到多台 Windows 系统；
- 每个分组拥有独立的根密钥对；
- `DeploymentKeyId = SHA-256(DeploymentPublicKey)`，不再维护另一个随机 DeploymentId；
- C 内置本分组的根公钥以及按优先级排列的 Endpoint；
- 分组根私钥离线保存，只用于签署可轮换的 S 在线公钥或证书；
- 公钥不是秘密，不依赖隐藏或混淆提供安全性。

### 4.2 S 身份

- 每个分组使用专属 SNI；
- Endpoint 分别记录实际连接地址和 `ServerName`；
- S 根据 SNI 选择该分组的在线证书；
- C 使用内置的分组根公钥验证 S 在线证书，确认连接到本分组授权的 S；
- 在线私钥泄漏时轮换在线密钥和证书，不需要重新生成、部署 C。

具体密钥算法、证书格式、签名数据和轮换格式在第一版 Protocol/API 规格中确定。

### 4.3 C 实例身份

- 每台安装实例首次运行时通过 Windows CNG 生成独立、持久化的客户端密钥；
- Client SDK 不绑定进程令牌类型：交互用户、非管理员、管理员和 SYSTEM 服务都能启动 Client。调用方必须显式选择用户或计算机密钥作用域；当前本机原型使用用户作用域，未来服务形态使用计算机作用域，不在两者之间自动回退或复制私钥；
- 线上 SEC1 公钥本身就是稳定实例标识，协议和 SDK 不再维护派生 `ClientId` 字段；
- Native/Managed/Web 为每条当前连接分配只在本次 Server 进程内有效的递增 `ClientId`，仅用于 API 路由和并发隔离；它不是协议字段，也不替代公钥身份；
- CNG 密钥被删除、重置或因系统重装丢失后，该实例表现为新的公钥身份；
- S 不自动把新的公钥身份与历史记录合并；
- 未知公钥完成客户端密钥签名握手后即可由 S 操作。

当前模型只证明“后续连接仍持有同一客户端私钥”，不额外证明该实例经过入组审批。如果以后需要受控入组，应另行增加一次性 Enrollment 凭据，不能把客户端自签名误当作部署授权。

## 5. Transport 与 Endpoint

支持三种 Transport，默认配置使用 QUIC：

1. QUIC；
2. TLS/TCP；
3. DTLS/UDP。

约束：

- QUIC 使用 KNSoft.Quic 提供的 MsQuic 基础；
- TLS/TCP 使用 Windows TLS 能力；
- UDP 使用 Windows DTLS 能力，并在其上提供可靠、有序的字节传输语义；
- 不支持明文 TCP、UDP，也不从安全协议自动降级到明文协议；
- QUIC 第一版只需使用一条双向 Stream，不立即暴露 QUIC 多流能力。

Client 和 Server 只初始化配置中实际出现的 Transport。DTLS/UDP 使用 Winsock 事件驱动接收，定时任务只等待最近的握手、重传、KeepAlive 或空闲超时期限，发送入队会主动唤醒工作线程，不保留固定周期轮询。

QUIC 第一版使用 ALPN `knsoft-zpigeon/1`。Client 单独解析 Endpoint 的 `Host` 并设置 MsQuic 远端地址，再把 `ServerName` 作为 SNI 传入，因此连接目标与身份名称不会被混为同一字段。Client 为配置中的 Deployment 根 DER 建立内存证书库和 `hExclusiveRoot` 专用链引擎，通过 MsQuic 延迟证书验证事件执行 Windows SSL 链策略与 `ServerName` 校验，验证完成前返回 `QUIC_STATUS_PENDING`；不得设置 `NO_CERTIFICATE_VALIDATION`，也不得回退系统公共根。Schannel 路径不设置 `QUIC_CREDENTIAL_FLAG_INPROC_PEER_CERTIFICATE`，从而保证证书事件提供原生 `PCCERT_CONTEXT`；若将来改用该标志，必须同时按 MsQuic 的序列化或 portable certificate 契约重写验证入口，不得把 blob 强制转换为证书 Context。

Server 为每个 Deployment 创建独立 MsQuic Configuration 并装载其 `PCCERT_CONTEXT`，新连接按 SNI 不区分大小写精确选择 Configuration；缺失或未知 SNI 直接拒绝。Server 只允许对端创建一条双向 Stream，Client 在 TLS 连接完成后创建该 Stream；额外 Stream 或单向 Stream 是协议错误。Listener、Connection 和 Stream 均遵循 MsQuic 的异步停止/`SHUTDOWN_COMPLETE` 后关闭规则，Registration 的同步关闭不得发生在 MsQuic 回调栈内。

Endpoint 至少需要表达：

- Transport 类型；
- 实际主机或 IP；
- 端口；
- 用于 TLS/SNI 验证的 `ServerName`；
- 优先级或列表顺序。

C 按配置顺序尝试 Endpoint；连接超时、失败后的轮次推进、重连退避和已连接后的 Transport 切换规则见 8.1 节。

## 6. 连接与握手

逻辑流程：

```text
选择 Endpoint
    -> 建立 QUIC/TLS/DTLS 安全连接
    -> 使用 Deployment 根公钥验证 S 在线证书
    -> 交换 Hello 与 Client 版本
    -> S 发出客户端 Challenge
    -> C 使用实例私钥签名
    -> S 验证签名和客户端公钥
    -> 登记或加载客户端记录
    -> Ready
```

握手只保留实际需要的信息：

- C 的 Client 版本；
- 客户端公钥；
- Challenge、签名以及必要的防重放数据。

Deployment 已由当前连接的 SNI 和证书上下文确定，握手中不重复传输 DeploymentKeyId。

C 不内置、不依赖 S 的版本。S 保存 C 的 Client 版本；低于最低可接受版本时拒绝连接，不保留未要求的旧版 Decoder、兼容路径或降级逻辑。

QUIC、TLS/TCP 和 DTLS/UDP 均使用 TLS 派生的对称会话密钥保护业务数据。分组公私钥不用于逐包加解密，也不在 TLS 外增加应用层二次加密。Client QUIC 使用 20 秒 KeepAlive，避免空闲但仍有效的控制连接被默认空闲超时关闭。

### 6.1 Connection 状态机与接收缓存

Connection 通用实现位于共享的 `Source/Network`，由 Client SDK 和 Server SDK 分别编译，不放入 Protocol 静态库。Protocol 仍只负责无状态的 Frame 和 Payload 编解码。

握手状态按本端下一步动作显式推进：

```text
Client: SendHello -> WaitChallenge -> SendAuthenticate -> WaitReady -> Ready
                         \-> ServerReject -> Closed
Server: WaitHello -> SendChallenge -> WaitAuthenticate -> SendReady -> Ready
                  \-> SendReject -> Closed
```

- Transport 成功接受一条本端握手消息后，Connection 通过发送通知推进到下一状态；
- 收到完整且顺序正确的握手 Frame 后，Connection 先推进状态，再调用消息回调，使回调可以立即生成下一条握手消息；
- `Ready` 前不允许业务消息，`Ready` 后不允许重复握手消息；越序、方向错误或重复消息以 `STATUS_PROTOCOL_UNREACHABLE` 关闭连接；
- `Ready` 前任何解码、状态或消息回调失败都关闭连接；身份验证、Transport/TLS 错误和内部不变量失败同样是 fatal error；
- 长度前缀非法时无法可靠确定下一帧边界，必须关闭连接；已经完整消费且仍可对齐下一帧的 `Ready` 状态 Frame 则把单次解码、协议或业务分派失败限制在该 Frame；
- 内存、资源和配额不足时丢弃当前 Frame 且不计违规；其他格式或状态违规在 10 秒窗口内累计 8 次才关闭连接，使孤立坏包不影响健康连接，同时限制持续恶意输入；
- 同一连接的接收和发送状态通知由 SDK 串行调用，Connection 本身不增加热路径锁。

接收路径针对 Transport 的任意分片和合并交付：连续完整 Frame 直接解码，不复制到中间 Buffer；不完整的 4 字节长度前缀保存在 Connection 内；分片 Frame 的堆 Buffer 从 4 KiB 起按已实际收到的数据增长，最大不超过 Frame 声明长度，不能仅凭未受信任的长度前缀立即分配 16 MiB。一次接收包含多个 Frame 时逐项分派，回调中的 View 只在当前回调返回前有效。

## 7. Protocol 与 Frame

Protocol 负责：

- 线上数据结构定义；
- Frame 编码、解码和边界校验；
- 固定版本 Payload Codec；
- 消息和 Client 版本处理；
- 架构无关的整数、字符串、数组及 Buffer 表达。

Protocol 不允许直接发送本机 C 结构体。线上格式使用：

- 小端整数；
- 按 Client 版本固定的字段顺序；
- UTF-16LE 字符串；
- 显式长度或数量；
- 不依赖指针宽度、编译器对齐或结构体 Padding。

编码接口支持调用方提供 Buffer，并支持先计算所需长度。解码尽量返回指向接收 Buffer 的只读 View，避免无必要的复制和堆分配；View 的有效期不得超过所属接收 Buffer。

Native/Managed/Web 桥接同样保持数据与控制分离：文件、剪贴板图像、固件表、证书 DER、进程内存和注册表预览以原始字节传递；JSON 只承载小型请求参数和记录元数据。不得为了复用文本接口把二进制先编码成 Base64、UTF-16 或 JSON，再在下一层恢复为字节。Base64 只保留在 PEM 等格式本身要求编码，或远端命令行协议明确需要安全文本封装的边界。

### 7.1 Frame

第一版 Frame 使用以下公共前缀：

```text
UINT32 BodyLength
BYTE   MessageType
BYTE[] Type-specific body
```

- 所有整数使用小端序；
- `BodyLength` 是其后全部字节数，包含 `MessageType`，不包含自身的 4 字节；
- `BodyLength` 最小为 1，最大为 16 MiB；完整 Frame 长度为 `4 + BodyLength`；
- 类型专用字段之后的剩余字节即该消息的模块 Payload，不再重复记录 Payload 长度；
- Frame 连续排列，无对齐和 Padding；Transport 可把一个 Frame 拆成多次接收，也可一次交付多个 Frame；
- 解码器在收到完整 4 字节长度前不得读取 `BodyLength`，在收到完整 Frame 前不得分派消息。

公共前缀不传输：

- Magic；
- HeaderSize；
- S 版本；
- 每帧协议版本；
- 无用途的保留字段；
- 对齐 Padding。

第一版 `MessageType` 编号固定为：

| 值 | 名称 | 方向 | 类型专用 Body |
|---:|---|---|---|
| `0x01` | `ClientHello` | C -> S | `BYTE ClientVersion`、65 字节客户端公钥 |
| `0x02` | `ServerChallenge` | S -> C | 32 字节随机 Challenge |
| `0x03` | `ClientAuthenticate` | C -> S | 64 字节 ECDSA P-256 `r || s` 签名 |
| `0x04` | `Ready` | S -> C | 空 |
| `0x05` | `ServerReject` | S -> C | `BYTE Reason`：1 表示 Client 版本过旧 |
| `0x10` | `Request` | S -> C | `UINT32 RequestId`、`BYTE ModuleId`、`BYTE OperationId`、`UINT32 TimeoutMilliseconds`、Payload |
| `0x11` | `Response` | C -> S | `UINT32 RequestId`、`BYTE StatusType`、`StatusType != None` 时的 `UINT32 StatusCode`、Payload |
| `0x12` | `Cancel` | S -> C | `UINT32 RequestId` |
| `0x13` | `ChannelData` | 双向 | `UINT32 ChannelId`、非空数据 |
| `0x14` | `ChannelClose` | 双向 | `UINT32 ChannelId`、`BYTE StatusType`、`StatusType != None` 时的 `UINT32 StatusCode` |
| `0x15` | `ChannelWindow` | 双向 | `UINT32 ChannelId`、非零 `UINT32 CreditBytes` |
| `0x16` | `ConnectionPolicy` | S -> C | 单字节连接性能策略 |

其他值在 Client Version 1 中非法。消息类型的最小 Body 长度由 Protocol 解码器校验。`ChannelData` 单帧数据最大为 1 MiB，单次 `ChannelWindow` Credit 最大为 16 MiB，避免大块传输长期占用连接发送队列；其他消息仍受 16 MiB Frame 上限约束。

`ClientVersion` 是 Client EXE 单调递增的线上兼容版本。Client 的模块随 EXE 同步编译和发布，因此不发送模块清单、能力位图或模块版本。Server 保存每条连接的 Client 版本；当前 Client 版本和最低可接受版本均为 1。低于最低版本时，Server 在认证前以 `ServerReject` Reason 1 拒绝，Client 以上报 `STATUS_REVISION_MISMATCH` 后断开；高于当前版本的 Client 不在握手层被拒绝。未来只有 Client 已无法由任何模块正确处理时才提高最低版本；存在格式差异的模块按连接的 Client 版本选择 Codec。任一不兼容的线上变化必须提升 Client 版本。

初始业务消息语义限制为：

- `Request`：发起一次操作并携带请求关联信息；
- `Response`：返回对应请求的类型化原始状态和结果；
- `ChannelData`：传递文件或终端等长生命周期数据；
- `ChannelWindow`：由接收方增加指定 Channel 的可发送字节额度；
- `ConnectionPolicy`：由 Server 以单字节下发连接性能档位。

`RequestId` 和 `ChannelId` 均为连接内非零 `UINT32`，在各自命名空间从 1 单调递增且不复用，不按奇偶或方向预留取值；耗尽后拒绝继续分配。第一版 Request 只由 Server 创建；Client 发来的业务 Request 是协议错误。Client 只接受严格大于本连接最高已见值的 RequestId，允许发送失败造成的序号空洞，但拒绝重复或倒序 Request。已结束 Request 的迟到 Cancel 和 Response 幂等忽略，未来未知 ID 仍是协议错误；只保存最高值或下一分配值，不建立 tombstone 表。第一版业务 Channel 只由 Client 创建。`ModuleId` 和 `OperationId` 为非零 `BYTE`。

`StatusType` 固定占 1 字节；`None = 0` 表示没有来源状态码且不传 `StatusCode`，其他类型紧跟 32 位 `StatusCode`，因此线上状态占 1 或 5 字节，不传本机结构体填充或 64 位扩展码。其余 Type 定义为 `NTSTATUS = 1`、`Win32 = 2`、`Winsock = 3`、`HRESULT = 4`、`Security = 5`、`QUIC = 6`、`ProcessExit = 7`、`ConfigurationManager = 8`、`SQLite = 9`；Code 保留来源 API 返回的原始 32 位 bit pattern，不做跨错误域映射。其他错误域的 Code 0 统一编码为 None；ProcessExit 必须保留类型且允许退出码 0。NTSTATUS、HRESULT、Security 和 QUIC 按各自有符号成功语义判断，Win32、Winsock、ConfigurationManager 和 SQLite 仅 Code 0 成功，ProcessExit 表示会话正常收尾而不是把退出码解释成错误。

`TimeoutMilliseconds` 是接收方从完整收到 Request 起计算的处理预算；0 表示协议层不额外施加超时。发送方 SDK 仍维护本地 Deadline：Deadline 到期后在本地以 `STATUS_IO_TIMEOUT` 完成操作，尽力发送 `Cancel`，并忽略迟到的 Response。显式取消在本地以 `STATUS_CANCELLED` 完成，`Cancel` 不要求单独响应。

Channel 使用接收方授信的字节窗口提供背压：新 Channel 建立后发送额度为 0，接收方发送 `ChannelWindow` 后发送方才能发送不超过累计剩余额度的 `ChannelData`；每次授信非零且不超过 16 MiB，剩余额度不得溢出 64 位计数。SDK 把 Data 回调返回视为对应 Buffer 已消费，并自动补回等量窗口。任一方发送一次 `ChannelClose` 即终止 Channel，Status 为终止结果且不回送第二个 Close。由于 ChannelId 单调且不复用，本地关闭或拒绝建立后收到不大于最高已见 ID 的迟到 Window 或 Close 时幂等忽略；未来 ID、未知 Data 或额度违规仍视为协议违规。

### 7.2 Payload 压缩

Response 和明确允许的 ChannelData 在原始 Body 不小于 4 KiB 时，可以使用 Windows XPRESS 进行逐 Frame 无状态压缩。压缩发生在 Transport 加密之前，由公共 Connection 实现，QUIC、TLS/TCP 和 DTLS/UDP 不维护各自的压缩路径。

- `MessageType` 的最高位 `0x80` 表示 Body 已压缩，其余 7 位仍表示原始消息类型；压缩位只允许用于 Response 和 ChannelData；
- 压缩 Body 编码为 `UINT32 UncompressedBodyLength` 后跟 XPRESS 数据，不重复记录压缩长度；
- 算法固定为 Windows XPRESS，不传算法字段，也不协商未实现的格式；低速且非低延迟的 Bulk 数据可使用 Maximum Engine，其余使用 Standard Engine；
- 管理端以速度、延迟各 5 档形成连接策略。C 位域结构和线上格式均为 1 字节：速度占低 3 bit，延迟占随后 3 bit，最高 2 bit 保留且必须为 0；
- 压缩所需的最低节省比例同时取决于速度和延迟档位：低速或高延迟连接更积极，快速或低延迟连接只压缩收益明显的数据；连同 4 字节原始长度不满足门槛时发送原始 Body；
- 大数据先压缩 1 KiB 样本，样本无收益时不扫描完整 Body；
- 每个 Frame 独立压缩，不跨消息共享字典；握手、认证、控制消息、媒体流、串口和 Tunnel 数据不压缩；
- Response 以及 File、PortableDevice 和 Terminal Channel 允许自适应压缩；Request 保持原始格式，文件类型不依赖扩展名判断；
- Frame 长度限制、请求 Payload 预算、Channel Credit、文件长度和业务进度全部按解压后的逻辑长度计算；
- 解压前校验声明长度，解压结果必须精确等于声明长度，并再次执行原消息 Body 校验；损坏或越界数据拒绝当前 Frame，并按 6.1 节的违规预算处理；
- TLS/QUIC/DTLS 已提供完整性保护，不增加 CRC，也不启用 TLS 连接级压缩。

管理端不主动测速。有效速度来自自然大流量的近期收发吞吐，响应延迟复用实际请求统计；没有新样本时档位和网络质量保持不变。自动策略对变差样本快速响应，对改善样本采用迟滞并逐档恢复，避免短时波动造成压缩策略频繁切换。

本机实际数据测试中，进程列表约由 51 KiB 降至 17 KiB，服务列表约由 135 KiB 降至 56 KiB；真实 EXE 和 MJS 样本约降至原始大小的 38% 和 42%，NuGet 包样本无收益并保持原始传输。

### 7.3 固定 Codec

Client Version 1 使用以下固定 Codec：

- `BYTE`、`UINT16`、`UINT32`、`UINT64` 和 `INT32` 分别占 1、2、4、8 和 4 字节；
- `BOOLEAN` 占 1 字节，只允许 0 和 1；
- `GUID` 固定占 16 字节，依次编码小端 `Data1`、`Data2`、`Data3` 和 8 字节 `Data4`；
- Payload 结构已确定长度的字段不重复编码长度；
- 字节串编码为 `UINT32 ByteLength` 后跟原始字节；
- UTF-16LE 字符串编码为 `UINT32 CodeUnitCount` 后跟对应数量的 16 位代码单元，不包含结尾 NUL；
- 无专用上限的数组编码为 `UINT32 ElementCount` 后跟逐项固定编码；具有模块上限的数组使用能覆盖该上限的最小固定宽度；
- 可选值先编码一个 `BOOLEAN Present`，为 1 时紧跟该值；
- 需要边界隔离的嵌套对象编码为 `UINT32 ByteLength` 后跟其内部固定编码；
- 值占据消息剩余部分且不需要内部边界时，直接使用外层 Body 长度，不重复发送长度；
- 不发送本机指针、`SIZE_T`、`HANDLE`、C 结构体 Padding 或依赖编译器布局的数据。

通用 Codec 的单个字节串、字符串或数组计数上限为 `0x00100000`；模块可制定更小上限。长度乘法和游标加法必须在访问 Buffer 前检查溢出。解码成功得到的 View 指向原始接收 Buffer，地址可能未对齐，其生命周期止于接收 Buffer 被释放或复用。

### 7.4 版本演进

- 最外层 Frame 格式保持稳定；
- C 报告唯一的 Client 版本；
- 模块 Payload 使用该 Client 版本确定的固定 Codec；
- 结构变化通过 Client 版本演进；
- 当前代码只实现当前发布所需版本，不背负未发布旧版本兼容；
- Client 版本低于最低可接受版本时明确拒绝连接，不自动降级或猜测 Payload 格式。

## 8. SDK API 与执行模型

已确定的 API 原则：

- 纯 C ABI；
- 使用项目既有 SAL 注解约定；
- 网络和远程操作以异步接口为核心；
- 同步本地调用失败返回 `NTSTATUS`；异步、Transport 和远程结果使用保留来源类型及 32 位原始码的 `ZP_STATUS`；
- 不采用一连接一线程；
- MsQuic 使用回调模型，其他 Windows Transport 使用适合的异步 I/O 模型；
- 热路径避免无依据的堆分配、内存复制、编码转换、锁和间接调用；
- 回调中不执行可能长期阻塞网络推进的业务操作。

第一版公开对象采用不透明指针 Handle：`ZP_CLIENT_HANDLE`、`ZP_SERVER_HANDLE`、`ZP_CONNECTION_HANDLE`、`ZP_REQUEST_HANDLE` 和 `ZP_CHANNEL_HANDLE`。对象由创建它的 SDK 分配，调用方只能通过对应 API 操作。

生命周期契约：

- `Create` 成功后返回初始停止状态对象；`Start` 启动异步工作；`Stop` 可重复调用并异步终止连接；
- `Close` 只接受已停止且不存在未完成回调的对象，否则返回 `STATUS_DEVICE_BUSY`；不隐式阻塞等待；
- 回调可能来自任意 SDK 工作线程，同一连接的状态与消息回调保持顺序，但不同连接可并发；
- SDK 在调用回调期间持有必要的内部引用；Client/Server `Close` 在其回调栈内返回 `STATUS_DEVICE_BUSY`，Request/Channel 的调用方引用则可在对应回调内通过各自 `Close` 释放；
- 配置和 Endpoint 字符串在 `Create` 返回前由 SDK 复制，调用方随后可释放源数据；
- 接收 Payload/View 只在当前回调返回前有效；需要长期保存时由调用方复制；
- 异步发送 Buffer 由调用方保持到完成回调，SDK 不修改其内容；同步拒绝发送时不会触发完成回调；
- 每个异步操作恰好产生一次终止完成；连接断开时未完成操作以连接终止状态完成。

取消与 Deadline：

- `ZpRequest_Cancel` 可从回调之外的任意线程调用；取消只保证本地完成，不保证远端操作能够撤销；
- Deadline 使用单调时钟在本地计算，不依赖 C/S 墙上时钟同步；
- 完成、取消、Deadline 和断开竞争时，以第一个原子确定的终止原因完成一次，其余事件只做清理；
- 回调不得长期阻塞；需要阻塞的业务工作由上层投递到自己的执行环境。

Protocol 第一阶段公开 `ZpFrame_*` 与 `ZpCodec_*` 纯函数；它们不分配内存、不持有全局状态。编码支持 `Buffer == NULL` 的长度计算模式，实际写入模式遇到容量不足返回 `STATUS_BUFFER_TOO_SMALL`。解码遇到不完整 Frame 返回 `STATUS_MORE_PROCESSING_REQUIRED`，遇到非法长度、字段或枚举返回 `STATUS_DATA_ERROR`。Client 版本由 Server 握手层判断并通过 `ServerReject` 返回；除长度计算契约明确要求的输出外，失败时不初始化输出参数。

### 8.1 Endpoint 与重连默认值

Endpoint 记录由 `Transport`、`Host`、`Port` 和 `ServerName` 构成。`Host` 是实际连接目标；`ServerName` 必须为非空 DNS 名称，用于 SNI 和证书名称验证，即使 `Host` 是 IP 也不省略。

第一版默认策略：

- 单个 Endpoint 的连接建立超时为 10 秒；
- 按配置顺序尝试全部 Endpoint，一轮内不重复；
- 一轮全部失败后等待 1 秒，之后指数退避为 2、4、8、16、32、60 秒，上限保持 60 秒；
- 每次等待加入正负 20% 随机抖动；连接连续稳定 60 秒后重置退避；
- 已建立连接异常断开后从列表第一项重新开始；不在存活连接之间主动迁移；
- 没有 Endpoint 时 `Start` 返回 `STATUS_INVALID_PARAMETER`；所有失败通过状态回调上报，不静默切换到未配置或不安全的 Transport。

### 8.2 第一版公开对象与配置

公共 `SDK.h` 定义五种互不兼容的不透明 Handle：`ZP_CLIENT_HANDLE`、`ZP_SERVER_HANDLE`、`ZP_CONNECTION_HANDLE`、`ZP_REQUEST_HANDLE` 和 `ZP_CHANNEL_HANDLE`。Client 公开头暴露被控端生命周期；Server 公开头暴露管理端生命周期、Connection 所有权及管理操作。具体对象布局和 Connection 握手状态不属于公开 ABI。

Client 配置包含：

- 按尝试顺序排列的 Endpoint 数组；
- Deployment 根证书 DER；
- 可选的 CNG 客户端持久化密钥名；为空时由 SDK 使用默认名称；默认使用机器范围，也可明确选择当前用户范围；
- 连接超时，0 使用 10 秒默认值；
- 单连接入站 Request 数量和 Payload 总量上限，以及本机 Channel 上限；
- 状态回调、操作回调和调用方 Context。

Server 配置包含：

- Listener 数组；Listener 的 `Host` 为空表示通配绑定；
- Deployment 数组，每项由 `ServerName` 和带可用私钥的 Windows `PCCERT_CONTEXT` 组成；SDK 在 `Create` 中复制证书 Context，调用方随后可释放原引用；
- 单连接出站 Request 和 Channel Handle 上限；
- Server 生命周期回调、单连接阶段回调和调用方 Context。

Client 状态为 `Stopped`、`Connecting`、`Authenticating`、`Ready`、`RetryWait` 和 `Stopping`；Server 状态为 `Stopped`、`Starting`、`Running` 和 `Stopping`；Server 单连接以及需要统一表达的连接阶段使用 `Connecting`、`Authenticating`、`Ready` 和 `Closed`。状态回调中的 `ZP_STATUS` 表示触发当前转换的结果，成功转换使用 `{ None, 0 }`。

`Create` 验证并复制数组、字符串和证书配置，成功后对象处于 `Stopped`；回调函数指针和 Context 只保存值。`Start`、`Stop` 和 `Close` 遵循本节前述异步生命周期契约。第一版不公开内部 Connection 结构，也不允许调用方直接驱动 Frame 状态机。

Server 连接回调中的 `ZP_CONNECTION_HANDLE` 默认只在当前回调期间借用。应用需要在回调外保存连接时调用 `ZpConnection_AddRef`，不再使用时调用 `ZpConnection_Release`；引用只保持对象内存有效，不阻止网络断开。连接进入 Closed 后，新的管理操作返回连接终止状态，所有已提交的 Request 和 Channel 仍恰好终止一次。

SDK 对象通过内部 `ZP_TRANSPORT_OPERATIONS` 操作表持有具体 Transport 的 `Start`、`Stop` 与 Context，公开生命周期不依赖 MsQuic 或后续 Transport 的对象布局。状态修改在对象锁内串行化，状态回调在锁外调用；进入回调前增加活动回调计数，返回后减少，因此即使状态已变为 `Stopped`，回调栈内或并发的 `Close` 仍返回 `STATUS_DEVICE_BUSY`，不会释放正在被回调使用的对象。

Client `Start` 只允许 `Stopped -> Connecting`，Server `Start` 只允许 `Stopped -> Starting`；缺少 Endpoint、Listener 或 Deployment 返回 `STATUS_INVALID_PARAMETER`，尚未安装对应 Transport 适配返回 `STATUS_NOT_SUPPORTED`。Transport 启动失败时立即回到 `Stopped`，状态回调携带该失败状态。Client 后续只接受 `Connecting -> Authenticating/RetryWait/Stopped`、`Authenticating -> Ready/RetryWait/Stopped`、`Ready -> RetryWait/Stopped`、`RetryWait -> Connecting/Stopped` 和 `Stopping -> Stopped`；Server 只接受 `Starting -> Running/Stopped`、`Running -> Stopped` 和 `Stopping -> Stopped`。

`Stop` 对 `Stopped` 和 `Stopping` 幂等；其他状态先同步进入 `Stopping` 并通知回调，再请求 Transport 异步停止。Transport 完成资源回收后通过受控内部通知进入 `Stopped`。Transport 操作表和状态通知均为 SDK 内部契约，不属于公开 ABI。

Server 通过 `ZpServer_SendRequest` 对指定已认证 Connection 创建引用计数 Request Handle；同步拒绝不会触发完成回调，成功提交后 Response、显式取消或连接终止恰好完成一次。Transport 可以在发起 API 返回前同步交付 Response，因此完成回调允许重入发起线程并立即 `ZpRequest_Close`；SDK 在提交期间持有额外临时引用，发起 API 不会在回调关闭调用方引用后继续访问已释放对象。调用方可通过 `ZpRequest_Cancel` 尽力发送 Cancel，并在不再使用句柄时调用 `ZpRequest_Close` 释放调用方引用。

`ZpServer_OpenFileRead` 仍以 Request Handle 表示异步打开阶段；成功 Open 回调交付独立的 Channel Handle、FileSize 和确认 Offset。Server SDK 在 Open 回调返回后自动授予 1 MiB 初始窗口，每次 Data 回调返回后自动补回等量额度。Data Buffer 仅在回调期间有效；远端 Close、本地取消或连接终止恰好触发一次 Channel Close 回调。调用方可通过 `ZpChannel_Cancel` 尽力发送 `STATUS_CANCELLED` 的 ChannelClose，并在不再使用句柄时调用 `ZpChannel_Close` 释放调用方引用。

Server 的非零 `TimeoutMilliseconds` 同时建立基于 `GetTickCount64` 的本地 Deadline；每条连接的线程池定时器始终只等待最近截止项，到期请求以 `STATUS_IO_TIMEOUT` 完成并尽力发送 Cancel，Response、取消和计时器竞争由同一请求表锁串行化。

Client 完整收到 Request 后检查方向、字段和配额，随后复制 Payload 并投递线程池；MsQuic 接收回调不执行系统查询等业务工作。每条连接维护活动入站 Request 表和引用计数，Cancel、连接关闭与工作完成只竞争一次终止。来自已认证 Server 的协议合法操作直接执行；Windows 本机权限或资源不足通过对应来源类型和原始码返回。

Client 的 `MaxRequestsPerConnection` 和 `MaxRequestPayloadBytesPerConnection` 限制已投递且尚未完成的入站 Request 及其深拷贝 Payload；`MaxChannelsPerConnection` 限制正在创建或已经激活的本机 Channel。Server 使用同名对象上限约束每连接的调用方 Handle。名额在执行业务或创建 OS 资源前预留，达到上限返回 `STATUS_QUOTA_EXCEEDED`，并在创建失败、响应失败、取消、关闭或完成后立即归还。

Client Endpoint、Server Listener 和 Server Deployment 数组第一版各最多 64 项；Deployment 根证书 DER 最大 1 MiB。非空数组与源指针必须成对提供；可选字符串使用 `NULL` 表示缺省，提供空字符串视为无效配置。ServerName 在同一 Server 配置中按不区分大小写方式保持唯一。所有深拷贝使用单块对象内存，Server 额外持有通过 `CertDuplicateCertificateContext` 获得的证书引用，并在 `Close` 时逐项释放。

## 9. 功能模块

当前协议包含 46 个模块：

- `System`（ModuleId 1）：系统基础信息。
- `Process`（ModuleId 2）：进程枚举、查询、控制、转储和内存读写。
- `Service`（ModuleId 3）：服务枚举、配置、控制、依存关系和恢复设置。
- `File`（ModuleId 4）：目录分页、属性、搜索、哈希、传输、ACL、范围读写、归档分页、快捷方式解析、分档图片预览和被控端 URL 下载。

URL 下载是 Client 本机后台作业，不把文件内容经 Server 或 Web 中转。BITS 引擎用于弱网下的自动重试和断点续传，WinHTTP 引擎用于立即直连；两者都只接受 HTTP/HTTPS，先写目标目录内的唯一临时文件，成功后再以同卷原子移动提交到指定文件名。协议只传递 URL、目标路径、引擎、覆盖标志和作业状态元数据，未知总长度以协议哨兵值表达，不把 64 位字节数降为不精确的 JSON 数字。

文件类型注册表由后缀映射到显示图标、I18N 类型名和右键菜单项；复合后缀按最长匹配，未知类型不猜测。文本按 64 KiB 分页读取，结构化查看设定 8 MiB 上限。原图、PDF、音视频使用支持 HTTP Range 的原始字节流；图片低、中、高档在 Client 端通过系统 WIC 缩放并编码为有界 JPEG，避免先传原图再在浏览器缩小。归档目录直接调用系统 `archiveint.dll` 导出的 libarchive 公共 ABI，不解析 `tar.exe` 的本地化文本输出，也不携带第三方运行时。
- `Terminal`（ModuleId 5）：系统 ConPTY 会话、输入输出、Resize、批处理、PowerShell、WSH VBScript/JScript/WSF、HTA 脚本和退出状态；原生 `.js` 由 WSH JScript 执行，不依赖或回退到 NodeJS。ConPTY 通过 MLE `IO_CreatePipe` 使用两组 128 KiB 异步无名 NPFS 单向管道，不轮询管道状态。
- `EventLog`（ModuleId 6）：频道枚举、事件分页、Bookmark、属性、启停和清除。
- `Registry`（ModuleId 7）：键和值的枚举、读写、重命名、ACL 和范围读写。
- `Window`（ModuleId 8）：窗口树、属性、控制、静态捕获和视频流。
- `User`（ModuleId 9）：本地用户、终端会话和登录会话管理。
- `Execution`（ModuleId 10）：程序和脚本的远程执行、运行环境探测与状态反馈。Client 只读取运行时可执行文件的版本资源和 PE 头，不为探测版本启动解释器；同一进程创建层统一负责身份、会话、环境、隐藏窗口和进程树终止。`Terminal` 复用该层，只负责 ConPTY 生命周期及双向 I/O。
- `Tunnel`（ModuleId 11）：通用 TCP/UDP 隧道及 RDP、CDP、WinDbg 等上层入口。
- `Browser`（ModuleId 12）：Edge/Chrome 探测、Profile、浏览数据和 CDP 会话；浏览器运行时也可读取正在使用的 Profile 数据，Cookie 和密码的加密内容默认不显示明文，当前账户可解密的值可按需显示，无法解密的 App-Bound 值明确标记保护状态。
- `Wmi`（ModuleId 13）：命名空间、类、实例和 WQL 查询。
- `Audio`（ModuleId 14）：音频设备、会话、音量及输入输出流。
- `Video`（ModuleId 15）：摄像头枚举、Media Foundation 采集、JPEG 编码和带背压的实时画面流。
- `Rtc`（ModuleId 16）：WebRTC 数据通道建立和流量转发。
- `Serial`（ModuleId 17）：串口枚举、配置和双向流式数据。
- `Recording`（ModuleId 18）：窗口、摄像头和音频录制任务。
- `PortableDevice`（ModuleId 19）：MTP 设备和对象的浏览、传输与管理。
- `Software`（ModuleId 20）：传统程序、Windows App、可选功能及当前账户输入法管理；可选功能直接通过 CBS 枚举完整父子关系和当前、预期、请求状态，启停时解析依赖并返回重启要求。动态探测 WinGet、Python/pip、NodeJS/npm、Chocolatey 和 .NET Global Tools，枚举其全局程序包，并统一承载安装包下发及后台部署作业。NodeJS/npm 仅表示已安装的第三方运行时和程序包管理器，不参与 WSH JScript 执行。WinGet 使用 `Microsoft.Management.Deployment`，其余包管理器直接启动已定位的真实可执行文件，不经过 Shell。Python 定位显式排除 WindowsApps 执行别名，并按 PEP 514 查询真实解释器注册信息。
- `Hardware`（ModuleId 21）：原生硬件和设备信息与控制。
- `Update`（ModuleId 22）：Windows Update 状态、历史和检查操作。
- `Task`（ModuleId 23）：任务计划枚举、控制以及任务 XML 的读取和更新。
- `Firewall`（ModuleId 24）：防火墙配置文件和规则管理。
- `Power`（ModuleId 25）：电源计划和系统电源操作。
- `SystemAdministration`（ModuleId 26）：系统配置、环境信息及远程桌面配置和多会话内存补丁管理。
- `Wlan`（ModuleId 27）：无线网络、接口和配置文件管理。
- `Certificate`（ModuleId 28）：用户和计算机证书存储管理，以及指定作用域、存储区和私钥选项的静默安装。
- `Clipboard`（ModuleId 29）：剪贴板格式、内容和变化等待。
- `Credential`（ModuleId 30）：Windows 与 Web 凭据管理。
- `Firmware`（ModuleId 31）：CPUID、SMBIOS、ACPI 和 UEFI 管理。
- `NetworkShare`（ModuleId 32）：发布共享和当前共享连接管理。
- `NetworkStatus`（ModuleId 33）：适配器、路由和网络端点管理。
- `PageFile`（ModuleId 34）：页面文件配置。
- `Bluetooth`（ModuleId 35）：蓝牙无线电和设备管理。
- `Keyboard`（ModuleId 36）：按需等待键盘事件。
- `Location`（ModuleId 37）：按需读取 Windows 位置报告。
- `Font`（ModuleId 38）：用户和计算机字体管理。
- `AppContainer`（ModuleId 39）：AppContainer Profile、能力和回环配置。
- `WinObj`（ModuleId 40）：Windows Object Manager 命名空间按层浏览；节点首次展开后按直接子目录结果校正其可展开状态。
- `Wsl`（ModuleId 41）：WSL 发行版、互操作配置和 Linux 进程管理。
- `UiAutomation`（ModuleId 42）：按层惰加载 Windows UI Automation 树。
- `ProxyVpn`（ModuleId 43）：WinINET、WinHTTP 代理和 RAS VPN 配置管理。
- `ClientStatus`（ModuleId 44）：Client 进程、启动、会话、安全、资源与环境状态。
- `ShadowCopy`（ModuleId 45）：系统保护、还原点和卷影副本管理。
- `BitLocker`（ModuleId 46）：直接通过 FVE API 枚举卷、转换状态、保护状态、锁定状态、加密方法、范围、自动解锁状态和密钥保护器；支持全卷或仅已用空间加密、解密、转换暂停与恢复、保护暂停与恢复、数据卷锁定与恢复密码解锁，以及恢复密码保护器的创建和删除。

ModuleId 9、20–46 共用 `Administration` 的固定记录 Codec 和执行框架，但仍保留独立的 ModuleId 和请求路由；目录复用不改变协议模块数量。

远程桌面补丁定义来自 `Source/3rdParty/rdpwrap.ini` 子模块。Client 上报 `termsrv.dll` 文件版本，Server 仅下发该精确版本所需的 RVA、属性和值；Client 校验文件版本、已加载映像和原始代码后，通过 `NtReadVirtualMemory`、`NtProtectVirtualMemory` 与 `NtWriteVirtualMemory` 检查或修改 `TermService` 进程。操作要求 Client 具有调试特权，不替换磁盘文件、不修改服务配置。Client 应用补丁时在进程内保存原字节，关闭补丁时原位恢复；Client 重启导致备份丢失时，通过重启 `TermService` 恢复。服务或系统重启后补丁失效。

输入法清单和控制均在 Client 进程当前账户下执行。Client 返回当前账户（用户名）与交互式桌面状态，Web
也统一使用这一表述。清单由 `input.dll` 的当前用户设置与 TSF profile 合并生成，控制使用
`InstallLayoutOrTip` 和 `ITfInputProcessorProfileMgr::ActivateProfile`，不直接读写注册表。无交互式桌面时
仍可查看、启用、禁用和设置默认项，但不提供当前会话切换。

远程浏览器复用 `Browser` 的 Edge/Chrome 与原生 Profile 探测、`Execution` 的受控进程作业和 `Tunnel`
的回环 CDP 转发。浏览器固定使用 `--headless` 和独立 `--user-data-dir`：临时与无痕会话使用可清理暂存
目录，新建 Profile 使用独立持久目录，选择浏览器现有 Profile 时先返回源大小、目标磁盘可用空间和
浏览器运行状态，经控制端确认后一次性复制 `Local State` 与所选 Profile；任何会话都不直接
打开或修改原 Profile。所有 Headless 会话禁用同步与后台联网；临时和无痕会话还禁用用户扩展、后台组件
扩展与默认应用。DevTools 端口就绪后通过原生网络端点定位真正监听端口的浏览器 PID，并连同进程创建时间
保存；关闭会话时终止该真实进程树，而不是只结束可能已经退出的浏览器启动器。临时会话随后以有界重试
删除 Profile，覆盖进程退出与文件句柄释放之间的竞态。远程控制和 DevTools 连接同一个多标签浏览器会话，
前者只允许导航、历史、对话框和输入所需的 CDP 方法，无法解析的独立消息被丢弃而不关闭健康连接。

CDP `Page.startScreencast` 按 Chromium 协议产生 JSON 内 Base64 JPEG。Tunnel 将通用 TCP 数据标记为可
自适应压缩，避免可压缩文本在弱网中保持 Base64 的线速膨胀；Web 桥接层直接从 UTF-8 JSON 解出 JPEG
并用二进制 WebSocket 发送给管理浏览器，不生成 UTF-16 Base64 字符串，也不把图像再次包入 JSON。

`Administration` 的当前记录种类还覆盖电池、WSL 发行版与进程、UI Automation 元素与属性、WinINET/WinHTTP 代理和 RAS VPN。WSL 进程以发行版、Linux PID 与启动时间组成稳定操作身份，支持 Linux 本身可可靠表达的终止、挂起和恢复；UI Automation 只初次枚举桌面直接子元素，展开节点时再按路径读取直接子元素，选中节点时再读取 UIA 属性和控件模式可用性，避免一次遍历完整自动化树。

模块契约遵循以下规则：

- 公共头文件定义 ModuleId、OperationId、请求结构、结果 View 和资源上限；模块目录中的 `Protocol.c`、`Client.c`、`Server.c` 分别负责 Codec、被控端执行和管理端控制。
- 线上 Payload 使用固定 Codec，不传本机指针、Handle、结构体填充或依赖编译器对齐的数据。
- 所有模块服从唯一的 Client 版本；当前未发布，不保留旧 Decoder、兼容分支或自动降级。
- 大型文件、终端、隧道、原图和音频数据使用带窗口的 Channel，按需传输，不塞入单个 Response 或完整缓存在 Web 层；Client 生成且有明确大小上限的图片预览可以使用二进制 Response。
- 本机 API 的失败保留 NTSTATUS、Win32、Winsock、HRESULT、Security、QUIC 或 ProcessExit 等来源类型和原始 32 位代码。
- 破坏性操作必须由 Server 明确发起；Client 不扩大目标范围，不静默重试，不增加与请求语义不同的兜底路径。

## 10. 安全与资源限制

- 所有 Transport 必须验证 S 身份并使用 TLS 保护传输；
- 握手必须检查 Client 版本；Frame 解码必须检查长度、整数溢出、字段边界和消息类型；
- 未匹配的未来 RequestId、ChannelId 或未知 Data 视为协议违规；完整 Frame 按 6.1 节限制在当前消息并计入预算，仅对单调序列中已结束对象的迟到 Response/Cancel/Window/Close 做幂等忽略；
- 正式发布前，新连接应按来源 IP、Deployment 和全局维度实施连接及握手速率限制；
- S 限制同时握手数量、自动登记记录数量和每连接出站对象数量；C 限制入站 Request、Payload、Channel 及本机 OS 资源数量；
- 通过部署根认证的 S 对 C 拥有完整管理能力；C 不建立第二套操作级授权系统，业务执行失败只返回协议校验、资源限制或 Windows 本机操作状态；
- 不把静态公钥、协议格式或客户端程序的不可见性当作安全边界；
- 不自制会话密码、逐包非对称加密、Nonce 或重放保护方案，使用成熟 TLS 实现提供这些能力。

当前 Web 只作为本机回环原型：

- Kestrel 只监听 `127.0.0.1`，HTTP 请求必须使用规范 `127.0.0.1:<port>` Host；
- 浏览器请求的 `Origin` 必须与规范本机 Origin 完全相同，`Sec-Fetch-Site: cross-site` 直接拒绝，WebSocket 也只接受该 Origin；
- 没有 `Origin` 和 Fetch Metadata 的本机原生程序仍被视为可信；当前边界只解决恶意网页跨站访问和 DNS rebinding，不试图隔离任意本机进程；
- `/mcp` 与 REST 共用相同的规范 Host 边界；MCP 调用方属于上述受信任本机原生程序，当前不额外建立认证会话；
- `/api/clients` 返回当前连接及公钥 SHA-256 指纹；其余 API 必须显式携带瞬时 `ClientId`，长生命周期 Web 状态按该 ID 隔离；Native 在连接增删时触发事件，Managed/Web 据此立即释放离线 Client 的状态，不依赖后续 HTTP 流量；
- MCP 不保存目标状态，每次工具调用都验证显式 `ClientId`；内置智能体的目标由 Web 当前 Client 上下文绑定，模型工具参数中不存在目标字段；
- 模型凭据使用 ASP.NET Core Data Protection 和当前 Windows 账户 DPAPI 加密后写入 `%LOCALAPPDATA%\KNSoft\ZPigeon`。批量列表不返回凭据；具备当前 Web 管理权限的详情接口可按项返回解密值，以支持显示和复制，但响应禁止缓存且不得写入日志；
- 对话、工具参数和工具结果会发送给所选模型 Provider。OpenAI Responses 请求显式设置 `store: false`，并保存加密推理内容以支持无状态工具续传；Chat Completions 不发送非通用存储参数，Anthropic 保存带签名的 Thinking 内容。实际数据处理与保留仍由 Provider 决定，页面必须明确提示；
- 模型输出和工具结果均视为不可信数据；管理端按纯文本显示模型回复，Agent 提示模型不得把工具结果当作指令，只有用户明确要求时才能读取 Cookie、密码等敏感数据，并限制消息、输出和工具结果大小、顺序执行工具、限制工具循环和连续工具错误；
- Native 同时维护 ClientId 和 Connection 哈希索引；Client 列表在一次快照中返回地址、指纹和连接统计。首页连接变化与管理页状态使用 SSE，浏览器不创建固定周期 HTTP 轮询；
- HTTP 请求取消会沿 Managed/Native Context 定位到精确 SDK Request 并调用 `ZpRequest_Cancel`，浏览器断开后不继续占用远端请求名额；
- Web 文案使用共享的稳定 I18N Key，中文和英文目录必须键集合一致；不受支持的浏览器语言回退英文，代码分析同时校验显式 Key、既有页面源文案、动态模板及 Native/Managed 生成文案；
- 当前未知客户端公钥完成持钥签名即可连接，尚无入组审批；未来发布或对外代理前必须单独确定认证和 Enrollment 方案。

确切限制值由两端配置和压力测试结果确定，不在协议中无依据地固化。

## 11. 依赖边界

- 优先使用 KNSoft.NDK 已提供的 NT 层定义和系统能力；
- 优先复用 KNSoft.MakeLifeEasier 的通用函数；
- 最低系统为 Windows 10，允许直接采用 Windows 10 及以上能力，不增加旧系统兼容或回退路径；
- QUIC 基础由 KNSoft.Quic 提供；
- MCP 使用官方 Model Context Protocol C# SDK 的无状态 Streamable HTTP 实现；工具 Schema 使用 Microsoft.Extensions.AI，模型协议以直接 HTTP/JSON 实现 OpenAI Responses、OpenAI Chat Completions 和 Anthropic Messages；
- ZPigeon 自己负责连接状态、Frame、Protocol Dispatcher 和业务模块；
- 如果实现所需辅助函数具有独立的 common library 价值，应先由 Owner 决定是否抽到 KNSoft.MakeLifeEasier，再进行编码和依赖同步。

## 12. 第一版密码与握手规格

- Deployment 根密钥和 S 在线密钥均使用 ECDSA P-256；根证书与在线证书使用标准 DER X.509；
- C 内置 Deployment 根证书 DER，TLS 握手时建立到该根的专用证书链并严格验证 `ServerName`；不回退到系统公共根，也不忽略名称、有效期或签名错误；
- S 在线证书由 Deployment 根证书签发，EKU 必须允许 Server Authentication；轮换通过同时部署新证书并保持同一根完成；
- C 实例密钥为 CNG `ECDSA_P256` 持久化密钥，由宿主明确选择当前用户或本地计算机作用域；私钥不可导出；
- 客户端公钥在线上使用 SEC1 非压缩格式 `0x04 || X[32] || Y[32]`，该公钥本身作为实例身份；
- S 的 Challenge 使用系统 CSPRNG 生成 32 字节，每条连接只使用一次；
- 客户端签名摘要为 `SHA-256("KNSoft.ZPigeon.ClientAuth.v1" || 0x00 || Challenge[32] || PublicKey[65])`；
- `ClientAuthenticate` 使用 IEEE P1363 编码的 ECDSA P-256 签名，即 32 字节大端 `r` 后跟 32 字节大端 `s`；
- `ClientHello` 之后只接受 `ServerChallenge` 或版本过旧的 `ServerReject`，`ServerChallenge` 之后只接受 `ClientAuthenticate`，认证成功后 S 发送空 Body 的 `Ready`；任何越序、重复或握手阶段业务消息均以 `STATUS_PROTOCOL_UNREACHABLE` 关闭连接；
- QUIC/TLS 关闭、无法重同步的 Frame 前缀错误、身份验证失败、内部不变量错误和持续违规触发的断开均终止所有未完成请求、订阅和通道，不尝试在新连接上透明续接；可恢复的完整 Frame 错误和瞬时资源不足按 6.1 节处理。

Client 未配置 `ClientKeyName` 时使用持久化 CNG 密钥名 `KNSoft.ZPigeon.Client`。`ClientKeyScope` 显式选择当前用户或本地计算机作用域，不在两者之间回退或复制身份；当前交互式原型选择用户作用域，管理员、非管理员和 SYSTEM 服务可按宿主配置选择其作用域。SDK 通过 Microsoft Software Key Storage Provider 打开或创建 `ECDSA_P256` 密钥，只导出 `BCRYPT_ECCPUBLIC_BLOB` 并转换为线上 SEC1 格式；私钥签名由 `NCryptSignHash` 在 Provider 内完成。Server 使用系统首选 CSPRNG 生成 Challenge，把 SEC1 公钥转换为 `BCRYPT_ECCPUBLIC_BLOB` 后通过 `BCryptVerifySignature` 验证 P1363 签名。签名验证成功前不会发送 `Ready` 或进入 Ready 阶段。

Client QUIC Transport 内部允许测试代码借用一个调用方持有的 `NCRYPT_KEY_HANDLE`，用于无持久化副作用的端到端测试；该入口不属于公开 ABI，SDK 不释放借用句柄，调用方必须保持它存活到 Client 完成关闭。正常产品路径始终使用上述选定作用域的持久化密钥。

QUIC Stream 发送为每个 Frame 持有独立异步发送 Context：MsQuic 接受发送后立即推进 Connection 发送状态，Buffer 一直保留到 `SEND_COMPLETE`；接收回调按 MsQuic Buffer 顺序交给 `ZpConnection_Receive`，由 Connection 统一处理任意分片/合并和握手越序。每连接和进程全局分别限制 32 MiB、256 MiB 未完成发送字节，并记录当前/峰值积压、最长排队时延和配额拒绝次数；达到配额只拒绝当前发送，不把健康连接误判为 fatal。交互与批量标记用于观测和后续保持通道顺序的调度，不能让 `ChannelClose` 越过此前的 `ChannelData`。Server 在 `ClientHello` 时验证并保存唯一的 Client 版本；模块没有独立版本或能力协商，行为变化直接提升 Client 版本。

### 12.1 发布前仍需固定的规格

1. 将 46 个现有模块公共头文件和 Codec 中已经实现的 Operation/Payload 契约整理为稳定 API 参考；
2. 确定第一版 ABI 版本和后续结构扩展规则；
3. 根据压力测试调整两端资源限制默认值及全局配额。
