# KNSoft.ZPigeon 项目进度

更新时间：2026-08-07

总体设计基线见 `Design.md`；长期项目约束和协作约定见 `Memory.md`。本文只记录实施状态、下一步和阻塞项。

## 当前阶段

项目已完成第一版 Protocol/API 规格、通用 Protocol、Transport 无关的 Connection 核心、Client/Server SDK 公开 API 契约、配置对象所有权以及 Start/Stop 生命周期；QUIC Transport 的连接、监听、证书链、单 Stream、密码学握手和本地进程内端到端验证均已接通。System、Process 及 Service 的首批只读管理操作已接通。

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
- 实现 Process.Query：按 PID 返回父 PID、Session、线程/句柄数、创建与 CPU 时间、工作集、私有内存和映像名，并以 UnitTest 当前进程完成真实端到端验证；
- 固定 Service 模块 Version 1 的 `Enumerate` 操作和变长记录 Codec，实现 Server 通过 Service Control Manager 枚举服务类型、状态、宿主 PID、服务名与显示名，以及 Client `ZpClient_EnumerateServices` 异步 API；
- 实现 Service.Query：按服务名返回当前状态、宿主 PID、启动类型、错误控制、显示名、二进制路径和登录账户，并以枚举所得真实服务完成端到端验证；
- 增加统一 Server 请求授权门禁：回调可依据认证 ClientId、Read/Control 访问级别、模块、操作和原始 Payload 返回授权结果；未配置回调时只读默认放行、控制默认拒绝，真实 localhost 请求已验证携带非零认证 ClientId；
- 实现 Process.Terminate 控制操作：非零 PID 与退出码使用固定 Codec，Server 在 Control 授权通过后调用系统终止接口；集成测试验证未授权拒绝以及授权后只终止测试自身创建的临时进程；
- 实现 Service.Start/Stop 控制操作：复用服务名 Codec，Server 在 Control 授权通过后调用 Service Control Manager；集成测试对真实服务仅验证未授权拒绝，授权路径只访问确定不存在的测试服务名，不修改系统服务状态；
- 固定 File 模块 Version 1 和 Query 操作，实现路径 Codec、文件属性/大小/创建/访问/修改时间 Codec、Server 原生属性查询及 Client `ZpClient_QueryFile` 异步 API，并以 UnitTest 自身文件完成真实端到端验证；
- 实现 File.Enumerate：返回排除点目录项的名称、属性、大小和时间元数据，Client 提供 View 异步 API，并在真实输出目录中定位 UnitTest 自身文件完成端到端验证；
- 定稿通用 Channel 背压语义：新增 `ChannelWindow` 消息、零初始额度、接收方消费后补窗、单次 Close 终止及 Client 奇数/Server 偶数 ChannelId 规则，并实现 ChannelData/ChannelClose/ChannelWindow 类型化 Codec 与 Ready 状态门禁；
- 固定 File.OpenRead 为 OperationId 3：请求编码断点 Offset 与 Path，成功响应编码 Server 偶数 ChannelId、FileSize 和确认 Offset，并实现严格的请求/响应 Codec；
- 实现 Client 通用接收 Channel：OpenRead 成功响应建立引用计数 Channel Handle，Open 回调后自动授予 1 MiB 首窗，Data 回调返回后等量补窗，远端 Close、本地 Cancel 和连接终止均保证 Close 回调单次完成；
- 实现 Server File.OpenRead 发送 Channel：按窗口在线程池分块读盘和发送，额度耗尽时退出工作并由后续补窗重新调度，关闭/取消/连接终止与活动工作通过连接引用安全竞争；
- 扩展 localhost QUIC 集成测试，从 Offset 17 下载 UnitTest 自身文件，以流式字节数和 FNV 哈希验证多帧数据完整性、断点位置及成功 Close；同时处理本地 Close 与反向迟到 Window/Close 的合法交叉在途消息；
- 固定 Terminal 模块 Version 1：Create 编码窗口尺寸、命令行和可选工作目录并返回偶数 ChannelId/PID，Resize 编码 ChannelId 与新尺寸；同一 Channel 双向承载 VT 输出与输入并分别授信；
- 实现 Client Terminal API 与双向 Channel 发送额度：Create 成功后交付 Channel/PID 并自动授予输出首窗，远端 Window 通过 Writable 回调通知新增输入额度，`ZpChannel_Send` 无隐藏排队并在额度不足时返回 `STATUS_RETRY`，Resize 复用 Channel Handle 发起独立异步 Request；
- 实现 Server ConPTY Terminal：创建同步输入/输出管道并附加子进程，4 KiB 输入授信写入后等量补窗，输出工作按 Client Window 持续排空 VT 数据，支持 Resize、原始进程退出码 Close、Client 取消和连接终止回收；ConPTY 关闭由独立工作触发，输出线程继续排空最终 Frame，避免同步关闭与输出管道互锁；
- 扩展 localhost QUIC 集成测试，真实创建交互式 `cmd.exe` ConPTY 会话，验证 Terminal Create/PID、输入发送与额度补回、Resize、超过 100 KiB 的 VT 输出、异常退出码 7、长进程取消及 Channel 生命周期；
- 修正 Console 宿主创建 ConPTY 子进程时继承宿主标准句柄的问题：以 `STARTF_USESTDHANDLES` 和空标准句柄触发伪控制台初始化，确保输入输出均绑定 ConPTY；
- 实现 File.Hash：Version 1 固定 SHA-256 算法标识及请求/响应 Codec，Client 提供异步 Hash API，Server 以 64 KiB 分块计算并响应取消；localhost QUIC 集成测试将远端结果与本地独立 SHA-256 计算逐字节比对；
- 实现 File.OpenWrite 原子上传：Client 以 Server 窗口驱动有界发送且禁止超过声明 FileSize，Server 写入同目录随机临时文件，完整接收并刷新后按 CreateNew/CreateAlways 原子提交；真实 QUIC 测试覆盖 131,089 字节内容完整性、覆盖为零字节文件以及取消后目标和临时文件均无残留；
- 实现 File.EnumeratePage：保留旧 Enumerate 兼容接口，新增 1～4096 页大小、无状态文件名 Cursor、ordinal 排序及 NextCursor 校验；真实 QUIC 测试以页大小 1 连续翻页并验证游标严格推进；
- 定稿 EventLog Version 1：分页查询、严格 Bookmark Seek、实时订阅序号、显式 Terminal、队列溢出/日志过期检测，以及从最后持久化 Bookmark 补页后重新订阅的至少一次恢复语义；
- 实现 Core Event 与 EventLog Version 1 类型化 Codec：QueryPage、Subscribe/Unsubscribe、Record/Terminal、分页记录 View 和边界校验均接入 Protocol 静态库；
- 实现 Client EventLog API 与引用计数 Subscription Handle：打开阶段复用 Request，Record 严格校验连续 Sequence，Terminal/断线/取消单次完成，Unsubscribe 成功 Response 作为发送截止点，SDK 内部取消 Request 自动回收；
- 实现 Server EventLog.QueryPage：使用 Windows Event Log API 按 Channel/XPath 正向查询，以 `EvtSeekStrict` 从 Bookmark 后精确续页，渲染原生 Bookmark 与事件 XML，并按 Frame 容量安全截页；
- 扩展 localhost QUIC 集成测试，覆盖 System 日志首个事件分页和严格 Bookmark 续页；
- 实现 Server EventLog 实时订阅：以 Windows 拉取订阅和手动复位事件驱动 64 条有界批次，严格递增 Sequence、持续更新 Bookmark，并实现 Unsubscribe 响应截止点、错误 Terminal 与连接关闭清理；
- 扩展 localhost QUIC 集成测试，写入真实 Application 事件并验证 Record 内容、主动取消 Terminal，以及活动订阅随连接断开完成；
- 扩展 localhost QUIC EventLog 恢复测试：持久化实时 Record Bookmark，离线产生事件后以 QueryPage 补页，再从补页 Bookmark 严格重新订阅并接收后续事件；
- 扩展 localhost QUIC 集成测试，以调用方 Token 验证真实 Ping/Pong 往返；
- 扩展 localhost QUIC 集成测试，覆盖 Server 停止、Client 进入 RetryWait、Server 重启以及 Client 自动重连并再次完成认证；
- 创建 Protocol、Server SDK 和 UnitTest 工程，并建立 Client/Server 到 Protocol 的工程依赖；
- x86/x64 的 Debug/Release 全矩阵 Rebuild 通过且无编译或链接警告；每个配置下 307 项断言全部通过，其中包含 EventLog/Core Event Codec、Client Subscription 生命周期/乱序拒绝/取消、真实 File.EnumeratePage 连续翻页、File.OpenWrite 原子上传/取消清理、File.Hash SHA-256、ConPTY Terminal Create/输入/输出/Resize/退出/取消、真实 File.OpenRead 下载和既有管理操作端到端验证。

## 下一步

1. 复核 EventLog 高速生产下的批次调度与显式日志过期 Terminal；
2. 根据实际需求定稿下一业务模块的 Version 1 协议并实施。

## 待确认与阻塞

- 当前无外部阻塞。
- 后续业务模块专属协议按 `Design.md` 的“仍按模块延后确定的规格”在实现前定稿。
