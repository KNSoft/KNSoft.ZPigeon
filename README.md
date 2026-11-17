# KNSoft.ZPigeon (WIP)

KNSoft.ZPigeon 是面向 Windows 系统的远程管理平台，[功能模块](#功能模块) 覆盖极其广泛。

我相信鸽子的最终形态会是局域网设备管理，尤其针对企业内部资产。**当前为未发布的本机闭环原型，仅供学习交流使用，禁止用于未经授权等非法用途**，详见 [许可协议](#许可协议)。

## 项目优势

- 高效传输：以 QUIC 与 TLS 1.3 为主要传输路径，支持异步 Request、流式 Channel、背压控制、KeepAlive、大文件分块传输和自适应压缩
- 清晰架构：传输、连接、协议和业务模块严格分层，并以 SDK 形式设计。Client 只负责执行，Server 统一发起控制，由 Client 主动连接 Server
- 安全部署：严格验证 Server 身份，Client 使用持久化 CNG 实例密钥
- 极简高效：核心使用纯 C，并优先调用 NT 层接口。不仅功能更多更强，运行效率也更佳。不背负旧系统和历史兼容路径，直接使用 Windows 10 及以上系统能力，带来当下最佳体验
- 功能全面：系统管理能力覆盖广泛，包含 文件数据、系统软硬件、网络及端口转发、远程桌面/终端 等功能，详见 [功能模块](#功能模块)
- AI 赋能：通过标准 MCP 入口向外部智能体提供有界、结构化的管理工具

## 功能模块

- ZPigeon：Client 自身网络与进程状态查看、连接策略管理

- 系统管理
  - 系统信息、系统配置查看与管理
  - 注册表管理、WinObj 查看、WMI 查看与查询
  - WSL、AppContainer、Windows Sandbox 管理
  - 用户与会话查看与管理，支持 Microsoft 账户、用户配置文件管理
  - 系统更新、证书、凭据、事件等查看与管理

- 网络
  - 端口转发：管理 TCP、UDP 及 RDP、CDP、WinDbg 等内置转发规则
  - 代理与 VPN：查看和管理当前用户 WinINET 代理、系统 WinHTTP 代理及 RAS VPN 配置
  - 网络共享：管理本机发布的共享和连接到其他主机的共享
  - 网络适配器：查看接口状态、地址、速率、统计信息和网络类别，并支持启用、禁用及公用/专用网络切换
  - 路由表：查看与管理 IPv4 和 IPv6 路由表
  - 网络连接：显示 TCP、UDP、IPv4、IPv6 端点、连接状态和所属进程，并可关闭受支持的已建立 IPv4 TCP 连接
  - WLAN：查看 WLAN 网络和配置文件，执行连接管理，读取受支持的已保存密码并下载配置文件 XML
  - 防火墙：管理网络配置文件、入站规则和出站规则

- 存储管理
  - 文件：提供接近资源管理器的文件管理体验
    - 支持分页文本与结构化数据查看、多媒体预览和通配符搜索
    - 支持特定文件类型快捷操作，如快捷方式与证书查看/安装、脚本、INF、注册表、字体和安装包操作等
    - 上传、下载文件，从 Internet 下载文件
    - 查询文件占用并解除占用
  - BitLocker、卷影副本查看与管理
  - 便携设备：浏览和管理 MTP 设备中的对象
  - 剪贴板：读取、编辑并监听用户剪贴板内容和格式变化，可视化查看和管理剪贴板中的文本和图像

- 任务管理
  - 进程管理（支持 WSL）、服务管理（支持内核驱动服务）、任务计划管理
  - 窗口：提供原生窗口管理（窗口属性、控制、静态图像和视频流等）和 UI Automation 查看

- 硬件管理
  - 硬件信息查看、设备管理
  - 固件（CPUID、SMBIOS、ACPI、UEFI）信息查看，并管理受支持的 UEFI 变量和启动项
  - 视频：枚举被控端摄像头，并按可选分辨率、帧率和图像质量实时查看和录制画面
  - 音频：管理输入输出设备、合成器和音量，并获取和录制输入或输出音频流
  - 电源：显示电池与 UPS 状态，管理电源计划，并可执行关闭显示器、锁屏、注销、睡眠、休眠、关机、重启和固件启动等操作
  - 蓝牙、串口等设备管理，位置信息读取（基于 Windows 位置服务），键盘记录

- 软件管理
  - 已安装程序：查看传统程序、Windows App 和 Windows 可选功能，并管理可用项目
  - 程序包：按当前账户实际可用的运行时动态显示 WinGet、Python/pip、NodeJS/npm、Chocolatey 和 .NET Global Tools 页签；支持枚举及相应的静默安装、升级、全部升级和卸载操作，也可后台下发 MSI、MSIX/AppX、Bundle、App Installer 及依赖包
  - 输入法、字体管理
  - 浏览器：管理和控制 Edge 和 Chrome
    - 读取浏览数据和 Profile，支持在浏览器运行时读取正在使用的 Profile 数据
    - 读取当前账户可解密的 Cookie 和密码，支持 App-Bound 解密，并标明无法解密值的保护状态
    - 多标签远程控制、Headless 会话和可选的本机 DevTools；支持在浏览器运行时将现有 Profile 一次性复制为独立 Profile

- 远程访问
  - 远程终端：基于系统 ConPTY 提供多会话 Shell，支持命令提示符、PowerShell、WSH、HTA 脚本执行和完整交互
  - 远程执行
    - 可运行本机或下发的程序与脚本
    - 支持 NodeJS、Python 和 Go 等脚本（如 Client 端存在对应运行时）
    - 支持自定义 Token 执行（NtCreateToken），支持 SYSTEM、TrustedInstaller 等预设以及 AppContainer Profile
  - 远程桌面：管理启用状态、端口及同一用户多会话策略；按精确 `termsrv.dll` 版本启用或关闭仅驻留内存的多会话补丁；
    管理 Child Sessions 系统功能、Hello-only 登录和仅限 localhost 的凭据委派，并基于 Client 当前活动用户会话创建独立 Child Session；通过受控端口转发建立 RDP 连接，或由用户明确点击开始后使用 Web 交互式远控

- MCP
  - 通过 Streamable HTTP 向外部智能体提供有界、结构化的管理工具；目标由瞬时 `ClientId` 显式指定

## 架构与本地运行

项目整体解决方案文件为 [KNSoft.ZPigeon.slnx](Source/KNSoft.ZPigeon.slnx)：
- Protocol：定义传输无关的 Frame、消息、状态和模块 Codec
- Client SDK：运行于被控端，维护连接并执行 Server 下发的本机操作
- Server SDK：并发维护已认证 Client 连接并针对指定连接发起管理请求
- Server Native：向托管程序提供稳定的 C ABI
- Server Managed：封装可复用的 .NET 管理能力
- Application：把 Server Managed 能力组合为显式目标、受边界约束的管理用例
- Tools：定义 MCP 的唯一工具目录及读写语义
- Web：承载本地回环 REST、MCP、连接分组控制台、系统设置、Client EXE 生成和按 Client 隔离的可视化控制界面
- Transport：支持 QUIC、TLS/TCP 和 DTLS/UDP，默认使用 QUIC

首次获取源码时需初始化 `Source/3rdParty/rdpwrap.ini` 子模块。

本地运行：
1. 启动 `KNSoft.ZPigeon.Web.exe`
2. 启动一个或多个 `KNSoft.ZPigeon.Client.exe`
3. 打开 `http://127.0.0.1:9983`，在首页控制台按分组选择 Client，进入其控制面板

首页控制台显示多个连接分组、传输方式、往返延迟、收发字节和请求状态，并支持搜索及按客户端公钥指纹保存分组调整。系统设置提供浅色、深色、跟随系统主题及蓝色、青色、紫色主题颜色，外观在当前浏览器内保存并应用到客户端控制面板。

“生成客户端”支持任意选择 46 个逻辑模块、6 个远程执行扩展以及 TCP、UDP、QUIC 传输。C# 直接调用 VS 2026 的 MSBuild 18 子进程，通过该进程的 `CL=/D...` 传入模块宏，在独立源码快照中构建整个解决方案的 Release x64 版本；页面显示实时编译输出、状态、取消按钮和最终 EXE 大小。相同源码、依赖内容、工具链和宏集合复用缓存模板。首次构建需要本机具备源码、已还原的依赖与 VS 2026；构建失败会显示日志。

连接目标当前固定为 `127.0.0.1`，TCP 与 QUIC 默认使用端口 `21943`，UDP/DTLS 使用下一端口 `21944`，TLS 名称为 `localhost`。Server 可用 `Transport:Port`（命令行 `--Transport:Port=22943`）显式配置基础端口，范围为 1–65534；生成客户端使用当前 Server 的端口。端口占用或系统保留时需要显式选定可用端口，不自动换端口。传输复选框决定编译支持和有序连接候选；同一实例保持一条活动连接。TCP 默认 TLS 1.2 或更高，可要求 TLS 1.3；当前 QUIC 包与严格 TLS 1.3 要求 Windows 11 / Server 2022，Windows 10 宿主开放 TCP/UDP。所有路径保留加密和 Server 证书验证。

生成器在模板副本中写入默认分组、连接顺序、TLS 策略、实例标识和当前 Server 根证书。“新实例”分配独立 ID；“更新已有实例”保留 ID，生成记录按部署持久化。CNG 密钥名固定为 `KNSoft.ZPigeon.Client.<部署根公钥 SHA-256>.<实例 ID>`，不提供任意名称输入。私钥由目标机器的当前用户 CNG 首次创建，EXE 不包含实例或 Server 私钥；同机同账户两个独立实例使用不同密钥，同一实例重复运行由按用户和角色区分的 NT 互斥对象拒绝。

命令执行基础模块与 TCP 的实测 EXE 为 212,992 字节（208 KiB），仅需一个 EXE；完整版本为 1,341,952 字节（约 1.28 MiB），比原动态 Loader 的 1,320,448 字节基线增加 21,504 字节（21 KiB），同时省去 164,704 字节的 Loader DLL。当前生成器仍为选中的 WinGet 提供 `Microsoft.Management.Deployment.dll` 和 `.winmd` 旁文件；`.winmd` 用于构建期投影，其分发策略待确认。RTC/P2P 的 WebView2 Loader 静态链接进 EXE，无需 `WebView2Loader.dll` 旁文件，目标系统仍需安装 WebView2 Runtime。Client 握手签名覆盖编译能力和实例元数据；Web 页面及 MCP 目录按已认证能力过滤。编译宏和构建细节见 [Client 可选编译与 Web 构建](Docs/ClientBuildProfiles.md)。

MCP Streamable HTTP 端点为 `http://127.0.0.1:9983/mcp`。外部调用方先使用 `list_clients` 获取本次 Server 进程内有效的 `ClientId`，再把该值传给其他工具。MCP 无会话目标状态，不会隐式沿用上一次选择的 Client。

MCP 工具按用途显式列入目录，不会因底层 SDK 新增 API 或枚举值而自动暴露。Cookie、密码等浏览器数据读取与其他读取工具一样属于只读工具。

当前 Web 是本机闭环原型，仅接受 `127.0.0.1` 的规范 Host 和同源浏览器请求；HTTP、WebSocket 与 MCP 都会经过同一 Host 边界，浏览器请求还会拒绝跨站来源。没有浏览器来源信息的本机原生程序仍视为可信，本阶段不把任意本机进程纳入安全边界。对外访问应通过本机反向代理，并重新设计 HTTPS、身份验证和可信代理边界。多个 Client 可同时主动连接一个 Server，并接受 Server 的统一管理。首页按分组及客户端公钥指纹列出当前连接，选择后进入该 Client 的独立管理上下文。当前分组仅用于控制台组织，不提供独立 Deployment 信任根或入组授权。

## 文档

- [Design.md](Design.md)：架构、协议与安全模型。
- [ClientBuildProfiles.md](Docs/ClientBuildProfiles.md)：Client 功能与传输的可选编译方案。
- [RemoteMedia.md](Docs/RemoteMedia.md)：远控画面流、P2P 路由恢复和 WebRTC 视频轨实验。
- [Release.md](Release.md)：发布前检查清单。

## 许可协议

仅允许合法、获授权的非商业使用；禁止滥用；商业使用须事先取得 [Ratin Gao &lt;ratin@knsoft.org&gt;](mailto:ratin@knsoft.org) 的书面许可。见 [LICENSE](LICENSE)。
