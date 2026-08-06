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

Endpoint 至少需要表达：

- Transport 类型；
- 实际主机或 IP；
- 端口；
- 用于 TLS/SNI 验证的 `ServerName`；
- WSS 路径（仅 WSS）；
- 优先级或列表顺序。

C 按配置顺序尝试 Endpoint。连接超时、失败分类、重连退避和已连接后的 Transport 切换规则留待 Network 规格确定。

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

Frame 使用最小公共前缀：

```text
UINT32 PayloadLength
BYTE   MessageType
...    Type-specific fields
BYTE[] Payload
```

公共前缀不传输：

- Magic；
- HeaderSize；
- S 版本；
- 每帧协议版本；
- 无用途的保留字段；
- 对齐 Padding。

初始消息语义限制为：

- `Request`：发起一次操作并携带请求关联信息；
- `Response`：返回对应请求的 `NTSTATUS` 和结果；
- `Event`：传递已建立订阅的事件；
- `ChannelData`：传递文件或终端等长生命周期数据；
- `Ping`、`Pong`：连接存活检测。

RequestId、ChannelId、订阅标识和操作标识只放入确实需要它们的消息类型，不为所有 Frame 建立统一大型 Header。确切字段位宽、最大 Payload、长度是否包含类型专用字段以及握手控制消息的编码方式，必须在编码前由第一版协议规格确定。

### 7.2 版本兼容

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

公开 Handle、回调、取消、超时、Buffer 所有权和对象销毁契约尚未定稿，应在第一版 API 规格中一次写清，不能依赖调用方猜测。

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

大型结果不塞入单个 Response。文件和终端使用 `ChannelData` 承载连续数据；背压、窗口、断点续传和哈希规则在实现对应模块前按真实需求确定，不预先建设通用虚拟流框架。

## 10. 安全与资源限制

- 所有 Transport 必须验证 S 身份并使用 TLS 保护传输；
- Frame 解码必须检查长度、整数溢出、字段边界、消息类型和版本；
- 未匹配 RequestId、ChannelId 或订阅的数据直接丢弃，达到违规阈值时断开；
- 新连接按来源 IP、Deployment 和全局维度实施连接及握手速率限制；
- S 限制同时握手数量、自动登记记录数量以及单连接未完成请求和通道数量；
- 不把静态公钥、协议格式或客户端程序的不可见性当作安全边界；
- 不自制会话密码、逐包非对称加密、Nonce 或重放保护方案，使用成熟 TLS 实现提供这些能力。

确切限制值由 Server 配置和压力测试结果确定，不在协议中无依据地固化。

## 11. 依赖边界

- 优先使用 KNSoft.NDK 已提供的 NT 层定义和系统能力；
- 优先复用 KNSoft.MakeLifeEasier 的通用函数；
- QUIC 基础由 KNSoft.Quic 提供；
- ZPigeon 自己负责连接状态、Frame、Protocol Dispatcher 和业务模块；
- 如果实现所需辅助函数具有独立的 common library 价值，应先由 Owner 决定是否抽到 KNSoft.MakeLifeEasier，再进行编码和依赖同步。

## 12. 第一版规格待定项

以下问题必须在开始 Network 编码前定稿：

1. 各 MessageType 的确切字段、位宽、长度定义和数量上限；
2. Hello、证书验证、Challenge 签名、Ready 和断开消息的编码；
3. 密钥算法、证书/签名格式、密钥轮换及本地密钥存储契约；
4. 字符串、数组、可选字段和嵌套 Payload 的 Codec 规则；
5. Endpoint 配置、超时、重试、退避和切换规则；
6. Protocol、Client SDK、Server SDK 的公开头文件、Handle 和回调契约；
7. 接收 Buffer、解码 View、异步发送 Buffer 的所有权和生命周期；
8. Request 取消、Deadline、Ping/Pong 和异常断开语义；
9. ChannelData 在 File 和 Terminal 模块中的背压及关闭语义；
10. Transport/Win32 错误到 `NTSTATUS` 的映射规则。

这些待定项属于设计细化，不应在实现中通过临时字段、保留位、自动降级或猜测行为绕过。
