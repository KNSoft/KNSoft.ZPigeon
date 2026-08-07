# KNSoft.ZPigeon SDK 设计

状态：总体设计基线  
更新时间：2026-08-07

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
- 最低支持 Windows 10，第一版支持 x86 和 x64；
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
Client modules                         Server modules
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

## 3. 项目与代码组织

解决方案最终包含三个静态库项目：

```text
KNSoft.ZPigeon.Protocol
KNSoft.ZPigeon.Client.SDK
KNSoft.ZPigeon.Server.SDK
```

依赖关系：

```text
Client SDK ----> Protocol
Server SDK ----> Protocol
```

- Protocol 项目由 C/S 共用，且只使用纯 C；
- Client SDK 和 Server SDK 链接 Protocol，不重复编译同一份 Protocol 实现；
- Protocol 不负责连接、线程、密钥存储或具体 Transport。

源码按功能聚合，而不是按三端分别复制目录：

```text
Source/
|-- Protocol/
|   |-- Frame.c
|   `-- Codec.c
|-- Network/
|   |-- Common.c
|   |-- Client.c
|   |-- Server.c
|   `-- Transport/
|       |-- Quic.c
|       |-- TlsTcp.c
|       `-- WebSocket.c
`-- System/
    |-- Process/
    |   |-- Process.Protocol.h
    |   |-- Process.Protocol.c
    |   |-- Process.Client.c
    |   `-- Process.Server.c
    |-- Service/
    |-- Software/
    |-- EventLog/
    |-- Registry/
    |-- File/
    `-- Terminal/
```

各项目通过工程文件选择属于自己的源文件：Protocol 编译 `*.Protocol.c`，Client SDK 编译 `*.Client.c` 和 Client Network，Server SDK 编译 `*.Server.c` 和 Server Network。

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
- `ClientId = SHA-256(ClientPublicKey)`；
- CNG 密钥被删除、重置或因系统重装丢失后，该实例生成新的 ClientId；
- S 不自动把新的 ClientId 与历史记录合并；
- 未知 ClientId 完成客户端密钥签名握手后自动登记，可立即由 S 操作，并进入独立的“新连接”虚拟组。

当前模型只证明“后续连接仍持有同一客户端私钥”，不额外证明该实例经过入组审批。允许未知 ClientId 自动登记是已选择的第一版行为；如果以后需要受控入组，应另行增加一次性 Enrollment 凭据，不能把客户端自签名误当作部署授权。

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
    -> S 验证签名和 ClientId
    -> 登记或加载客户端记录
    -> Ready
```

握手只保留实际需要的信息：

- C 核心协议版本；
- C 各模块协议版本和能力；
- 客户端公钥，S 由其计算 ClientId；
- Challenge、签名以及必要的防重放数据。

Deployment 已由当前连接的 SNI 和证书上下文确定，握手中不重复传输 DeploymentKeyId。

C 不内置、不依赖 S 的协议版本。S 在线升级后根据 C 报告的版本选择兼容的 Decoder 和 Handler。

QUIC、TLS/TCP 和 WSS 均使用 TLS 派生的对称会话密钥保护业务数据。分组公私钥不用于逐包加解密，也不在 TLS 外增加应用层二次加密。

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
- `Disconnect` 在任意未关闭状态均合法，分派后进入关闭状态；同批接收数据中位于 `Disconnect` 之后的字节不再分派；
- 消息回调返回失败时当前连接立即进入关闭状态；具体 Transport 关闭和可选 `Disconnect` 发送由上层 Network 代码执行；
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
| `0x05` | `Disconnect` | 双向 | `INT32 Status`、UTF-16LE 原因字符串 |
| `0x10` | `Request` | 双向 | `UINT64 RequestId`、`UINT16 ModuleId`、`UINT16 OperationId`、`UINT32 TimeoutMilliseconds`、Payload |
| `0x11` | `Response` | 双向 | `UINT64 RequestId`、`INT32 Status`、Payload |
| `0x12` | `Cancel` | 双向 | `UINT64 RequestId` |
| `0x13` | `Event` | 双向 | `UINT64 SubscriptionId`、`UINT16 ModuleId`、`UINT16 EventId`、Payload |
| `0x14` | `ChannelData` | 双向 | `UINT64 ChannelId`、非空数据 |
| `0x15` | `ChannelClose` | 双向 | `UINT64 ChannelId`、`INT32 Status` |
| `0x16` | `Ping` | 双向 | `UINT64 Token` |
| `0x17` | `Pong` | 双向 | `UINT64 Token` |
| `0x18` | `ChannelWindow` | 双向 | `UINT64 ChannelId`、非零 `UINT32 CreditBytes` |

其他值在 Core Version 1 中非法。消息类型的最小 Body 长度由 Protocol 解码器校验。`ChannelData` 单帧数据最大为 1 MiB，单次 `ChannelWindow` Credit 最大为 16 MiB，避免大块传输长期占用连接发送队列；其他消息仍受 16 MiB Frame 上限约束。

模块记录编码为：

```text
UINT16 ModuleId
UINT16 ModuleVersion
UINT32 Capabilities
```

- `ModuleId` 和 `ModuleVersion` 均不得为 0；
- 单个 Hello/Ready 最多 64 条模块记录；
- 记录按 `ModuleId` 严格升序排列，不允许重复；
- S 选择双方均支持的模块版本和能力，将结果放入 `Ready`；未出现在 `Ready` 的模块在当前连接不可用。

初始业务消息语义限制为：

- `Request`：发起一次操作并携带请求关联信息；
- `Response`：返回对应请求的 `NTSTATUS` 和结果；
- `Event`：传递已建立订阅的事件；
- `ChannelData`：传递文件或终端等长生命周期数据；
- `ChannelWindow`：由接收方增加指定 Channel 的可发送字节额度；
- `Ping`、`Pong`：连接存活检测。

`RequestId`、`ChannelId` 和 `SubscriptionId` 均为连接内非零 `UINT64`，由创建它的一方分配，在对应对象结束前不得复用。`ModuleId`、`OperationId` 和 `EventId` 为非零 `UINT16`。未匹配的标识视为协议违规。

`TimeoutMilliseconds` 是接收方从完整收到 Request 起计算的处理预算；0 表示协议层不额外施加超时。发送方 SDK 仍维护本地 Deadline：Deadline 到期后在本地以 `STATUS_IO_TIMEOUT` 完成操作，尽力发送 `Cancel`，并忽略迟到的 Response。显式取消在本地以 `STATUS_CANCELLED` 完成，`Cancel` 不要求单独响应。

Channel 使用接收方授信的字节窗口提供背压：新 Channel 建立后发送额度为 0，接收方发送 `ChannelWindow` 后发送方才能发送不超过累计剩余额度的 `ChannelData`；每次授信非零且不超过 16 MiB，剩余额度不得溢出 64 位计数。SDK 把 Data 回调返回视为对应 Buffer 已消费，并自动补回等量窗口。任一方发送一次 `ChannelClose` 即终止 Channel，Status 为终止结果且不回送第二个 Close。Client 创建的 ChannelId 使用奇数，Server 创建的 ChannelId 使用偶数，连接内单调分配且永不复用；因此本地关闭后收到已分配旧 ID 的迟到 Window 或 Close 时幂等忽略，以容纳双向消息交叉在途，未来 ID、错误奇偶 ID、未知 Data 或额度违规仍视为协议违规。

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

### 7.3 版本兼容

- 最外层 Frame 格式保持稳定；
- C 报告核心协议版本和各模块版本；
- 模块 Payload 使用确定版本的固定 Codec；
- 结构变化通过模块版本演进；
- S 保留仍受支持的旧版 Decoder 和 Handler；
- S 可拒绝已停止支持的过旧 C，最低支持版本策略另行制定。

## 8. SDK API 与执行模型

已确定的 API 原则：

- 纯 C ABI；
- 使用项目既有 SAL 注解约定；
- 网络和远程操作以异步接口为核心；
- 本地和远程操作结果以 `NTSTATUS` 为主；
- 不采用一连接一线程；
- MsQuic 使用回调模型，其他 Windows Transport 使用适合的异步 I/O 模型；
- 热路径避免无依据的堆分配、内存复制、编码转换、锁和间接调用；
- 回调中不执行可能长期阻塞网络推进的业务操作。

第一版公开对象采用不透明指针 Handle：`ZP_CLIENT_HANDLE`、`ZP_SERVER_HANDLE`、`ZP_CONNECTION_HANDLE`、`ZP_REQUEST_HANDLE` 和 `ZP_CHANNEL_HANDLE`。对象由创建它的 SDK 分配，调用方只能通过对应 API 操作。

生命周期契约：

- `Create` 成功后返回初始停止状态对象；`Start` 启动异步工作；`Stop` 可重复调用并异步终止连接；
- `Close` 只接受已停止且不存在未完成回调的对象，否则返回 `STATUS_DEVICE_BUSY`；不隐式阻塞等待；
- 回调可能来自任意 SDK 工作线程，同一连接的状态与消息回调保持顺序，但不同连接可并发；
- SDK 在调用回调期间持有 Handle 的有效引用，调用方不得在回调栈内关闭当前对象；
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

公共 `SDK.h` 定义五种互不兼容的不透明 Handle：`ZP_CLIENT_HANDLE`、`ZP_SERVER_HANDLE`、`ZP_CONNECTION_HANDLE`、`ZP_REQUEST_HANDLE` 和 `ZP_CHANNEL_HANDLE`。Client 与 Server 公开头分别只暴露本端生命周期 API；具体对象布局和 Connection 握手状态不属于公开 ABI。

Client 配置包含：

- `Size`，第一版必须为 `sizeof(ZP_CLIENT_CONFIG)`；
- 按尝试顺序排列的 Endpoint 数组；
- Deployment 根证书 DER；
- 可选的 CNG 客户端持久化密钥名；为空时由 SDK 使用默认名称；
- 严格按 `ModuleId` 升序排列的模块版本与能力；
- 连接超时，0 使用 10 秒默认值；
- 状态回调、可选的 Pong 回调和调用方 Context。

Server 配置包含：

- `Size`，第一版必须为 `sizeof(ZP_SERVER_CONFIG)`；
- Listener 数组；Listener 的 `Host` 为空表示通配绑定，只有 WSS 使用非空 Path；
- Deployment 数组，每项由 `ServerName` 和带可用私钥的 Windows `PCCERT_CONTEXT` 组成；SDK 在 `Create` 中复制证书 Context，调用方随后可释放原引用；
- 严格按 `ModuleId` 升序排列的模块版本与能力；
- 单连接未完成 Request 上限，0 使用 64 默认值；
- Server 生命周期回调、单连接阶段回调、可选的请求授权回调和调用方 Context。

Client 状态为 `Stopped`、`Connecting`、`Authenticating`、`Ready`、`RetryWait` 和 `Stopping`；Server 状态为 `Stopped`、`Starting`、`Running` 和 `Stopping`；Server 单连接以及需要统一表达的连接阶段使用 `Connecting`、`Authenticating`、`Ready` 和 `Closed`。状态回调中的 `NTSTATUS` 表示触发当前转换的结果，成功转换使用 `STATUS_SUCCESS`。

`Create` 验证并复制数组、字符串、证书和模块配置，成功后对象处于 `Stopped`；回调函数指针和 Context 只保存值。`Start`、`Stop` 和 `Close` 遵循本节前述异步生命周期契约。第一版不公开内部 Connection 结构，也不允许调用方直接驱动 Frame 状态机。

SDK 对象通过内部 `ZP_TRANSPORT_OPERATIONS` 操作表持有具体 Transport 的 `Start`、`Stop` 与 Context，公开生命周期不依赖 MsQuic 或后续 Transport 的对象布局。状态修改在对象锁内串行化，状态回调在锁外调用；进入回调前增加活动回调计数，返回后减少，因此即使状态已变为 `Stopped`，回调栈内或并发的 `Close` 仍返回 `STATUS_DEVICE_BUSY`，不会释放正在被回调使用的对象。

Client `Start` 只允许 `Stopped -> Connecting`，Server `Start` 只允许 `Stopped -> Starting`；缺少 Endpoint、Listener 或 Deployment 返回 `STATUS_INVALID_PARAMETER`，尚未安装对应 Transport 适配返回 `STATUS_NOT_SUPPORTED`。Transport 启动失败时立即回到 `Stopped`，状态回调携带该失败状态。Client 后续只接受 `Connecting -> Authenticating/RetryWait/Stopped`、`Authenticating -> Ready/RetryWait/Stopped`、`Ready -> RetryWait/Stopped`、`RetryWait -> Connecting/Stopped` 和 `Stopping -> Stopped`；Server 只接受 `Starting -> Running/Stopped`、`Running -> Stopped` 和 `Stopping -> Stopped`。

`Stop` 对 `Stopped` 和 `Stopping` 幂等；其他状态先同步进入 `Stopping` 并通知回调，再请求 Transport 异步停止。Transport 完成资源回收后通过受控内部通知进入 `Stopped`。Transport 操作表和状态通知均为 SDK 内部契约，不属于公开 ABI。

Client 在 `Ready` 状态可通过 `ZpClient_Ping` 发送调用方 Token；Server 协议层自动回送等值 Pong，Client 通过可选 Pong 回调交付 Token。该路径用于连接存活检测，不创建 Request 对象。

Client 通过 `ZpClient_SendRequest` 创建引用计数 Request Handle；同步拒绝不会触发完成回调，成功提交后 Response、显式取消或连接终止恰好完成一次。调用方可通过 `ZpRequest_Cancel` 尽力发送 Cancel，并在不再使用句柄时调用 `ZpRequest_Close` 释放调用方引用。

`ZpClient_OpenFileRead` 仍以 Request Handle 表示异步打开阶段；成功 Open 回调交付独立的 Channel Handle、FileSize 和确认 Offset。SDK 在 Open 回调返回后自动授予 1 MiB 初始窗口，每次 Data 回调返回后自动补回等量额度。Data Buffer 仅在回调期间有效；远端 Close、本地取消或连接终止恰好触发一次 Channel Close 回调。调用方可通过 `ZpChannel_Cancel` 尽力发送 `STATUS_CANCELLED` 的 ChannelClose，并在不再使用句柄时调用 `ZpChannel_Close` 释放调用方引用。

Client 的非零 `TimeoutMilliseconds` 同时建立基于 `GetTickCount64` 的本地 Deadline；对象级线程池定时器始终只等待最近截止项，到期请求以 `STATUS_IO_TIMEOUT` 完成并尽力发送 Cancel，Response、取消和计时器竞争由同一请求表锁串行化。

Server 完整收到 Request 后复制 Payload 并投递线程池，MsQuic 接收回调不执行系统查询等业务工作；每条连接维护活动 Request 表和引用计数。Cancel、连接关闭与工作完成通过请求表锁竞争一次终止，连接对象延迟到所有工作退出后释放，Server Stop 也等待这些连接引用归零。

Server 配置的 `MaxRequestsPerConnection` 限制每连接已投递且尚未完成的 Request；0 使用默认值 64，第一版配置硬上限为 4096。达到上限的新请求返回 `STATUS_QUOTA_EXCEEDED`，不进入工作队列且不终止连接。

Server 在线程池执行具体业务操作前统一经过授权门禁。授权回调接收已认证连接的 32 字节 ClientId、`Read` 或 `Control` 访问级别、ModuleId、OperationId 以及只在回调期间有效的原始 Payload View，并以 `NTSTATUS` 决定是否继续；失败状态原样作为 Response 返回。未配置回调时只读操作默认放行，控制类操作默认返回 `STATUS_ACCESS_DENIED`。授权回调在对象锁外执行并计入活动回调，不能在回调栈内关闭 Server。

Client Endpoint、Server Listener 和 Server Deployment 数组第一版各最多 64 项；Deployment 根证书 DER 最大 1 MiB。非空数组与源指针必须成对提供；可选字符串使用 `NULL` 表示缺省，提供空字符串视为无效配置。ServerName 在同一 Server 配置中按不区分大小写方式保持唯一。所有深拷贝使用单块对象内存，Server 额外持有通过 `CertDuplicateCertificateContext` 获得的证书引用，并在 `Close` 时逐项释放。

## 9. 功能模块

第一版模块范围：

- `System.Info`：系统基本信息和连通性验证；
- `Process`：枚举、查询和控制进程；
- `Service`：枚举、查询、启动、停止和修改配置；
- `Software`：已安装软件清单，安装/卸载在需要时增加；
- `EventLog`：分页查询、Bookmark 和实时订阅；
- `Registry`：显式 Root、WOW64 View 和 Value Type；
- `File`：枚举、属性及文件传输等管理操作；
- `Terminal`：基于 ConPTY 的双向终端，包含窗口尺寸、输入、输出和退出状态。

`System` 模块第一版固定为 `ModuleId = 1`、`ModuleVersion = 1`；`Info` 固定为 `OperationId = 1`，请求 Payload 必须为空。成功响应 Payload 依次编码 `UINT16 Architecture`（1=x86、2=x64、3=ARM64）、三个 `UINT32` Windows 主/次/Build 版本、`UINT32 ProcessorCount`、`UINT64 PhysicalMemoryBytes` 和 UTF-16LE ComputerName 字符串。

`Process` 模块第一版固定为 `ModuleId = 2`、`ModuleVersion = 1`；`Enumerate` 固定为 `OperationId = 1`，请求 Payload 必须为空。成功响应 Payload 为数组，单项依次编码 `UINT32 ProcessId`、`UINT32 SessionId` 和 UTF-16LE ImageName 字符串；PID 0 和空 ImageName 均为合法系统记录。

`Process.Query` 固定为 `OperationId = 2`，请求 Payload 为 `UINT32 ProcessId`。成功响应依次编码 `UINT32 ProcessId`、父 ProcessId、SessionId、ThreadCount、HandleCount，随后为五个 `UINT64`：CreateTime、UserTime、KernelTime、WorkingSetBytes、PrivateBytes，最后为 UTF-16LE ImageName；时间值沿用 Windows 100ns 原生计数。

`Process.Terminate` 固定为 `OperationId = 3` 且访问级别为 `Control`，请求 Payload 依次为非零 `UINT32 ProcessId` 和 `UINT32 ExitCode`，成功响应 Payload 为空。Server 只有在授权门禁放行后才打开目标进程并调用系统终止接口；未配置授权回调时固定返回 `STATUS_ACCESS_DENIED`。

`Service` 模块第一版固定为 `ModuleId = 3`、`ModuleVersion = 1`；`Enumerate` 固定为 `OperationId = 1`，请求 Payload 必须为空。成功响应 Payload 为数组，单项依次编码 `UINT32 ServiceType`、`CurrentState`、`ProcessId`，随后为 UTF-16LE ServiceName 和 DisplayName 字符串。字段值沿用 Windows Service Control Manager 定义；停止中的服务可返回 PID 0。

`Service.Query` 固定为 `OperationId = 2`，请求 Payload 为非空 UTF-16LE ServiceName 字符串。成功响应依次编码 `UINT32 ServiceType`、`CurrentState`、`ProcessId`、`StartType`、`ErrorControl`，随后为 UTF-16LE ServiceName、DisplayName、BinaryPathName 和 StartName 字符串；配置和状态字段沿用 Windows Service Control Manager 定义。

`Service.Start` 和 `Service.Stop` 分别固定为 `OperationId = 3` 和 `4`，访问级别均为 `Control`，请求 Payload 复用非空 UTF-16LE ServiceName 字符串，成功响应 Payload 为空。Server 只有在授权门禁放行后才打开 Service Control Manager 句柄并执行启动或停止控制；未配置授权回调时固定返回 `STATUS_ACCESS_DENIED`。

`File` 模块第一版固定为 `ModuleId = 4`、`ModuleVersion = 1`；`Enumerate` 固定为 `OperationId = 1`，`Query` 固定为 `OperationId = 2`，`OpenRead` 固定为 `OperationId = 3`，`Hash` 固定为 `OperationId = 4`。Enumerate 与 Query 请求 Payload 均为非空 UTF-16LE Path 字符串。Query 成功响应依次编码 `UINT32 Attributes` 以及四个 `UINT64`：Size、CreationTime、LastAccessTime、LastWriteTime。Enumerate 成功响应为数组，单项编码同一组元数据后追加 UTF-16LE Name 字符串，并排除 `.` 与 `..`；结果超过单 Frame 上限时返回 `STATUS_BUFFER_OVERFLOW`，后续按真实规模需求增加分页。OpenRead 请求依次编码 `UINT64 Offset` 和非空 UTF-16LE Path；成功响应依次编码 Server 创建的偶数 `UINT64 ChannelId`、`UINT64 FileSize` 和服务端确认的 `UINT64 Offset`，Offset 大于 FileSize 时请求失败，等于 FileSize 时建立后正常空流结束。Client 在成功响应回调返回后授予首个接收窗口，Server 才能发送文件数据。Hash 请求依次编码 `UINT16 Algorithm` 和非空 UTF-16LE Path，Version 1 仅定义 `Algorithm = 1` 的 SHA-256；成功响应编码相同 Algorithm、`UINT64 FileSize` 和固定 32 字节 Digest。Server 分块读取文件并在请求取消后停止计算，Digest 表示本次打开并顺序读取到的完整字节流。属性值和 100ns 时间值沿用 Windows 文件系统定义。File 请求属于 `Read`，Server 授权回调可依据原始 Payload 收窄可访问范围。

`Terminal` 模块第一版固定为 `ModuleId = 5`、`ModuleVersion = 1`；`Create` 固定为 `OperationId = 1`，请求依次编码非零 `UINT16 Columns`、`UINT16 Rows`、非空 UTF-16LE CommandLine 和可空 UTF-16LE WorkingDirectory，成功响应编码 Server 创建的偶数 `UINT64 ChannelId` 与非零 `UINT32 ProcessId`。同一 Channel 上 Server 到 Client 的 Data 是 ConPTY VT 输出，Client 到 Server 的 Data 是输入字节，两个方向分别由对端 Window 授信。`Resize` 固定为 `OperationId = 2`，请求编码 ChannelId、非零 Columns 和 Rows，成功响应为空。Client 或 Server 发送 ChannelClose 终止会话；正常进程退出的 Close Status 使用进程退出码的原始 32 位值，基础设施错误使用失败 `NTSTATUS`。Terminal 操作属于 `Control` 权限。

Client 以 `ZpClient_CreateTerminal` 异步建立终端并在成功回调中交付 Channel Handle 和 ProcessId；输出 Buffer 只在 Data 回调期间有效，回调返回后 SDK 自动补回接收窗口。Server 通过 `ChannelWindow` 增加 Client 输入发送额度，SDK 以 Writable 回调通知本次新增额度；`ZpChannel_Send` 不做隐藏排队，额度不足返回 `STATUS_RETRY`，成功发送后立即扣减额度。`ZpClient_ResizeTerminal` 复用 Channel Handle 定位会话并返回独立 Request Handle。

Server 使用 ConPTY 建立同步输入/输出管道；传给 `CreatePseudoConsole` 的 Input Read 与 Output Write 端必须保持到附加终端属性的子进程创建成功后再关闭。输出在长生命周期线程池工作中持续排空并受 Client Window 限制；输入首窗固定为 4 KiB，Server 将获准数据写入 ConPTY 后等量补窗，限制同步写入对网络回调的占用。进程退出后由独立线程池回调关闭 ConPTY，输出工作继续排空最终 VT Frame，随后以原始进程退出码发送 ChannelClose；连接终止或本地关闭会终止仍存活的终端进程并回收全部句柄。

大型结果不塞入单个 Response。文件和终端使用 `ChannelData` 承载连续数据；背压、窗口和断点续传按上述通用 Channel 与 File.OpenRead 规则处理，不预先建设通用虚拟流框架。

## 10. 安全与资源限制

- 所有 Transport 必须验证 S 身份并使用 TLS 保护传输；
- Frame 解码必须检查长度、整数溢出、字段边界、消息类型和版本；
- 未匹配 RequestId、ChannelId 或订阅的数据直接丢弃，达到违规阈值时断开；
- 新连接按来源 IP、Deployment 和全局维度实施连接及握手速率限制；
- S 限制同时握手数量、自动登记记录数量以及单连接未完成请求和通道数量；
- 所有控制类操作必须标记为 `Control` 并通过 Server 授权门禁；不得因已完成身份认证而隐式放行；
- 不把静态公钥、协议格式或客户端程序的不可见性当作安全边界；
- 不自制会话密码、逐包非对称加密、Nonce 或重放保护方案，使用成熟 TLS 实现提供这些能力。

确切限制值由 Server 配置和压力测试结果确定，不在协议中无依据地固化。

## 11. 依赖边界

- 优先使用 KNSoft.NDK 已提供的 NT 层定义和系统能力；
- 优先复用 KNSoft.MakeLifeEasier 的通用函数；
- QUIC 基础由 KNSoft.Quic 提供；
- ZPigeon 自己负责连接状态、Frame、Protocol Dispatcher 和业务模块；
- 如果实现所需辅助函数具有独立的 common library 价值，应先由 Owner 决定是否抽到 KNSoft.MakeLifeEasier，再进行编码和依赖同步。

## 12. 第一版密码与握手规格

- Deployment 根密钥和 S 在线密钥均使用 ECDSA P-256；根证书与在线证书使用标准 DER X.509；
- C 内置 Deployment 根证书 DER，TLS 握手时建立到该根的专用证书链并严格验证 `ServerName`；不回退到系统公共根，也不忽略名称、有效期或签名错误；
- S 在线证书由 Deployment 根证书签发，EKU 必须允许 Server Authentication；轮换通过同时部署新证书并保持同一根完成；
- C 实例密钥为 CNG `ECDSA_P256` 持久化机器密钥；私钥不可导出；
- 客户端公钥在线上使用 SEC1 非压缩格式 `0x04 || X[32] || Y[32]`；`ClientId = SHA-256(PublicKey[65])`；
- S 的 Challenge 使用系统 CSPRNG 生成 32 字节，每条连接只使用一次；
- 客户端签名摘要为 `SHA-256("KNSoft.ZPigeon.ClientAuth.v1" || 0x00 || Challenge[32] || PublicKey[65])`；
- `ClientAuthenticate` 使用 IEEE P1363 编码的 ECDSA P-256 签名，即 32 字节大端 `r` 后跟 32 字节大端 `s`；
- `ClientHello` 之后只接受 `ServerChallenge` 或 `Disconnect`，`ServerChallenge` 之后只接受 `ClientAuthenticate` 或 `Disconnect`，认证成功后 S 发送 `Ready`；任何越序、重复或握手阶段业务消息均以 `STATUS_PROTOCOL_UNREACHABLE` 断开；
- TLS 关闭、Frame 解析错误、身份验证失败和资源限制触发的断开均终止所有未完成请求、订阅和通道，不尝试在新连接上透明续接。

Client 未配置 `ClientKeyName` 时使用机器级持久化 CNG 密钥名 `KNSoft.ZPigeon.Client`。SDK 通过 Microsoft Software Key Storage Provider 打开或创建 `ECDSA_P256` 密钥，只导出 `BCRYPT_ECCPUBLIC_BLOB` 并转换为线上 SEC1 格式；私钥签名由 `NCryptSignHash` 在 Provider 内完成。Server 使用系统首选 CSPRNG 生成 Challenge，把 SEC1 公钥转换为 `BCRYPT_ECCPUBLIC_BLOB` 后通过 `BCryptVerifySignature` 验证 P1363 签名，并计算 ClientId。签名验证成功前不会发送 `Ready` 或进入 Ready 阶段。

Client QUIC Transport 内部允许测试代码借用一个调用方持有的 `NCRYPT_KEY_HANDLE`，用于无持久化副作用的端到端测试；该入口不属于公开 ABI，SDK 不释放借用句柄，调用方必须保持它存活到 Client 完成关闭。正常产品路径始终使用上述机器级持久化密钥。

QUIC Stream 发送为每个 Frame 持有独立异步发送 Context：MsQuic 接受发送后立即推进 Connection 发送状态，Buffer 一直保留到 `SEND_COMPLETE`；接收回调按 MsQuic Buffer 顺序交给 `ZpConnection_Receive`，由 Connection 统一处理任意分片/合并和握手越序。Server 在 `ClientHello` 时按 ModuleId 取交集，版本取双方上限的较小值，Capabilities 取按位交集；Client 对 `Ready` 再验证所有选择均是其声明能力的子集。

### 12.1 仍按模块延后确定的规格

以下内容不阻塞 Network 和通用 Protocol 编码，在实现对应模块前定稿：

1. 除已固定的 System、Process、Service、File 和 Terminal 操作外，其余业务模块的 `ModuleId`、`OperationId`、Payload 和版本演进；
2. File 通道的哈希、上传、目录分页和落盘契约；
3. EventLog 等订阅模块的事件丢失与恢复语义；
4. 压力测试后确定的 Server 资源限制默认值。
