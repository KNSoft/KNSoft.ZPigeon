# KNSoft.ZPigeon SDK 设计

状态：总体设计基线  
更新时间：2026-08-10

本文档是 KNSoft.ZPigeon SDK 的设计基线，记录系统边界、架构、协议、身份与安全模型以及待细化的技术问题。项目约束和协作约定见 `Memory.md`，实施状态与下一步见 `Progress.md`。

## 1. 目标与边界

KNSoft.ZPigeon 是面向合法 Windows 远程管理场景的 C/S 软件。计划通过 Client SDK 和 Server SDK 提供以下能力：

- 系统信息；
- 文件管理；
- 软件管理；
- 进程和服务管理；
- 事件日志和注册表管理；
- 远程终端。

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
- raw UDP 管理协议；
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
              QUIC | TLS/TCP | WSS Transport
```

网络核心只保留三层：

```text
Transport -> Connection -> Protocol Dispatcher
```

- `Transport` 提供连接、监听、发送、关闭及对应异步通知，不理解业务消息；
- `Connection` 管理单个安全连接的生命周期、接收 Buffer、Frame 切分和握手状态；
- `Protocol Dispatcher` 按消息类型和模块路由已验证的 Payload。

第一版不建立复杂 RPC 框架、通用权限框架或完整虚拟流系统。需要的请求关联、事件和通道能力直接由最小协议字段表达。

业务控制方向固定为 S -> C：S 针对已认证 Connection 发起管理 Request，C 在本机执行并返回 Response、Event 或 Channel 数据。C 不向 S 发起业务 Request；收到方向错误的业务消息时按协议错误关闭连接。Ping/Pong 仍可双向使用，不改变业务控制方向。

## 3. 项目与代码组织

解决方案包含三个静态库和四个可运行/互操作项目：

```text
KNSoft.ZPigeon.Protocol
KNSoft.ZPigeon.Client.SDK
KNSoft.ZPigeon.Server.SDK
KNSoft.ZPigeon.Client
KNSoft.ZPigeon.Server.Native
KNSoft.ZPigeon.Server.Managed
KNSoft.ZPigeon.Web
```

依赖关系：

```text
Client SDK ----> Protocol
Server SDK ----> Protocol
Client EXE ----> Client SDK + Protocol
Server Native DLL ----> Server SDK + Protocol
Managed SDK ----> Server Native DLL
Web ----> Managed SDK
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
|   |-- Process/
|   |-- Service/
|   |-- EventLog/
|   |-- Registry/
|   |-- File/
|   `-- Terminal/
|-- KNSoft.ZPigeon.Client.SDK/
|   |-- Core/
|   `-- Transport/
|-- KNSoft.ZPigeon.Server.SDK/
|   |-- Core/
|   `-- Transport/
|-- KNSoft.ZPigeon.Client/
|-- KNSoft.ZPigeon.Server.Native/
|-- KNSoft.ZPigeon.Server.Managed/
|-- KNSoft.ZPigeon.Web/
`-- SDK/
```

各项目通过工程文件选择属于自己的源文件：Protocol 编译模块目录内的 `Protocol.c`，Client SDK 编译 `Client.c`、Client Core 和 Client Transport，Server SDK 编译 `Server.c`、Server Core 和 Server Transport。`Network` 只保留双方共用的网络基础，不出现业务模块；SDK 内不再建立第二套 `Network` 目录。

模块通过最小静态描述符表接入 Core Dispatcher。Dispatcher 只完成模块和操作查找、通用状态校验及生命周期转交，不包含模块 Payload 解码或 Windows 业务 API。Module 不得包含 `HQUIC` 或直接调用 Transport；Transport 不得按 ModuleId 分发或执行业务。

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
- 线上 SEC1 公钥本身就是稳定实例标识，协议和 SDK 不再维护派生 `ClientId` 字段；
- CNG 密钥被删除、重置或因系统重装丢失后，该实例表现为新的公钥身份；
- S 不自动把新的公钥身份与历史记录合并；
- 未知公钥完成客户端密钥签名握手后即可由 S 操作。

当前模型只证明“后续连接仍持有同一客户端私钥”，不额外证明该实例经过入组审批。如果以后需要受控入组，应另行增加一次性 Enrollment 凭据，不能把客户端自签名误当作部署授权。

## 5. Transport 与 Endpoint

计划支持三种 Transport，默认优先级为：

1. QUIC；
2. TLS/TCP；
3. WSS。

约束：

- QUIC 使用 KNSoft.Quic 提供的 MsQuic 基础；
- TLS/TCP 使用 Windows TLS 能力；
- WebSocket 只支持 WSS；
- raw UDP 已从 Transport 方案删除，QUIC 底层使用 UDP 不等于提供 raw UDP Transport；
- 不支持明文 TCP、WS，也不从安全协议自动降级到明文协议；
- 第一版统一向上提供可靠、有序的字节传输语义；
- QUIC 第一版只需使用一条双向 Stream，不立即暴露 QUIC 多流能力。

QUIC 第一版使用 ALPN `knsoft-zpigeon/1`。Client 单独解析 Endpoint 的 `Host` 并设置 MsQuic 远端地址，再把 `ServerName` 作为 SNI 传入，因此连接目标与身份名称不会被混为同一字段。Client 为配置中的 Deployment 根 DER 建立内存证书库和 `hExclusiveRoot` 专用链引擎，通过 MsQuic 延迟证书验证事件执行 Windows SSL 链策略与 `ServerName` 校验，验证完成前返回 `QUIC_STATUS_PENDING`；不得设置 `NO_CERTIFICATE_VALIDATION`，也不得回退系统公共根。Schannel 路径不设置 `QUIC_CREDENTIAL_FLAG_INPROC_PEER_CERTIFICATE`，从而保证证书事件提供原生 `PCCERT_CONTEXT`；若将来改用该标志，必须同时按 MsQuic 的序列化或 portable certificate 契约重写验证入口，不得把 blob 强制转换为证书 Context。

Server 为每个 Deployment 创建独立 MsQuic Configuration 并装载其 `PCCERT_CONTEXT`，新连接按 SNI 不区分大小写精确选择 Configuration；缺失或未知 SNI 直接拒绝。Server 只允许对端创建一条双向 Stream，Client 在 TLS 连接完成后创建该 Stream；额外 Stream 或单向 Stream 是协议错误。Listener、Connection 和 Stream 均遵循 MsQuic 的异步停止/`SHUTDOWN_COMPLETE` 后关闭规则，Registration 的同步关闭不得发生在 MsQuic 回调栈内。

Endpoint 至少需要表达：

- Transport 类型；
- 实际主机或 IP；
- 端口；
- 用于 TLS/SNI 验证的 `ServerName`；
- WSS 路径（仅 WSS）；
- 优先级或列表顺序。

C 按配置顺序尝试 Endpoint；连接超时、失败后的轮次推进、重连退避和已连接后的 Transport 切换规则见 8.1 节。

## 6. 连接与握手

逻辑流程：

```text
选择 Endpoint
    -> 建立 QUIC/TLS/WSS 安全连接
    -> 使用 Deployment 根公钥验证 S 在线证书
    -> 交换 Hello 与版本/模块能力
    -> S 发出客户端 Challenge
    -> C 使用实例私钥签名
    -> S 验证签名和客户端公钥
    -> 登记或加载客户端记录
    -> Ready
```

握手只保留实际需要的信息：

- C 核心协议版本；
- C 各模块协议版本；
- 客户端公钥；
- Challenge、签名以及必要的防重放数据。

Deployment 已由当前连接的 SNI 和证书上下文确定，握手中不重复传输 DeploymentKeyId。

C 不内置、不依赖 S 的协议版本。双方只选择当前实现明确支持的版本；不存在对应实现时拒绝协商，不保留未要求的旧版 Decoder、兼容路径或降级逻辑。

QUIC、TLS/TCP 和 WSS 均使用 TLS 派生的对称会话密钥保护业务数据。分组公私钥不用于逐包加解密，也不在 TLS 外增加应用层二次加密。Client QUIC 使用 20 秒 KeepAlive，避免空闲但仍有效的控制连接被默认空闲超时关闭。

### 6.1 Connection 状态机与接收缓存

Connection 通用实现位于共享的 `Source/Network`，由 Client SDK 和 Server SDK 分别编译，不放入 Protocol 静态库。Protocol 仍只负责无状态的 Frame 和 Payload 编解码。

握手状态按本端下一步动作显式推进：

```text
Client: SendHello -> WaitChallenge -> SendAuthenticate -> WaitReady -> Ready
Server: WaitHello -> SendChallenge -> WaitAuthenticate -> SendReady -> Ready
```

- Transport 成功接受一条本端握手消息后，Connection 通过发送通知推进到下一状态；
- 收到完整且顺序正确的握手 Frame 后，Connection 先推进状态，再调用消息回调，使回调可以立即生成下一条握手消息；
- `Ready` 前不允许业务消息，`Ready` 后不允许重复握手消息；越序、方向错误或重复消息以 `STATUS_PROTOCOL_UNREACHABLE` 关闭连接；
- 消息回调返回失败时当前连接立即进入关闭状态，具体 Transport 关闭由上层 Network 代码执行；
- 同一连接的接收和发送状态通知由 SDK 串行调用，Connection 本身不增加热路径锁。

接收路径针对 Transport 的任意分片和合并交付：连续完整 Frame 直接解码，不复制到中间 Buffer；不完整的 4 字节长度前缀保存在 Connection 内；分片 Frame 的堆 Buffer 从 4 KiB 起按已实际收到的数据增长，最大不超过 Frame 声明长度，不能仅凭未受信任的长度前缀立即分配 16 MiB。一次接收包含多个 Frame 时逐项分派，回调中的 View 只在当前回调返回前有效。

## 7. Protocol 与 Frame

Protocol 负责：

- 线上数据结构定义；
- Frame 编码、解码和边界校验；
- 固定版本 Payload Codec；
- 消息和模块版本处理；
- 架构无关的整数、字符串、数组及 Buffer 表达。

Protocol 不允许直接发送本机 C 结构体。线上格式使用：

- 小端整数；
- 按模块版本固定的字段顺序；
- UTF-16LE 字符串；
- 显式长度或数量；
- 不依赖指针宽度、编译器对齐或结构体 Padding。

编码接口支持调用方提供 Buffer，并支持先计算所需长度。解码尽量返回指向接收 Buffer 的只读 View，避免无必要的复制和堆分配；View 的有效期不得超过所属接收 Buffer。

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
| `0x01` | `ClientHello` | C -> S | `UINT16 CoreVersion`、`UINT16 ModuleCount`、模块记录、65 字节客户端公钥 |
| `0x02` | `ServerChallenge` | S -> C | 32 字节随机 Challenge |
| `0x03` | `ClientAuthenticate` | C -> S | 64 字节 ECDSA P-256 `r || s` 签名 |
| `0x04` | `Ready` | S -> C | `UINT16 ModuleCount`、协商后的模块记录 |
| `0x10` | `Request` | S -> C | `UINT64 RequestId`、`UINT16 ModuleId`、`UINT16 OperationId`、`UINT32 TimeoutMilliseconds`、Payload |
| `0x11` | `Response` | C -> S | `UINT64 RequestId`、`UINT16 StatusType`、`UINT32 StatusCode`、Payload |
| `0x12` | `Cancel` | S -> C | `UINT64 RequestId` |
| `0x13` | `ChannelData` | 双向 | `UINT64 ChannelId`、非空数据 |
| `0x14` | `ChannelClose` | 双向 | `UINT64 ChannelId`、`UINT16 StatusType`、`UINT32 StatusCode` |
| `0x15` | `Ping` | 双向 | `UINT64 Token` |
| `0x16` | `Pong` | 双向 | `UINT64 Token` |
| `0x17` | `ChannelWindow` | 双向 | `UINT64 ChannelId`、非零 `UINT32 CreditBytes` |

其他值在 Core Version 1 中非法。消息类型的最小 Body 长度由 Protocol 解码器校验。`ChannelData` 单帧数据最大为 1 MiB，单次 `ChannelWindow` Credit 最大为 16 MiB，避免大块传输长期占用连接发送队列；其他消息仍受 16 MiB Frame 上限约束。

模块记录编码为：

```text
UINT16 ModuleId
UINT16 ModuleVersion
```

- `ModuleId` 和 `ModuleVersion` 均不得为 0；
- 单个 Hello/Ready 最多 64 条模块记录；
- 记录按 `ModuleId` 严格升序排列，不允许重复；
- S 仅选择双方版本完全相同的模块放入 `Ready`；未出现在 `Ready` 的模块在当前连接不可用。

初始业务消息语义限制为：

- `Request`：发起一次操作并携带请求关联信息；
- `Response`：返回对应请求的类型化原始状态和结果；
- `ChannelData`：传递文件或终端等长生命周期数据；
- `ChannelWindow`：由接收方增加指定 Channel 的可发送字节额度；
- `Ping`、`Pong`：连接存活检测。

`RequestId` 和 `ChannelId` 均为连接内非零 `UINT64`，在各自命名空间从 1 单调递增且永不复用，不按奇偶或方向预留取值。第一版 Request 只由 Server 创建；Client 发来的业务 Request 是协议错误。Client 只接受严格大于本连接最高已见值的 RequestId，允许发送失败造成的序号空洞，但拒绝重复或倒序 Request。已结束 Request 的迟到 Cancel 和 Response 幂等忽略，未来未知 ID 仍是协议错误；只保存最高值或下一分配值，不建立 tombstone 表。第一版业务 Channel 只由 Client 创建。`ModuleId` 和 `OperationId` 为非零 `UINT16`。

`StatusType` 固定占 16 位，`StatusCode` 固定占 32 位，线上合计 6 字节，不传本机结构体填充或 64 位扩展码。Type 定义为 `None = 0`、`NTSTATUS = 1`、`Win32 = 2`、`Winsock = 3`、`HRESULT = 4`、`Security = 5`、`QUIC = 6`、`ProcessExit = 7`；Code 保留来源 API 返回的原始 32 位 bit pattern，不做跨错误域映射。`None` 只允许 Code 0；其他错误域的 Code 0 统一编码为 None；ProcessExit 必须保留类型且允许退出码 0。NTSTATUS、HRESULT、Security 和 QUIC 按各自有符号成功语义判断，Win32/Winsock 仅 Code 0 成功，ProcessExit 表示会话正常收尾而不是把退出码解释成错误。

`TimeoutMilliseconds` 是接收方从完整收到 Request 起计算的处理预算；0 表示协议层不额外施加超时。发送方 SDK 仍维护本地 Deadline：Deadline 到期后在本地以 `STATUS_IO_TIMEOUT` 完成操作，尽力发送 `Cancel`，并忽略迟到的 Response。显式取消在本地以 `STATUS_CANCELLED` 完成，`Cancel` 不要求单独响应。

Channel 使用接收方授信的字节窗口提供背压：新 Channel 建立后发送额度为 0，接收方发送 `ChannelWindow` 后发送方才能发送不超过累计剩余额度的 `ChannelData`；每次授信非零且不超过 16 MiB，剩余额度不得溢出 64 位计数。SDK 把 Data 回调返回视为对应 Buffer 已消费，并自动补回等量窗口。任一方发送一次 `ChannelClose` 即终止 Channel，Status 为终止结果且不回送第二个 Close。由于 ChannelId 单调且不复用，本地关闭或拒绝建立后收到不大于最高已见 ID 的迟到 Window 或 Close 时幂等忽略；未来 ID、未知 Data 或额度违规仍视为协议违规。

### 7.2 固定 Codec

Core Version 1 使用以下固定 Codec：

- `BYTE`、`UINT16`、`UINT32`、`UINT64` 和 `INT32` 分别占 1、2、4、8 和 4 字节；
- `BOOLEAN` 占 1 字节，只允许 0 和 1；
- 字节串编码为 `UINT32 ByteLength` 后跟原始字节；
- UTF-16LE 字符串编码为 `UINT32 CodeUnitCount` 后跟对应数量的 16 位代码单元，不包含结尾 NUL；
- 数组编码为 `UINT32 ElementCount` 后跟逐项固定编码；
- 可选值先编码一个 `BOOLEAN Present`，为 1 时紧跟该值；
- 需要边界隔离的嵌套对象编码为 `UINT32 ByteLength` 后跟其内部固定编码；
- 不发送本机指针、`SIZE_T`、`HANDLE`、C 结构体 Padding 或依赖编译器布局的数据。

通用 Codec 的单个字节串、字符串或数组计数上限为 `0x00100000`；模块可制定更小上限。长度乘法和游标加法必须在访问 Buffer 前检查溢出。解码成功得到的 View 指向原始接收 Buffer，地址可能未对齐，其生命周期止于接收 Buffer 被释放或复用。

### 7.3 版本演进

- 最外层 Frame 格式保持稳定；
- C 报告核心协议版本和各模块版本；
- 模块 Payload 使用确定版本的固定 Codec；
- 结构变化通过模块版本演进；
- 当前代码只实现当前发布所需版本，不背负未发布旧版本兼容；
- 没有共同实现版本时明确拒绝协商，不自动降级或猜测 Payload 格式。

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

Protocol 第一阶段公开 `ZpFrame_*` 与 `ZpCodec_*` 纯函数；它们不分配内存、不持有全局状态。编码支持 `Buffer == NULL` 的长度计算模式，实际写入模式遇到容量不足返回 `STATUS_BUFFER_TOO_SMALL`。解码遇到不完整 Frame 返回 `STATUS_MORE_PROCESSING_REQUIRED`，遇到非法长度、字段或枚举返回 `STATUS_DATA_ERROR`，遇到不支持的版本返回 `STATUS_REVISION_MISMATCH`。除长度计算契约明确要求的输出外，失败时不初始化输出参数。

### 8.1 Endpoint 与重连默认值

Endpoint 记录由 `Transport`、`Host`、`Port`、`ServerName` 和可选 `WssPath` 构成。`Host` 是实际连接目标；`ServerName` 必须为非空 DNS 名称，用于 SNI 和证书名称验证，即使 `Host` 是 IP 也不省略。只有 WSS 允许非空 Path，Path 必须以 `/` 开头。

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

- `Size`，第一版必须为 `sizeof(ZP_CLIENT_CONFIG)`；
- 按尝试顺序排列的 Endpoint 数组；
- Deployment 根证书 DER；
- 可选的 CNG 客户端持久化密钥名；为空时由 SDK 使用默认名称；默认使用机器范围，也可明确选择当前用户范围；
- 严格按 `ModuleId` 升序排列的模块版本；
- 连接超时，0 使用 10 秒默认值；
- 单连接入站 Request 数量和 Payload 总量上限，以及本机 Channel 上限；
- 状态回调、可选的 Pong 回调和调用方 Context。

Server 配置包含：

- `Size`，第一版必须为 `sizeof(ZP_SERVER_CONFIG)`；
- Listener 数组；Listener 的 `Host` 为空表示通配绑定，只有 WSS 使用非空 Path；
- Deployment 数组，每项由 `ServerName` 和带可用私钥的 Windows `PCCERT_CONTEXT` 组成；SDK 在 `Create` 中复制证书 Context，调用方随后可释放原引用；
- 严格按 `ModuleId` 升序排列的模块版本；
- 单连接出站 Request 和 Channel Handle 上限；
- Server 生命周期回调、单连接阶段回调和调用方 Context。

Client 状态为 `Stopped`、`Connecting`、`Authenticating`、`Ready`、`RetryWait` 和 `Stopping`；Server 状态为 `Stopped`、`Starting`、`Running` 和 `Stopping`；Server 单连接以及需要统一表达的连接阶段使用 `Connecting`、`Authenticating`、`Ready` 和 `Closed`。状态回调中的 `ZP_STATUS` 表示触发当前转换的结果，成功转换使用 `{ None, 0 }`。

`Create` 验证并复制数组、字符串、证书和模块配置，成功后对象处于 `Stopped`；回调函数指针和 Context 只保存值。`Start`、`Stop` 和 `Close` 遵循本节前述异步生命周期契约。第一版不公开内部 Connection 结构，也不允许调用方直接驱动 Frame 状态机。

Server 连接回调中的 `ZP_CONNECTION_HANDLE` 默认只在当前回调期间借用。应用需要在回调外保存连接时调用 `ZpConnection_AddRef`，不再使用时调用 `ZpConnection_Release`；引用只保持对象内存有效，不阻止网络断开。连接进入 Closed 后，新的管理操作返回连接终止状态，所有已提交的 Request 和 Channel 仍恰好终止一次。

SDK 对象通过内部 `ZP_TRANSPORT_OPERATIONS` 操作表持有具体 Transport 的 `Start`、`Stop` 与 Context，公开生命周期不依赖 MsQuic 或后续 Transport 的对象布局。状态修改在对象锁内串行化，状态回调在锁外调用；进入回调前增加活动回调计数，返回后减少，因此即使状态已变为 `Stopped`，回调栈内或并发的 `Close` 仍返回 `STATUS_DEVICE_BUSY`，不会释放正在被回调使用的对象。

Client `Start` 只允许 `Stopped -> Connecting`，Server `Start` 只允许 `Stopped -> Starting`；缺少 Endpoint、Listener 或 Deployment 返回 `STATUS_INVALID_PARAMETER`，尚未安装对应 Transport 适配返回 `STATUS_NOT_SUPPORTED`。Transport 启动失败时立即回到 `Stopped`，状态回调携带该失败状态。Client 后续只接受 `Connecting -> Authenticating/RetryWait/Stopped`、`Authenticating -> Ready/RetryWait/Stopped`、`Ready -> RetryWait/Stopped`、`RetryWait -> Connecting/Stopped` 和 `Stopping -> Stopped`；Server 只接受 `Starting -> Running/Stopped`、`Running -> Stopped` 和 `Stopping -> Stopped`。

`Stop` 对 `Stopped` 和 `Stopping` 幂等；其他状态先同步进入 `Stopping` 并通知回调，再请求 Transport 异步停止。Transport 完成资源回收后通过受控内部通知进入 `Stopped`。Transport 操作表和状态通知均为 SDK 内部契约，不属于公开 ABI。

Server 在 Connection Ready 后可发送 Ping 或管理 Request。Client 协议层自动回送等值 Pong；Server 通过可选 Pong 回调交付 Connection 和 Token。Ping 不创建 Request 对象。

Server 通过 `ZpServer_SendRequest` 对指定已认证 Connection 创建引用计数 Request Handle；同步拒绝不会触发完成回调，成功提交后 Response、显式取消或连接终止恰好完成一次。Transport 可以在发起 API 返回前同步交付 Response，因此完成回调允许重入发起线程并立即 `ZpRequest_Close`；SDK 在提交期间持有额外临时引用，发起 API 不会在回调关闭调用方引用后继续访问已释放对象。调用方可通过 `ZpRequest_Cancel` 尽力发送 Cancel，并在不再使用句柄时调用 `ZpRequest_Close` 释放调用方引用。

`ZpServer_OpenFileRead` 仍以 Request Handle 表示异步打开阶段；成功 Open 回调交付独立的 Channel Handle、FileSize 和确认 Offset。Server SDK 在 Open 回调返回后自动授予 1 MiB 初始窗口，每次 Data 回调返回后自动补回等量额度。Data Buffer 仅在回调期间有效；远端 Close、本地取消或连接终止恰好触发一次 Channel Close 回调。调用方可通过 `ZpChannel_Cancel` 尽力发送 `STATUS_CANCELLED` 的 ChannelClose，并在不再使用句柄时调用 `ZpChannel_Close` 释放调用方引用。

Server 的非零 `TimeoutMilliseconds` 同时建立基于 `GetTickCount64` 的本地 Deadline；每条连接的线程池定时器始终只等待最近截止项，到期请求以 `STATUS_IO_TIMEOUT` 完成并尽力发送 Cancel，Response、取消和计时器竞争由同一请求表锁串行化。

Client 完整收到 Request 后检查方向、协商模块和配额，随后复制 Payload 并投递线程池；MsQuic 接收回调不执行系统查询等业务工作。每条连接维护活动入站 Request 表和引用计数，Cancel、连接关闭与工作完成只竞争一次终止。来自已认证 Server 的已协商且协议合法的操作直接执行；Windows 本机权限或资源不足通过对应来源类型和原始码返回。

Client 的 `MaxRequestsPerConnection` 和 `MaxRequestPayloadBytesPerConnection` 限制已投递且尚未完成的入站 Request 及其深拷贝 Payload；`MaxChannelsPerConnection` 限制正在创建或已经激活的本机 Channel。Server 使用同名对象上限约束每连接的调用方 Handle。名额在执行业务或创建 OS 资源前预留，达到上限返回 `STATUS_QUOTA_EXCEEDED`，并在创建失败、响应失败、取消、关闭或完成后立即归还。

Client Endpoint、Server Listener 和 Server Deployment 数组第一版各最多 64 项；Deployment 根证书 DER 最大 1 MiB。非空数组与源指针必须成对提供；可选字符串使用 `NULL` 表示缺省，提供空字符串视为无效配置。ServerName 在同一 Server 配置中按不区分大小写方式保持唯一。所有深拷贝使用单块对象内存，Server 额外持有通过 `CertDuplicateCertificateContext` 获得的证书引用，并在 `Close` 时逐项释放。

## 9. 功能模块

第一版模块范围：

- `System.Info`：系统基本信息和连通性验证；
- `Process`：枚举、查询和控制进程；
- `Service`：枚举、查询、启动、停止和修改配置；
- `EventLog`：分页查询、Bookmark、频道启停和清除；
- `Registry`：显式 Root、WOW64 View 和 Value Type；
- `File`：枚举、属性及文件传输等管理操作；
- `Terminal`：基于 ConPTY 的双向终端，包含窗口尺寸、输入、输出和退出状态。

`System` 模块第一版固定为 `ModuleId = 1`、`ModuleVersion = 1`；`Info` 固定为 `OperationId = 1`，请求 Payload 必须为空。成功响应 Payload 依次编码 `UINT16 Architecture`（1=x86、2=x64、3=ARM64）、三个 `UINT32` Windows 主/次/Build 版本、`UINT32 ProcessorCount`、`UINT64 PhysicalMemoryBytes` 和 UTF-16LE ComputerName 字符串。

`Process` 模块第一版固定为 `ModuleId = 2`、`ModuleVersion = 1`；`Enumerate` 固定为 `OperationId = 1`，请求 Payload 必须为空。成功响应 Payload 为数组，单项依次编码 `UINT32 ProcessId`、`UINT32 SessionId` 和 UTF-16LE ImageName 字符串；PID 0 和空 ImageName 均为合法系统记录。

`Process.Query` 固定为 `OperationId = 2`，请求 Payload 为 `UINT32 ProcessId`。成功响应依次编码 `UINT32 ProcessId`、父 ProcessId、SessionId、ThreadCount、HandleCount，随后为五个 `UINT64`：CreateTime、UserTime、KernelTime、WorkingSetBytes、PrivateBytes，最后为 UTF-16LE ImageName；时间值沿用 Windows 100ns 原生计数。

`Process.Terminate` 固定为 `OperationId = 3`，请求 Payload 依次为非零 `UINT32 ProcessId` 和 `UINT32 ExitCode`，成功响应 Payload 为空。Client 打开目标进程并调用系统终止接口，权限失败按原始 `NTSTATUS` 返回。

`Service` 模块第一版固定为 `ModuleId = 3`、`ModuleVersion = 1`；`Enumerate` 固定为 `OperationId = 1`，请求 Payload 必须为空。成功响应 Payload 为数组，单项依次编码 `UINT32 ServiceType`、`CurrentState`、`ProcessId`，随后为 UTF-16LE ServiceName 和 DisplayName 字符串。字段值沿用 Windows Service Control Manager 定义；停止中的服务可返回 PID 0。

`Service.Query` 固定为 `OperationId = 2`，请求 Payload 为非空 UTF-16LE ServiceName 字符串。成功响应依次编码 `UINT32 ServiceType`、`CurrentState`、`ProcessId`、`StartType`、`ErrorControl`，随后为 UTF-16LE ServiceName、DisplayName、BinaryPathName 和 StartName 字符串；配置和状态字段沿用 Windows Service Control Manager 定义。

`Service.Start` 和 `Service.Stop` 分别固定为 `OperationId = 3` 和 `4`，请求 Payload 复用非空 UTF-16LE ServiceName 字符串，成功响应 Payload 为空。Client 打开 Service Control Manager 句柄并执行启动或停止控制，SCM 失败按原始 Win32 错误码返回。

`File` 模块第一版固定为 `ModuleId = 4`、`ModuleVersion = 1`；`Enumerate` 固定为 `OperationId = 1`，`Query` 固定为 `OperationId = 2`，`OpenRead` 固定为 `OperationId = 3`，`Hash` 固定为 `OperationId = 4`，`OpenWrite` 固定为 `OperationId = 5`，`EnumeratePage` 固定为 `OperationId = 6`。

Enumerate 与 Query 请求 Payload 均为非空 UTF-16LE Path 字符串。Query 成功响应依次编码 `UINT32 Attributes` 以及四个 `UINT64`：Size、CreationTime、LastAccessTime、LastWriteTime。Enumerate 成功响应为数组，单项编码同一组元数据后追加 UTF-16LE Name 字符串，并排除 `.` 与 `..`；结果超过单 Frame 上限时返回 `STATUS_BUFFER_OVERFLOW`。

EnumeratePage 请求依次编码 `UINT32 MaxEntries`、非空 Path 字符串和可空 Cursor 字符串，MaxEntries 范围为 1～4096。Client 以不区分大小写的 ordinal 文件名顺序返回严格大于 Cursor 的下一页；响应编码可空 NextCursor 字符串及同 Enumerate 格式的记录数组，有后续记录时 NextCursor 必须等于本页最后一个名称，否则为空。该游标无被控端状态；目录并发变化时分页是 best-effort 快照，调用方可按需要重新从空 Cursor 开始。

为完成 ordinal 排序，Client 的单次 File 枚举会建立有界目录快照：最多 65536 条记录，记录对象和排序指针的保守总量最多 16 MiB；超出时返回 `STATUS_QUOTA_EXCEEDED`，不得把截断结果伪装为完整末页。分页记录上限同时保证编码响应不超过 Core Frame 上限。

OpenRead 请求依次编码 `UINT64 Offset` 和非空 UTF-16LE Path；成功响应依次编码 Client 创建的非零 `UINT64 ChannelId`、`UINT64 FileSize` 和 Client 确认的 `UINT64 Offset`，Offset 大于 FileSize 时请求失败，等于 FileSize 时建立后正常空流结束。Server 在成功响应回调返回后授予首个接收窗口，Client 才能发送文件数据。

Hash 请求依次编码 `UINT16 Algorithm` 和非空 UTF-16LE Path，Version 1 仅定义 `Algorithm = 1` 的 SHA-256；成功响应编码相同 Algorithm、`UINT64 FileSize` 和固定 32 字节 Digest。Client 分块读取文件并在请求取消后停止计算，Digest 表示本次打开并顺序读取到的完整字节流。

OpenWrite 请求依次编码 `UINT16 Disposition`、`UINT64 FileSize` 和非空 UTF-16LE Path；Version 1 定义 `CreateNew = 1` 与 `CreateAlways = 2`。成功响应编码 Client 创建的非零 `UINT64 ChannelId` 和确认的 `UINT64 FileSize`，后续 Server 只能在 Client 授予的窗口内发送恰好 FileSize 字节。Client 应写入同目录临时文件，完整接收并刷新后按 Disposition 原子提交；取消、断线、少传、多传或写入失败均删除临时文件，不暴露部分目标文件。属性值和 100ns 时间值沿用 Windows 文件系统定义。

`Terminal` 模块固定为 `ModuleId = 5`、`ModuleVersion = 1`；`Create` 固定为 `OperationId = 1`，请求依次编码非零 `UINT16 Columns`、`UINT16 Rows`、非空 UTF-16LE CommandLine 和可空 UTF-16LE WorkingDirectory，成功响应编码 Client 创建的非零 `UINT64 ChannelId` 与非零 `UINT32 ProcessId`。同一 Channel 上 Client 到 Server 的 Data 是 ConPTY VT 输出，Server 到 Client 的 Data 是输入字节，两个方向分别由对端 Window 授信。`Resize` 固定为 `OperationId = 2`，请求编码 ChannelId、非零 Columns 和 Rows，成功响应为空。`QueryShells` 固定为 `OperationId = 3`，空请求返回 Client 当前 PATH 可解析的 `cmd.exe`、`powershell.exe`、`pwsh.exe` 位掩码。Client 或 Server 发送 ChannelClose 终止会话；正常进程退出使用 `ProcessExit` 和原始 32 位退出码，ConPTY 使用 HRESULT，进程 API 使用 Win32，NT I/O 使用 NTSTATUS。

Server 以 `ZpServer_CreateTerminal` 异步建立终端并在成功回调中交付 Channel Handle 和 ProcessId；输出 Buffer 只在 Data 回调期间有效，回调返回后 SDK 自动补回接收窗口。Client 通过 `ChannelWindow` 增加 Server 输入发送额度，SDK 以 Writable 回调通知本次新增额度；`ZpChannel_Send` 不做隐藏排队，额度不足返回 `STATUS_RETRY`，成功发送后立即扣减额度。`ZpServer_ResizeTerminal` 复用 Channel Handle 定位会话并返回独立 Request Handle。Managed 会话保存 Shell 元数据和完整 `ZP_STATUS`；WebSocket 只负责把 VT 字节流和该结构化结束结果适配给浏览器。

Client 使用系统 ConPTY，并通过 `NtCreateNamedPipeFile` 建立输入、输出两个异步匿名字节流管道；传给 `CreatePseudoConsole` 的两个 Peer 端保持到附加终端属性的根进程创建成功后再关闭。未显式指定工作目录时，Client 以自身账户的 `USERPROFILE` 作为 Shell 起始目录。专用输出线程以事件驱动的 `NtReadFile` 持续排空 VT 输出，并以 Server 发放、Server SDK 同步校验的 Window 限制发送量；Client 侧额度只是发送优化，不是 Server 的信任边界。输入首窗固定为 4 KiB，单次仅保留一个异步写。根进程退出后由独立回调关闭 ConPTY，输出线程排空最终 VT Frame，随后以原始退出码发送 ChannelClose；Server Channel 关闭、连接断开或 Client 停止均终止仍存活的根进程并结束会话。

`EventLog` 模块第一版固定为 `ModuleId = 6`、`ModuleVersion = 1`；`QueryPage`、`SetChannelEnabled` 和 `Clear` 分别固定为 `OperationId = 1`、`2` 和 `3`。ChannelPath 必须为非空 UTF-16LE 字符串；Query 是可空 XPath，空值等价于 `*`。Bookmark 是 Windows Event Log 渲染出的不透明 XML 字符串，只能作为同一 ChannelPath 与 Query 的恢复位置使用，Server 应用不解析或拼接其内容。

QueryPage 请求依次编码 `UINT16 StartMode`、`UINT32 MaxEvents`、ChannelPath、Query 和 Bookmark。StartMode 只允许 `Oldest = 1` 或 `AfterBookmark = 2`；AfterBookmark 要求非空 Bookmark，Oldest 要求空 Bookmark。MaxEvents 范围为 1～256。成功响应依次编码 `BOOLEAN HasMore`、NextBookmark 和 EventRecord 数组；每条 EventRecord 编码该事件的 Bookmark 与 `EvtRenderEventXml` 产生的 XML。非空结果的 NextBookmark 必须等于最后一条记录的 Bookmark，空结果沿用请求 Bookmark。Client 以正向查询和严格 Bookmark Seek 返回 Bookmark 之后的记录，不在 Bookmark 失效时悄悄跳到最接近的位置；日志已清除、覆盖或 Bookmark 不属于结果集时原样返回相应失败状态。

SetChannelEnabled 请求编码 ChannelPath 和 `BOOLEAN Enabled`，Client 通过 Windows Event Log Channel Configuration API 保存频道启停状态。Clear 请求只编码 ChannelPath，Client 清除该频道。两项成功响应均为空，Wevtapi 失败按原始 Win32 错误码返回；不提供实时订阅。

Bookmark 和单条事件 XML 分别限制为 64 Ki 个 UTF-16 code unit 和 1 Mi 个 UTF-16 code unit；QueryPage 编码后仍不得超过 Core Frame 的 16 MiB 上限。

`Registry` 模块第一版固定为 `ModuleId = 7`、`ModuleVersion = 1`；`EnumerateKeysPage`、`EnumerateValuesPage`、`QueryValue`、`SetValue`、`DeleteValue`、`CreateKey` 和 `DeleteKey` 分别固定为 `OperationId = 1`～`7`。

Registry Root 使用 `UINT16`：`ClassesRoot = 1`、`CurrentUser = 2`、`LocalMachine = 3`、`Users = 4`、`CurrentConfig = 5`。Registry View 使用 `UINT16`：`Default = 1`、`Registry32 = 2`、`Registry64 = 3`，后两者分别映射 32 位和 64 位 Registry View；不允许调用方直接传入任意访问掩码或原生 Handle。`CurrentUser` 明确定义为 Client 进程安全上下文的当前用户 Registry Hive，不推断其他交互式登录用户。Path 是相对于 Root 的 UTF-16LE 子键路径，允许空值表示 Root 本身，最长 32767 个 code unit。

EnumerateKeysPage 和 EnumerateValuesPage 请求依次编码 Root、View、`UINT32 MaxEntries`、`BOOLEAN CursorPresent`、Path 和 Cursor，MaxEntries 范围为 1～4096。CursorPresent 为 0 表示从头开始，为 1 表示返回严格大于 Cursor 的记录；显式存在的空 Cursor 可表示默认值名称之后的位置。Client 收集当前快照后按不区分大小写的 ordinal 名称排序，同名比较以区分大小写 ordinal 作为稳定次序补充。响应依次编码 `BOOLEAN HasMore`、NextCursor 和记录数组；HasMore 为 1 时 NextCursor 必须等于本页最后一个名称，为 0 时 NextCursor 必须为空。子键记录编码 Name 和 `UINT64 LastWriteTime`；值记录编码 Name、Windows 原生 `UINT32 Type` 和 `UINT32 DataLength`。空 Name 是合法的默认值名称。分页是 best-effort 快照，并发增删时调用方以 CursorPresent=0 重新开始。

QueryValue 请求编码 Root、View、Path 和可空 ValueName；成功响应编码 Windows 原生 `UINT32 Type` 和长度前缀原始 Data。SetValue 请求编码 Root、View、Type、Path、ValueName 和原始 Data；Version 1 保留 Windows Value Type 与字节表示，不做文本转码或环境变量展开。单值 Data 最大 1 MiB；`REG_SZ`、`REG_EXPAND_SZ` 和 `REG_MULTI_SZ` 的终止符语义由调用方负责，Client 精确写入线上字节。DeleteValue 复用 QueryValue 请求，允许删除默认值。

Server 分页 API 以 `Cursor == NULL` 表示 CursorPresent=0；非空 Cursor 指针配合零长度表示 CursorPresent=1 且名称为空，即从默认值名称之后继续。响应 Page 和 Value 中的 View 仅在回调期间有效，与其他 Server 异步 API 的 Buffer 生命周期一致。

CreateKey 请求编码 Root、View 和 Path，Path 必须非空；操作是幂等的，键已存在仍成功。DeleteKey 复用同一请求，只允许删除非空且无子键的目标，使用所选 WOW64 View，不提供递归删除，避免一次请求隐式扩大破坏范围。Registry 使用 NT Registry 接口并返回原始 `NTSTATUS`；成功的 SetValue、DeleteValue、CreateKey 和 DeleteKey Response Payload 均为空。

为完成 ordinal 排序，Client 的单次 Registry 枚举会建立有界快照：最多 65536 条记录，按当前最大名称长度估算的名称 Buffer 总量最多 16 MiB；超出时返回 `STATUS_QUOTA_EXCEEDED`，不得静默截断为看似完整的末页。分页响应仍按 Frame 上限自动缩小当前页，并通过 HasMore/NextCursor 继续推进。

大型结果不塞入单个 Response。文件和终端使用 `ChannelData` 承载连续数据；背压、窗口和断点续传按上述通用 Channel 与 File.OpenRead 规则处理，不预先建设通用虚拟流框架。

## 10. 安全与资源限制

- 所有 Transport 必须验证 S 身份并使用 TLS 保护传输；
- Frame 解码必须检查长度、整数溢出、字段边界、消息类型和版本；
- 未匹配的未来 RequestId、ChannelId 或未知 Data 视为协议错误并断开；仅对单调序列中已结束对象的迟到 Response/Cancel/Window/Close 做幂等忽略；
- 新连接按来源 IP、Deployment 和全局维度实施连接及握手速率限制；
- S 限制同时握手数量、自动登记记录数量和每连接出站对象数量；C 限制入站 Request、Payload、Channel 及本机 OS 资源数量；
- 通过部署根认证的 S 对 C 拥有完整管理能力；C 不建立第二套操作级授权系统，业务执行失败只返回协议校验、资源限制或 Windows 本机操作状态；
- 不把静态公钥、协议格式或客户端程序的不可见性当作安全边界；
- 不自制会话密码、逐包非对称加密、Nonce 或重放保护方案，使用成熟 TLS 实现提供这些能力。

确切限制值由两端配置和压力测试结果确定，不在协议中无依据地固化。

## 11. 依赖边界

- 优先使用 KNSoft.NDK 已提供的 NT 层定义和系统能力；
- 优先复用 KNSoft.MakeLifeEasier 的通用函数；
- 最低系统为 Windows 10，允许直接采用 Windows 10 及以上能力，不增加旧系统兼容或回退路径；
- QUIC 基础由 KNSoft.Quic 提供；
- ZPigeon 自己负责连接状态、Frame、Protocol Dispatcher 和业务模块；
- 如果实现所需辅助函数具有独立的 common library 价值，应先由 Owner 决定是否抽到 KNSoft.MakeLifeEasier，再进行编码和依赖同步。

## 12. 第一版密码与握手规格

- Deployment 根密钥和 S 在线密钥均使用 ECDSA P-256；根证书与在线证书使用标准 DER X.509；
- C 内置 Deployment 根证书 DER，TLS 握手时建立到该根的专用证书链并严格验证 `ServerName`；不回退到系统公共根，也不忽略名称、有效期或签名错误；
- S 在线证书由 Deployment 根证书签发，EKU 必须允许 Server Authentication；轮换通过同时部署新证书并保持同一根完成；
- C 实例密钥为 CNG `ECDSA_P256` 持久化机器密钥；私钥不可导出；
- 客户端公钥在线上使用 SEC1 非压缩格式 `0x04 || X[32] || Y[32]`，该公钥本身作为实例身份；
- S 的 Challenge 使用系统 CSPRNG 生成 32 字节，每条连接只使用一次；
- 客户端签名摘要为 `SHA-256("KNSoft.ZPigeon.ClientAuth.v1" || 0x00 || Challenge[32] || PublicKey[65])`；
- `ClientAuthenticate` 使用 IEEE P1363 编码的 ECDSA P-256 签名，即 32 字节大端 `r` 后跟 32 字节大端 `s`；
- `ClientHello` 之后只接受 `ServerChallenge`，`ServerChallenge` 之后只接受 `ClientAuthenticate`，认证成功后 S 发送 `Ready`；任何越序、重复或握手阶段业务消息均以 `STATUS_PROTOCOL_UNREACHABLE` 关闭连接；
- QUIC 关闭、Frame 解析错误、身份验证失败和资源限制触发的断开均终止所有未完成请求、订阅和通道，不尝试在新连接上透明续接。

Client 未配置 `ClientKeyName` 时使用机器级持久化 CNG 密钥名 `KNSoft.ZPigeon.Client`。SDK 通过 Microsoft Software Key Storage Provider 打开或创建 `ECDSA_P256` 密钥，只导出 `BCRYPT_ECCPUBLIC_BLOB` 并转换为线上 SEC1 格式；私钥签名由 `NCryptSignHash` 在 Provider 内完成。Server 使用系统首选 CSPRNG 生成 Challenge，把 SEC1 公钥转换为 `BCRYPT_ECCPUBLIC_BLOB` 后通过 `BCryptVerifySignature` 验证 P1363 签名。签名验证成功前不会发送 `Ready` 或进入 Ready 阶段。

Client QUIC Transport 内部允许测试代码借用一个调用方持有的 `NCRYPT_KEY_HANDLE`，用于无持久化副作用的端到端测试；该入口不属于公开 ABI，SDK 不释放借用句柄，调用方必须保持它存活到 Client 完成关闭。正常产品路径始终使用上述机器级持久化密钥。

QUIC Stream 发送为每个 Frame 持有独立异步发送 Context：MsQuic 接受发送后立即推进 Connection 发送状态，Buffer 一直保留到 `SEND_COMPLETE`；接收回调按 MsQuic Buffer 顺序交给 `ZpConnection_Receive`，由 Connection 统一处理任意分片/合并和握手越序。Server 在 `ClientHello` 时只选择 ModuleId 与 ModuleVersion 均完全相同的模块；Client 对 `Ready` 再验证版本相等。模块没有独立能力位，行为变化直接提升模块版本。

### 12.1 仍按模块延后确定的规格

以下内容不阻塞 Network 和通用 Protocol 编码，在实现对应模块前定稿：

1. 除已固定的 System、Process、Service、File、Terminal、EventLog 和 Registry 操作外，其余业务模块的 `ModuleId`、`OperationId`、Payload 和版本演进；
2. 压力测试后仍需调整的两端资源限制默认值及全局配额。
