# KNSoft.ZPigeon (WIP)

KNSoft.ZPigeon 是面向 Windows 系统的远程管理平台，[功能模块](#功能模块) 覆盖极其广泛。

我相信鸽子的最终形态会是局域网设备管理，尤其针对企业内部资产。**当前为未发布的本机闭环原型，仅供学习交流使用，禁止用于未经授权等非法用途**，详见 [许可协议](#许可协议)。

## 项目优势

- 高效传输：以 QUIC 与 TLS 1.3 为主要传输路径，支持异步 Request、流式 Channel、背压控制、KeepAlive、大文件分块传输和自适应压缩
- 清晰架构：传输、连接、协议和业务模块严格分层，并以 SDK 形式设计。Client 只负责执行，Server 统一发起控制，由 Client 主动连接 Server
- 安全部署：严格验证 Server 身份，Client 使用持久化 CNG 实例密钥
- 极简高效：核心使用纯 C，并优先调用 NT 层接口。不仅功能更多更强，运行效率也更佳。不背负旧系统和历史兼容路径，直接使用 Windows 10 及以上系统能力，带来当下最佳体验
- 功能全面：系统管理能力覆盖广泛，包含 文件数据、系统软硬件、网络及端口转发、远程桌面/终端 等功能，详见 [功能模块](#功能模块)
- AI 赋能：同一工具目录同时提供标准 MCP 入口和管理端内置智能体，支持 OpenAI Responses、OpenAI-compatible Chat Completions 和 Anthropic Messages

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
  - 远程桌面：管理启用状态、NLA、端口及同一用户多会话策略；按精确 `termsrv.dll` 版本启用或关闭仅驻留内存的多会话补丁；通过受控端口转发建立 RDP 连接，或由用户明确点击开始后使用 Web 交互式远控

- 智能体
  - MCP：通过 Streamable HTTP 向外部智能体提供有界、结构化的管理工具；目标由瞬时 `ClientId` 显式指定
  - 模型：从仓库内的 models.dev 快照选择 Provider 和模型，或手动配置接口协议、Base URL、认证、上下文、输出、Reasoning、超时及高级 JSON；凭据使用当前 Windows 账户加密后保存在本机
  - Agent：绑定模型、System Prompt、工具以及 `AGENTS.md`、`TOOLS.md`、`MEMORY.md` 和自定义 Markdown
  - 会话：历史保存在 Server，支持搜索、分支和导出；运行时支持 Tool Call 时间线、Token 用量、上下文压缩、终止、消息排队和插队

## 架构与本地运行

项目整体解决方案文件为 [KNSoft.ZPigeon.slnx](Source/KNSoft.ZPigeon.slnx)：
- Protocol：定义传输无关的 Frame、消息、状态和模块 Codec
- Client SDK：运行于被控端，维护连接并执行 Server 下发的本机操作
- Server SDK：并发维护已认证 Client 连接并针对指定连接发起管理请求
- Server Native：向托管程序提供稳定的 C ABI
- Server Managed：封装可复用的 .NET 管理能力
- Application：把 Server Managed 能力组合为显式目标、受边界约束的管理用例
- Tools：定义 MCP 与内置智能体共用的唯一工具目录及读写语义
- Agent：持久化模型、Agent 与会话，并通过 OpenAI Responses、Chat Completions 或 Anthropic Messages 执行工具循环
- Web：承载本地回环 REST、MCP、模型与 Agent 配置、会话界面、已连接 Client 首页和按 Client 隔离的可视化控制界面
- Transport：支持 QUIC、TLS/TCP 和 DTLS/UDP，默认使用 QUIC

首次获取源码时需初始化 `Source/3rdParty/rdpwrap.ini` 子模块。

本地运行：
1. 启动 `KNSoft.ZPigeon.Web.exe`
2. 启动一个或多个 `KNSoft.ZPigeon.Client.exe`
3. 打开 `http://127.0.0.1:9983`，再从首页选择 Client

MCP Streamable HTTP 端点为 `http://127.0.0.1:9983/mcp`。外部调用方先使用 `list_clients` 获取本次 Server 进程内有效的 `ClientId`，再把该值传给其他工具。MCP 无会话目标状态，不会隐式沿用上一次选择的 Client。管理端“智能体”页面则把当前页面的 Client 固定绑定到工具调用，模型无法改选目标。

AI 工具按用途显式列入目录，不会因底层 SDK 新增 API 或枚举值而自动暴露。Cookie、密码等敏感浏览器数据使用独立工具和敏感性标记，并提示模型仅在用户明确要求时调用。OpenAI 请求显式关闭 Provider 端存储；所有模型服务的实际数据处理和保留策略仍由所选 Provider 决定。

当前 Web 是本机闭环原型，仅接受 `127.0.0.1` 的规范 Host 和同源浏览器请求；HTTP、WebSocket 与 MCP 都会经过同一 Host 边界，浏览器请求还会拒绝跨站来源。没有浏览器来源信息的本机原生程序仍视为可信，本阶段不把任意本机进程纳入安全边界。对外访问应通过本机反向代理，并重新设计 HTTPS、身份验证和可信代理边界。多个 Client 可同时主动连接一个 Server，并接受 Server 的统一管理。首页按客户端公钥指纹列出当前连接，选择后进入该 Client 的独立管理上下文。

## 文档

- [Design.md](Design.md)：架构、协议与安全模型。
- [Release.md](Release.md)：发布前检查清单。

## 许可协议

仅允许合法、获授权的非商业使用；禁止滥用；商业使用须事先取得 [Ratin Gao &lt;ratin@knsoft.org&gt;](mailto:ratin@knsoft.org) 的书面许可。见 [LICENSE](LICENSE)。
