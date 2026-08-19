# KNSoft.ZPigeon 第一版交付清单

本文记录第一版从仓库内可用到可发布状态的检查项。架构和安全模型见 `Design.md`，实现状态见 `Progress.md`。

## 当前候选基线

- 最低支持 Windows 10，当前只提供 x64；ARM64 后续按需加入。
- 原生核心和公共 SDK 使用纯 C，并提供 Native DLL、Managed SDK、Client EXE 和 C# Web 管理端。
- QUIC 是当前完成端到端验证的 Transport；TLS/TCP 和 WSS 保留明确的 Transport 边界，不允许降级到明文协议。
- 14 个协议模块已经实现：System、Process、Service、File、Terminal、EventLog、Registry、Window、Administration、Execution、Tunnel、Browser、Wmi 和 Audio。
- Web 管理端已经覆盖系统、网络、存储、任务、硬件、软件和远程访问七类功能。
- ZPigeon 由同一 Solution 直接构建，不单独发布 NuGet 包。

## 公共文件

- Core：`Protocol.h`、`SDK.h`、`Operations.h`、`Client.h`、`Server.h`。
- 基础管理：`System.h`、`Process.h`、`Service.h`、`File.h`、`Terminal.h`、`EventLog.h`、`Registry.h`。
- 扩展管理：`Window.h`、`Administration.h`、`Execution.h`、`Tunnel.h`、`Browser.h`、`Wmi.h`、`Audio.h`。

## API 与 ABI

- [x] 公开 `Zp*` 声明具有唯一实现，公共入口和回调使用一致调用约定。
- [x] Request、Channel 使用不透明 Handle，并定义取消、关闭和回调期 Buffer 生命周期。
- [x] 远程结果使用 `ZP_STATUS` 保留来源错误域和原始 32 位代码。
- [x] Protocol 不传输指针、结构体填充或依赖编译器对齐的本机数据。
- [x] 模块版本必须完全相同才参与协商，不保留旧 Decoder 或自动降级。
- [x] ConsumerTest 只依赖公共 include 根，并验证 C/C++ 消费和三个静态库链接。
- [ ] 补齐新增模块的公开 API 参考和稳定 Payload 规格。
- [ ] 在正式发布前确定 ABI 版本与结构扩展规则。

## 安全与资源

- [x] Client 使用部署专属根证书和 ServerName 验证 Server 身份。
- [x] Client 使用持久 CNG 实例密钥完成签名认证。
- [x] Request、Payload、Channel、Frame、分页快照和连续流均具有资源边界。
- [x] 文件上传使用同目录临时文件并在完成后原子提交。
- [x] 文件、终端、隧道、图像和音频等长数据通过 Channel 流式传输。
- [x] Web 只监听本机回环；对外访问要求本机反向代理提供 HTTPS 和身份验证。
- [ ] 在普通用户权限下完成全部模块的权限失败与资源清理验证。
- [ ] 对最终依赖执行漏洞、许可证和版本锁定复核。

## 构建与验证

- [x] Visual Studio 2026 x64 Debug 全 Solution 构建通过。
- [x] x64 Debug UnitTest 374/374 通过，包含 localhost QUIC 集成。
- [x] Web、Managed、Native、Client 和 QUIC 本地闭环通过。
- [x] 网络共享、网络适配器、IPv4/IPv6 路由和 TCP/UDP 端点完成只读联调。
- [x] NetworkStatus 新增 Client 实现通过静态分析。
- [ ] 对当前代码重新执行 x64 Release 全 Solution 构建、UnitTest 和 ConsumerTest。
- [ ] 在干净环境仅通过 restore、build 和 test 验证无开发机隐式依赖。
- [ ] 在最终交付布局运行完整 Client/Server/Web 冒烟测试。
- [ ] 对 Release 二进制执行架构、调试信息、默认库和公共符号检查。

## 禁止的发布前测试

- 不在开发机执行磁盘格式化。
- 不在开发机写入 UEFI 变量或修改固件启动顺序。
- 不为验证网卡控制而中断当前管理连接。

## 当前发布阻塞项

- 当前代码尚未重新完成 x64 Release 和干净环境验证。
- 普通用户权限矩阵尚未覆盖全部模块。
- 新增模块的稳定 API/Payload 文档仍需补齐。
