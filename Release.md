# KNSoft.ZPigeon 第一版 SDK 交付清单

本文定义第一版从“仓库内可用”到“可直接试用”的交付门槛。协议和安全模型见 `Design.md`，实现状态见 `Progress.md`，使用入口见 `README.md`。

> C/S 方向纠偏已经完成：Server 发起全部管理业务，Client 在被控主机执行。当前优先交付同一 Solution 可直接构建和运行的本地试用版，不制作 ZPigeon NuGet。

## 当前候选基线

- 支持 Windows 10 及以上系统，语言与公共 ABI 为 C；
- 目标架构为 x86、x64，配置为 Debug、Release；
- 产物为三个静态库、`KNSoft.ZPigeon.Client.exe`、`KNSoft.ZPigeon.Server.Native.dll` 和 `KNSoft.ZPigeon.Web`；
- QUIC 为第一版已实现 Transport，TLS 1.3、SNI、专属根证书链和客户端持久身份均已接通；
- System、Process、Service、File、Terminal、EventLog、Registry Version 1 已实现；
- x86/x64、Debug/Release 全 Solution Rebuild，四配置各自 324/324 测试及 ConsumerTest 通过；x64 的 Web/Native/QUIC/Client localhost 冒烟、System.Info 与 EventLog Bookmark 分页通过。

## 公共文件集合

Solution 产物对应的公共头文件保持 `KNSoft/ZPigeon/...` include 布局：

- `Protocol.h`、`SDK.h`、`Operations.h`；
- `Client.h`、`Server.h`；
- `System.h`、`Process.h`、`Service.h`；
- `File.h`、`Terminal.h`、`EventLog.h`、`Registry.h`。

构建输出还必须包含：

- 对应平台和配置的三个静态库、Client EXE、Server Native DLL 与 Web；
- Debug/Release 和 x86/x64 隔离的 `OutDir`；
- Native DLL 与 Web 位于相同输出目录，可由 C# 直接加载；
- Client 和 Web 位于相同输出目录，共享首次生成的本地试用根证书。

## API 与 ABI 门槛

- [x] 所有公开 `Zp*` 声明均存在唯一实现；
- [x] Client/Server 公共入口统一使用 `NTAPI`，回调 typedef 与实现调用约定一致；
- [x] Client/Server 配置以 `Size` 校验结构版本，字符串、数组和证书输入在 `Create` 中深拷贝或增加引用；
- [x] Request、Channel 使用不透明 Handle，并定义取消、关闭和回调期 Buffer/View 生命周期；
- [x] 模块 ID、Version、Operation/Event ID 已在公共头文件与 `Design.md` 中固定；
- [x] 仓库内独立 `ConsumerTest` 仅使用公共 include 根，在 C 与 C++ 翻译单元中编译全部 12 个公共头并链接三个静态库；
- [x] ConsumerTest 不引用仓库私有头文件；
- [x] 从 C++ 消费者编译全部公共头文件，验证 `EXTERN_C_START/END` 和符号链接；
- [ ] 为所有异步入口补齐简明 API 参考，明确成功/失败输出、回调线程、可重入操作和 Handle 关闭责任；
- [ ] 确定第一版 ABI 版本号与后续结构扩展规则，避免仅依赖包版本表达二进制兼容性。

## 安全与资源门槛

- [x] Server 证书必须包含可用私钥，Client 使用部署专属根证书链并校验 ServerName；
- [x] Client 持久公钥签名认证已接通；公钥本身作为实例身份，不维护派生 ClientId；通过部署根认证的 Server 对 Client 拥有完整管理能力，不存在 Read/Control 操作级授权层；
- [x] Request 数量、Request Payload 总量、Channel 和分页快照均有硬上限；
- [x] File.OpenWrite 使用同目录随机临时文件，完整写入和刷新后原子提交，取消或失败时清理临时文件；
- [x] 文件与 Terminal 长流使用窗口，EventLog 查询使用有界分页，不把无界数据堆入内存；
- [ ] 对最终 Release 依赖执行漏洞与许可证复核，并记录可复现的依赖锁定结果；
- [ ] 在受限普通用户账户下执行完整集成测试，确认权限失败均通过 NTSTATUS 返回且不残留资源。

## 构建与验证门槛

- [x] x86 Debug：全 Solution Rebuild、324/324 测试和 ConsumerTest；
- [x] x86 Release：全 Solution Rebuild、324/324 测试和 ConsumerTest；
- [x] x64 Debug：全 Solution Rebuild、324/324 测试及本地试用链路通过；
- [x] x64 Release：全 Solution Rebuild、324/324 测试和 ConsumerTest；
- [x] 独立 `ConsumerTest` 在 x86/x64 Debug/Release 均构建并成功运行；
- [x] MSVC x64 Debug 首轮静态分析完成，Protocol 为零告警，SDK 告警已按真实问题、框架 SAL 和第三方内联实现分流；
- [ ] 在干净环境仅执行 restore + build + test，验证没有开发机隐式依赖；
- [ ] 使用最终交付布局运行最小 Client/Server 示例和 localhost QUIC 集成测试；
- [ ] 对 Release 库运行基础二进制检查，确认架构、调试信息、默认库和公共符号符合预期。

## Solution 交付待办

1. 验证从干净工作区恢复现有依赖后可一次构建 Solution；
2. 记录第一版仅实现 QUIC，TLS-TCP/WSS 的边界和枚举继续保留；
3. 保留与产物匹配的源码、符号、依赖版本和测试摘要。

## 当前阻塞发布的事项

- 尚未确定第一版 ABI 版本策略；
- 尚未完成干净环境和普通用户权限下的发布候选验证；
- 公共 API 参考仍需补齐回调线程、重入和失败输出约定。
