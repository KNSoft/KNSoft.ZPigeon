# KNSoft.ZPigeon 实施进度

更新时间：2026-08-10

## 当前阶段

首版错误的 C/S 方向已经完成纠正。Server 是唯一管理控制端，Client 是被控端执行器；所有现有业务模块均沿 `ZpServer_* -> Request -> Client Execute -> Response/Channel` 路径运行。当前优先形成可直接试用的本地闭环。

## 本轮已完成

- 将 System、Process、Service、Registry、File、Terminal、EventLog 的 Windows 原生操作全部放入 `Modules/<Name>/Client.c`；Server 控制入口和回调解码位于对应 `Server.c`，模块 Codec 位于 `Protocol.c`。
- 删除 Client 主动管理 Request/API、Server 本机业务执行器、旧 EventLog/Terminal QUIC 私有实现、Read/Control 授权层和派生 ClientId。
- 将 Protocol Core、Client Core、Server Core、模块、SDK Handle 与 Transport 分离；Client/Server 私有 `Network` 目录改名为 `Transport`，根部只保留唯一共享 `Network`。
- Request 只允许 Server 创建；Channel 只允许 Client 创建。所有 ID 改为普通非零单调序列，不再按奇偶区分方向或预留未来路径。
- 增加 Client 入站 Request 数量/Payload 配额及两端 Channel 配额；Server 对资源创建先预留名额，同步超额不向 Client 发请求。
- 保留 Channel 的零初始额度和显式 Window 背压；已结束/已拒绝 ID 的迟到 Window/Close 幂等处理，不增加 tombstone 表。
- RequestId 改用连接内单调水位做 O(1) 重放校验；重复/倒序 Request 被拒绝，取消或超时后的迟到 Response/Cancel 幂等忽略。
- 修正连接正常关闭时未完成业务对象被伪报成功的问题；未完成对象统一以 `STATUS_CONNECTION_DISCONNECTED` 完成。
- 模块协商改为版本完全相等才选择，删除“取较小版本”的隐式兼容行为。
- 删除从未被模块读取的 Capabilities 字段，以及没有任何产品发送路径的 Disconnect 死协议。
- 删除未读取的连接 Status、QUIC Listener Index、Connection Role 存储、状态只读包装及内部未使用类型别名。
- Registry 使用 NT Registry 路径，实现默认值、命名值和空名称游标分页；修复空名称传入 ordinal 比较函数后被误判相等的问题。
- File 保持有界快照、SHA-256、断点下载和同目录临时文件原子上传。
- Terminal 在 Client 使用 ConPTY，支持双向窗口、Resize、进程退出码、Cancel 和最终输出排空；补齐 `STARTF_USESTDHANDLES`，避免子进程错误绑定宿主标准句柄。
- EventLog 保留严格 Bookmark 分页查询、频道启停和清除，已删除实时订阅及全部 Subscription 对象。
- Solution 新增 Client EXE、供 C# 调用的 Server Native DLL 和本地回环 Web 管理端；ZPigeon 不生成 NuGet 包。
- Client EXE 使用当前用户范围 CNG 身份键，网络与各模块分别写日志；SDK 的默认机器范围身份不变。
- 经 Owner 允许，在父级 MLE 本地增加 `Mem_ReAlloc` 并同步当前引用副本；不提交父级 MLE。`MLE_Todo.md` 继续只记录 `IO_CreatePipe` 和 `PS_CreateProcessEx` 方案。

## 当前验证

- Visual Studio 2026 下 x86/x64、Debug/Release 全 Solution Rebuild 均为零警告、零错误，直接产出三个 `.lib`、Client `.exe`、Server Native `.dll` 和 C# Web；构建固定使用本机完整的 Windows SDK 10.0.26100.0。
- 四个配置的 UnitTest 均为 324/324 通过，包含真实 localhost QUIC 集成；ConsumerTest 均通过。
- 已实际启动 VS2026 构建的 Web 与 Client，验证 Server Running、Client Ready、System.Info 往返、EventLog 查询及 Bookmark 下一页、首页静态资源以及 `network.log`/`system.log` 分离输出。
- 父级 MLE x64 Debug 全 Solution Build 及 43/43 测试通过。

## 下一步

1. 由 Owner 试用本地 Web/Client 闭环，修正发现的真实问题。
2. 审计剩余模块内部冗余、重复 Encode/Allocate 模式和可抽到 MLE 的候选；涉及 MLE 先询问 Owner。
3. 保持 QUIC/TLS-TCP/WSS 边界，不在当前试用版扩展新功能。

## 阻塞

当前没有架构或环境阻塞；统一使用 Visual Studio 2026 构建原生项目与 .NET 10 Web。
