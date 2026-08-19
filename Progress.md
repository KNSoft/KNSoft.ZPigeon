# KNSoft.ZPigeon 实施进度

更新时间：2026-08-20

## 当前阶段

Client 主动连接 Server 并接受统一管理。协议、Client 执行器、Server 控制 API、Native/Managed 桥接层和 Web 管理端已经形成可直接试用的完整闭环。

当前仅保留 x64，最低支持 Windows 10。项目不承担未发布协议、旧系统或错误历史设计的兼容负担。

## 已完成

- 核心架构：Transport、Connection、Protocol、SDK 和业务模块已经分层，Request 只由 Server 创建，业务 Channel 只由 Client 创建。
- 网络核心：QUIC、TLS 1.3、SNI、专属根证书验证、客户端持久身份、KeepAlive、重连、Request 和带背压的流式 Channel 已接通。
- 错误体系：`ZP_STATUS` 保留 NTSTATUS、Win32、Winsock、HRESULT、Security、QUIC、ProcessExit 等来源类型及原始 32 位代码。
- 系统管理：系统信息、注册表、用户与会话、WMI、更新、证书、凭据和事件查看器已经接入 Web。
- 网络管理：端口转发、网络共享、网络适配器、路由表、网络连接、WLAN 和防火墙已经接入 Web。
- 存储管理：文件浏览、分页、搜索、传输、哈希、属性、ACL、十六进制编辑和剪贴板监听已经接入 Web。
- 任务管理：进程、窗口、服务和任务计划已经接入 Web；进程支持内存读写，窗口支持静态捕获和视频流。
- 硬件管理：原生硬件信息、设备管理、固件、视频、音频和电源已经接入 Web；固件数据按 CPUID、SMBIOS 和 ACPI 按需读取，视频使用 Media Foundation 按需传输摄像头画面。
- 软件管理：传统程序、Windows App、Windows 可选功能以及 Edge/Chrome 管理和 CDP 入口已经接入 Web。
- 远程访问：ConPTY 终端、脚本执行、远程执行、通用 TCP/UDP 转发和 RDP 转发入口已经接入 Web。
- 通用组件：ACL 编辑器、远程文件选择器和虚拟十六进制编辑器已经在多个模块复用。
- 日志与错误反馈：Client 按网络和模块拆分日志；Web 保留远端原始错误域，不将 Win32、QUIC 等状态强转为 NTSTATUS。

## 当前验证

- Visual Studio 2026 x64 Debug 全 Solution 构建通过。
- UnitTest 为 374/374，通过真实 localhost QUIC 集成路径。
- Web、Managed、Native、Client 和 QUIC 本地闭环通过。
- 网络适配器、IPv4/IPv6 路由表、TCP/UDP 端点和网络共享完成真实只读联调。
- 摄像头枚举、采集、JPEG 编码、流式传输及主动停止完成本机只读联调。
- NetworkStatus 新增 Client 实现通过静态分析；其他模块仍有既有静态分析告警需要独立处理。
- 网卡控制、固件写入、磁盘格式化等可能影响开发机状态的功能未执行破坏性测试。

## 下一步

1. 按实际试用反馈修正功能完整性和交互一致性。
2. 完成当前代码的 x64 Release、干净环境和普通用户权限验证。
3. 继续审计冗余、资源边界和 MLE 复用候选；修改 MLE 前先取得 Owner 确认。

## 阻塞

当前没有外部阻塞。
