# KNSoft.ZPigeon 第一版 SDK 交付清单

本文定义第一版 SDK 从“仓库内可用”到“可供外部项目消费”的交付门槛。协议和安全模型见 `Design.md`，实现状态见 `Progress.md`，使用入口见 `README.md`。

## 当前候选基线

- 支持 Windows 10 及以上系统，语言与公共 ABI 为 C；
- 目标架构为 x86、x64，配置为 Debug、Release；
- 产物为 `KNSoft.ZPigeon.Protocol`、`KNSoft.ZPigeon.Client.SDK`、`KNSoft.ZPigeon.Server.SDK` 三个静态库；
- QUIC 为第一版已实现 Transport，TLS 1.3、SNI、专属根证书链和客户端持久身份均已接通；
- System、Process、Service、File、Terminal、EventLog、Registry Version 1 已实现；
- 当前四配置均为零编译/链接警告，每配置 336/336 项断言通过。

## 公共文件集合

交付包必须包含以下头文件，并保持 `KNSoft/ZPigeon/...` include 布局：

- `Protocol.h`、`SDK.h`；
- `Client.h`、`Server.h`；
- `System.h`、`Process.h`、`Service.h`；
- `File.h`、`Terminal.h`、`EventLog.h`、`Registry.h`。

交付包还必须包含：

- x86/x64 对应的三个静态库；
- Debug/Release 库之间不会误链接的目录或 MSBuild 选择规则；
- 直接依赖及传递依赖的版本、许可证和链接要求；
- `README.md`、`Design.md`、`LICENSE` 和版本变更记录；
- Release 符号文件，至少保留与发布二进制严格匹配的私有归档。

## API 与 ABI 门槛

- [x] 所有公开 `Zp*` 声明均存在唯一实现；
- [x] Client/Server 公共入口统一使用 `NTAPI`，回调 typedef 与实现调用约定一致；
- [x] Client/Server 配置以 `Size` 校验结构版本，字符串、数组和证书输入在 `Create` 中深拷贝或增加引用；
- [x] Request、Channel、Subscription 使用不透明 Handle，并定义取消、关闭和回调期 Buffer/View 生命周期；
- [x] 模块 ID、Version、Operation/Event ID 已在公共头文件与 `Design.md` 中固定；
- [ ] 从最终打包后的 include/lib 目录构建独立 C 消费者，不引用仓库私有头文件；
- [ ] 从 C++ 消费者编译全部公共头文件，验证 `EXTERN_C_START/END` 和符号链接；
- [ ] 为所有异步入口补齐简明 API 参考，明确成功/失败输出、回调线程、可重入操作和 Handle 关闭责任；
- [ ] 确定第一版 ABI 版本号与后续结构扩展规则，避免仅依赖包版本表达二进制兼容性。

## 安全与资源门槛

- [x] Server 证书必须包含可用私钥，Client 使用部署专属根证书链并校验 ServerName；
- [x] ClientId 由认证公钥计算，控制操作默认拒绝，应用可按 ClientId/模块/操作授权；
- [x] Request 数量、Request Payload 总量、Channel、Subscription 和分页快照均有硬上限；
- [x] File.OpenWrite 使用同目录随机临时文件，完整写入和刷新后原子提交，取消或失败时清理临时文件；
- [x] 文件、Terminal 和 EventLog 长流使用窗口或有界批次，不把无界数据堆入内存；
- [ ] 对最终 Release 依赖执行漏洞与许可证复核，并记录可复现的依赖锁定结果；
- [ ] 在受限普通用户账户下执行完整集成测试，确认权限失败均通过 NTSTATUS 返回且不残留资源。

## 构建与验证门槛

- [x] x86 Debug：零编译/链接警告，336/336；
- [x] x86 Release：零编译/链接警告，336/336；
- [x] x64 Debug：零编译/链接警告，336/336；
- [x] x64 Release：零编译/链接警告，336/336；
- [x] MSVC x64 Debug 首轮静态分析完成，Protocol 为零告警，SDK 告警已按真实问题、框架 SAL 和第三方内联实现分流；
- [ ] 在干净环境仅执行 restore + build + test，验证没有开发机隐式依赖；
- [ ] 使用最终交付布局运行最小 Client/Server 示例和 localhost QUIC 集成测试；
- [ ] 对 Release 库运行基础二进制检查，确认架构、调试信息、默认库和公共符号符合预期。

## 打包与发布待办

1. 确定首发载体和版本号；原生 NuGet 包应优先提供 `build/native` include、lib 和自动链接规则，压缩包可作为并行的透明产物；
2. 增加可重复的打包脚本或 MSBuild Target，产物只来自已验证配置，不从工作树临时目录手工拼装；
3. 增加包内消费者测试，分别覆盖 Client-only、Server-only 和 Protocol-only 链接；
4. 生成变更记录与已知限制，明确第一版仅实现 QUIC，TLS-TCP/WSS 枚举值不代表已有实现；
5. 以候选 tag 重跑全部门槛，记录提交号、依赖版本、构建工具版本和测试摘要；
6. 发布后保留对应源码、符号、包哈希和可复现构建记录。

## 当前阻塞发布的事项

- 尚无正式打包定义或消费者安装验证；
- 尚未确定第一版包版本与 ABI 版本策略；
- 尚未完成干净环境和普通用户权限下的发布候选验证；
- 公共 API 参考仍需补齐回调线程、重入和失败输出约定。
