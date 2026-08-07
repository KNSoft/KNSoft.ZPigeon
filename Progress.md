# KNSoft.ZPigeon 项目进度

更新时间：2026-08-07

总体设计基线见 `Design.md`；长期项目约束和协作约定见 `Memory.md`。本文只记录实施状态、下一步和阻塞项。

## 当前阶段

项目已完成第一版 Protocol/API 规格、通用 Protocol、Transport 无关的 Connection 核心、Client/Server SDK 公开 API 契约、配置对象所有权以及 Start/Stop 生命周期；QUIC Transport 的连接、监听、证书链、单 Stream、密码学握手和本地进程内端到端验证均已接通。系统管理模块尚未开始。

当前解决方案包含：

- `KNSoft.ZPigeon.Protocol` 静态库；
- `KNSoft.ZPigeon.Client.SDK` 静态库骨架；
- `KNSoft.ZPigeon.Server.SDK` 静态库骨架；
- `UnitTest` 测试项目。

## 已完成

- 明确产品边界与纯 C、Windows 10 及以上、x86/x64 的实施基线；
- 完成 C/S、项目依赖、Transport、身份、连接加密、Protocol 和功能目录的总体设计；
- 将总体设计整理到独立的 `Design.md`；
- 明确第一版应保持薄 Network 模型，不预先实现复杂 RPC、权限或虚拟流框架；
- 定稿 Core Version 1 的 Frame、MessageType、Codec、握手密码学、Endpoint、重连、取消、Deadline 和 Buffer 生命周期；
- 实现不分配内存的 `ZpCodec_*` 小端读写和 View 解码；
- 实现 `ZpFrame_*` 长度计算、编码、流式解码及类型专用 Body 边界校验；
- 实现 ClientHello、ServerChallenge、ClientAuthenticate、Ready 和 Disconnect 类型化 Codec；
- 实现 Client/Server 握手发送与接收状态机，拒绝越序、重复及握手阶段业务消息；
- 实现任意 Transport 分片与合并交付下的 Frame 切分，完整 Frame 走零中间复制路径，分片 Buffer 按实际接收量渐进增长；
- 定义公共 Transport、Endpoint、Listener、五类不透明 Handle，以及 Client/Server 配置、生命周期状态和回调 API；
- 实现 Client/Server `Create` 与 `Close`，包括配置校验、单块内存深拷贝、默认值、关闭状态门禁和 Server 证书 Context 引用管理；
- 实现 Client/Server `Start`、`Stop`、受控状态转换和 Transport 操作表，状态回调在对象锁外执行并由活动回调计数保护 Handle 生命周期；
- 实现基于 KNSoft.Quic/MsQuic 的 Client 连接与 Server 监听骨架，包括独立 Host 解析、SNI、`knsoft-zpigeon/1` ALPN、Client 专属根证书链验证、Server 按 SNI 选证书以及一条双向 Stream 限制；
- 将 QUIC Stream 收发接入 Connection 状态机，实现 Client 持久化 ECDSA P-256 身份、ClientHello、系统随机 Challenge、P1363 签名与验证、ClientId 计算、模块协商和 Ready 双端校验；
- 实现本地进程内 QUIC 端到端测试，以临时自签名证书和借用的进程内 P-256 身份密钥验证 TLS 专属根链、SNI、单 Stream、完整认证握手、模块交集以及双端异步关闭；
- 修正 Schannel `INPROC_PEER_CERTIFICATE` 与原生 `PCCERT_CONTEXT` 混用导致的访问冲突，证书验证回调现在严格接收原生 Windows 证书 Context；
- Client 不再只识别配置首项：QUIC Endpoint 可位于混合列表任意位置，同步建连准备失败时会继续尝试后续 QUIC Endpoint；
- 固化重连退避计算：失败轮次按 1、2、4、8、16、32、60 秒封顶，并在每轮等待上应用正负 20% 的确定边界抖动；
- 将异步连接失败和异常断线接入 Endpoint 轮次推进及线程池定时器：同轮继续后续 QUIC Endpoint，整轮失败后按策略退避，Ready 连接断开后从首项重新开始，稳定 60 秒后重置失败轮次；
- 将 Endpoint 调度、异步重试和稳定连接重置从 QUIC 私有实现上移到 Client 通用层；各 Transport 按类型注册，所有已注册 Transport 的 Endpoint 严格按配置顺序参与同一轮尝试；
- 扩展 SDK 契约测试，覆盖 TLS 同步失败后继续 QUIC Endpoint、活动 Transport 选择、停止路由以及同步失败进入通用 RetryWait；
- 实现 Request、Response、Cancel、Ping 和 Pong 的类型化 Codec，以及 Client 通用 Transport 发送入口、`ZpClient_Ping`、Server 自动 Pong 和 Client Pong 回调；
- 实现 Client 通用异步 Request Handle：请求关联、Response 单次完成、显式取消、调用方引用释放以及断线批量完成；
- 实现 Client 单调时钟本地 Deadline：以对象级线程池定时器统一调度未完成请求，超时本地完成为 `STATUS_IO_TIMEOUT` 并尽力发送 Cancel；
- 将 Server Request 业务处理移出 MsQuic 回调并投递线程池；连接引用计数覆盖工作生命周期，Cancel/关闭会从活动表摘除请求并抑制迟到 Response，Server Stop 等待工作安全退出；
- 扩展集成压力路径，覆盖 8 个并发 Process 请求进行中停止 Server、全部 Client 请求单次完成以及随后自动重连；
- 增加 Server 每连接未完成 Request 配额：默认 64、配置硬上限 4096；超额请求返回 `STATUS_QUOTA_EXCEEDED`，集成压力路径以配额 4 验证 8 个并发请求；
- 固定 System 模块 Version 1 的 `Info` 操作和 Payload Codec，实现 Server 原生架构、Windows 版本、处理器数、物理内存及计算机名采集，以及 Client `ZpClient_GetSystemInfo` 异步 API；
- 扩展 localhost QUIC 集成测试，覆盖 System.Info 的真实 Request/Response 往返和结果解码；
- 固定 Process 模块 Version 1 的 `Enumerate` 操作和变长记录 Codec，实现 Server 原生进程快照、Client `ZpClient_EnumerateProcesses` 异步 API，以及当前进程可见性的真实端到端验证；
- 扩展 localhost QUIC 集成测试，以调用方 Token 验证真实 Ping/Pong 往返；
- 扩展 localhost QUIC 集成测试，覆盖 Server 停止、Client 进入 RetryWait、Server 重启以及 Client 自动重连并再次完成认证；
- 创建 Protocol、Server SDK 和 UnitTest 工程，并建立 Client/Server 到 Protocol 的工程依赖；
- x86/x64 的 Debug/Release 全矩阵 Rebuild 通过且无编译或链接警告；每个配置下 203 项断言全部通过，其中包含真实 localhost QUIC 认证、重连、Ping/Pong、System.Info 和 Process.Enumerate 端到端链路。

## 下一步

1. 增加 Process.Query 所需的单进程详细信息协议与实现；
2. 再按 Process.Control、Service、File、Terminal 等模块逐步实现。

## 待确认与阻塞

- 当前无外部阻塞。
- File、Terminal、EventLog 等模块专属协议按 `Design.md` 的“仍按模块延后确定的规格”在实现前定稿。
