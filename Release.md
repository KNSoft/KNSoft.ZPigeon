# KNSoft.ZPigeon 第一版发布检查清单

本文记录第一版从仓库内可用到可发布状态的检查项。架构和安全模型见 `Design.md`，待办见 `TODO.md`。

## 候选基线

- 最低支持 Windows 10，当前只提供 x64；ARM64 后续按需加入。
- 原生核心和公共 SDK 使用纯 C，并提供 Native DLL、Managed SDK、Application、共享工具目录、内置 Agent、Client EXE 和 C# Web 管理端。
- Transport 支持 QUIC、TLS/TCP 和 DTLS/UDP，不允许降级到明文协议。
- 46 个协议模块已经实现；`Execution`、`Browser` 和 `Software` 为 Version 2，其余模块为 Version 1。ModuleId 9、20–46 共用 `Administration` Codec 和执行框架，其余模块按功能目录独立实现。
- Web 管理端覆盖系统、网络、存储、任务、硬件、软件、远程访问和智能体八类功能，并在 `/mcp` 提供无状态 Streamable HTTP。
- ZPigeon 由同一 Solution 直接构建，不单独发布 NuGet 包。

## API 与 ABI

- [x] 公开 `Zp*` 声明具有唯一实现，公共入口和回调使用一致调用约定。
- [x] Request、Channel 使用不透明 Handle，并定义取消、关闭和回调期 Buffer 生命周期。
- [x] 远程结果使用 `ZP_STATUS` 保留来源错误域和原始 32 位代码。
- [x] Protocol 不传输指针、结构体填充或依赖编译器对齐的本机数据。
- [x] 模块版本必须完全相同才参与协商，不保留旧 Decoder 或自动降级。
- [x] ConsumerTest 只依赖公共 include 根，并验证 C/C++ 消费和三个静态库链接。
- [x] ToolCatalogTest 验证 MCP/内置智能体工具目录，以及模型凭据、会话搜索、队列、Usage 和分支持久化。
- [ ] 补齐 46 个模块的公开 API 参考和稳定 Payload 规格。
- [ ] 确定第一版 ABI 版本与结构扩展规则。

## 安全与资源

- [x] Client 使用部署专属根证书和 ServerName 验证 Server 身份。
- [x] Client 使用指定当前用户或本地计算机作用域的持久 CNG 实例密钥完成签名认证。
- [x] Request、Payload、Channel、Frame、分页快照和连续流均具有资源边界。
- [x] 文件上传使用同目录临时文件并在完成后原子提交。
- [x] 文件、终端、隧道、图像和音频等长数据通过 Channel 流式传输。
- [x] Web 只监听规范 `127.0.0.1` 回环，并通过 Host、Origin、Fetch Metadata 和 WebSocket Origin 阻止恶意网页跨站访问；本机原生程序仍视为可信。
- [x] Server Native、Managed 和 Web 支持多个同时连接的 Client，所有连接相关操作显式路由，长生命周期 Web 状态按 Client 隔离。
- [x] 远程浏览器固定使用 Headless 和独立用户数据目录；现有 Profile 只允许经明确确认复制，不直接打开或修改。
- [x] 浏览器 Cookie 和密码的加密内容默认不显示明文，无法解密的 App-Bound 值明确标记保护状态。
- [x] MCP 工具显式接收瞬时 ClientId；内置智能体在工具绑定层固定当前 Client，模型无法改选目标。
- [x] MCP 与内置智能体复用同一工具定义；工具声明只读、破坏性、幂等和开放世界语义，并限制结果数与函数调用循环。
- [x] 模型凭据使用当前 Windows 账户 DPAPI 保护后存入本机配置目录；批量列表不返回，管理详情按需解密并禁止缓存。
- [x] OpenAI 请求关闭 Provider 端存储；敏感浏览器数据使用独立工具，页面明确提示所有模型服务的数据边界。
- [ ] 为新连接按来源 IP、Deployment 和全局维度实施连接及握手速率限制。
- [ ] 正式发布或对外代理前确定 Web 身份验证、Client Enrollment 和可信代理方案。
- [ ] 在普通用户权限下完成全部模块的权限失败与资源清理验证。
- [ ] 对最终依赖执行漏洞、许可证和版本锁定复核。

## 构建与验证

- [x] Visual Studio 2026 x64 Debug 全 Solution 构建具有已知通过基线。
- [x] `Source/I18nTest.mjs` 检查通过。
- [ ] 对发布候选重新执行 x64 Debug 和 Release 全 Solution 构建、UnitTest 与 ConsumerTest。
- [ ] 在干净环境仅通过 restore、build 和 test 验证无开发机隐式依赖。
- [ ] 在最终交付布局运行 Client、Server、Native、Managed 和 Web 冒烟测试。
- [ ] 完成多 Client、断线重连、接收容错、请求取消和 Web 同源边界测试。
- [ ] 完成 MCP 2026-07-28 无握手请求、旧版 initialize 兼容、tools/list、全部工具 Schema、显式 ClientId 路由、取消和错误域测试。
- [ ] 完成内置 Agent 的 models.dev 目录、认证、OpenAI Responses/Chat Completions、Anthropic Messages、队列/Steer/Stop、压缩、工具循环、目标隔离和模型服务故障测试。
- [ ] 完成浏览器运行中 History、Downloads、Cookie、Password 读取、App-Bound 成功与失败、Profile 复制、远程控制和 DevTools 测试。
- [ ] 完成音频输入输出、摄像头、窗口录制以及停止、下载和删除测试。
- [ ] 审核 `Source/OutDir/CodeAnalysis` 中的 C/C++ SARIF 诊断，修正真实问题并仅对确认误报作最小范围抑制。
- [ ] 对 Release 二进制执行架构、调试信息、默认库和公共符号检查。
- [ ] 确认 README、源码包和交付包包含同一份 `LICENSE`。

## 测试安全边界

- 不在开发机执行磁盘格式化。
- 不在开发机写入 UEFI 变量或修改固件启动顺序。
- 不为验证网卡控制而中断当前管理连接。
