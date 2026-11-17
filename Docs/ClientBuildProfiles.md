# Client 可选编译与 Web 构建

Client 的每个业务模块和传输方式各用一个独立编译宏，用户可任意选择功能子集。Visual Studio 继续使用现有 Debug 与 Release 配置，功能组合由宏集合表示。Web 将复选框转换成构建子进程的 `CL=/D...`，直接调用完整解决方案构建，不生成选择头文件或 `.props`。模块声明头文件无条件包含，便于未来使用 PCH；宏裁剪实现及其引用。编译模板再写入连接、分组和实例标识。CNG 密钥名采用固定内部命名规则，支持同一电脑上的多个独立 Client。可选编译及 Web 构建已实施，当前连接目标仍限制为本机回环。Web 以 C# 直接启动 MSBuild 子进程，实时显示编译输出。

## 编译边界

| 位置 | 实现边界 |
| --- | --- |
| `Source/KNSoft.ZPigeon.Client.SDK/Core/Request.c` | 模块执行器、函数表和请求路由使用所选宏；固定 Probe 保留，未选模块返回不支持 |
| `Source/KNSoft.ZPigeon.Client.SDK/Transport/Main.c` | 能力常量、适配器配置及 File、RTC、Recording、Execution、Snapshot、Child Session 生命周期按相同宏控制 |
| `Source/KNSoft.ZPigeon.Client.SDK/Client.inl` | 同时声明三种传输和模块状态；首版保留声明与布局，通过源码宏裁剪实际初始化、操作和清理 |
| `Source/Modules/Administration/Client.c` | 同一编译单元包含多个管理模块的 `.inl` 和函数表；只关闭外层路由不能移除其内部引用 |
| `Source/Modules/Execution` | 完整执行器包含 ShellExecute、运行时探测、其他用户及预设 Token、AppContainer、自定义 Token 等能力；Terminal 复用其中的进程启动代码 |
| Client 与 Client SDK 项目 | 无条件提供静态 KNSoft.Quic 链接输入；SDK 无条件生成 WinGet 投影，EXE 无条件复制 WinGet Bridge；需区分构建依赖和最终 Client 部署依赖；Child Session 宿主实现入口按 System Administration 选择裁剪 |

VS 2026 / MSBuild 18 完整解决方案 Release x64 的当前完整 Client 为 1,341,952 字节（约 1.28 MiB）；通过 Web 编译的 Execution 基础模块 + TCP 为 212,992 字节（208 KiB），不需要旁文件。静态 WebView2 Loader 使全量 EXE 比原动态 Loader 的 1,320,448 字节基线增加 21,504 字节（21 KiB），同时省去 164,704 字节的 Loader DLL。尺寸会随源码及工具链变化，后续比较需使用相同输入与安全选项。

## 功能与依赖

固定核心保留连接状态机、证书验证、CNG 实例身份、Frame 与公共 Codec、Request/Response、取消、配额、重连、探测和最小连接信息。核心不包含系统管理页面对应的完整模块。公共 Channel 基础设施及 Frame 消息边界处理保留，具体终端、传输和媒体等流式业务实现按模块宏裁剪；对无法处理的 Channel 请求返回明确错误。

业务选项以现有 ModuleId 为边界。Administration 内的用户、任务、硬件、软件、更新、防火墙等模块各自可选，共享固定记录 Codec 和必要 Builder。对实现型 `.inl` 引入和函数表条目使用条件编译，无需拆成大量新项目；独立 C/C++ 辅助源文件保留现有项目条目，函数实现按实际依赖使用宏守卫。

远程执行另外提供以下选项，避免“只选 Execution”仍包含其全部扩展：

| 选项 | 内容和依赖 |
| --- | --- |
| `execution.basic` | 当前 Client 身份和会话中的程序启动、参数与工作目录、作业状态、退出码、终止；可调用系统现有的 cmd 和 PowerShell，无需运行时扫描 |
| `execution.runtimeDiscovery` | 运行时枚举和映像查询；加入当前 `Runtime.cpp` 实现及相应依赖 |
| `execution.shellExecute` | ShellExecute 及 Verb；加入 Shell 相关实现 |
| `execution.alternateIdentity` | 其他会话、用户、SYSTEM、TrustedInstaller 等身份执行；加入会话和 Token 辅助实现 |
| `execution.appContainer` | AppContainer 执行；依赖进程启动及 AppContainer 安全能力辅助实现，不强制开启 AppContainer 管理页面 |
| `execution.customToken` | 自定义 Token 执行；加入相应解析、验证及 Token 创建路径 |
| `execution.staging` | 暂存目录及上传后执行；上传入口依赖所选 File 写入能力，不能在未编译 File 时显示可用的上传操作 |
| `terminal` | ConPTY 会话和实时输入输出；依赖进程启动核心与 Channel，不强制开启全部 Execution 扩展 |

现有 Execution 的基础契约是启动、作业状态和退出码；实时命令输入输出由 Terminal 提供。Web 的精简配置说明应明确这一区别，并允许用户在选择命令执行时额外勾选交互式终端。

共享辅助代码按依赖闭包编译，例如进程启动同时供 Execution 和 Terminal 使用。辅助代码存在不代表对应管理模块可用；能力声明只来自所选功能。现有 KNSoft.NDK 与 KNSoft.MakeLifeEasier 直接复用，本方案不要求修改共享库或增加替代实现。

## 传输复选框与安全策略

“编译支持的传输”使用三个独立复选框，至少选择一个。每项分别决定对应适配器实现和链接引用是否进入 Client。

| 复选框 | 编译宏 | 实现与平台要求 |
| --- | --- | --- |
| TCP（TLS） | `_KNSOFT_ZPIGEON_BUILD_TRANSPORT_TCP_` | Client TCP 适配及 Network TCP/TLS；当前 Schannel credential API 要求 Windows 10 1809 及以上 |
| UDP（DTLS 1.2） | `_KNSOFT_ZPIGEON_BUILD_TRANSPORT_UDP_` | Client UDP 适配、Network UDP/DTLS、确认、重传、排序与背压；当前 DTLS 路径要求 Windows 10 1607 及以上 |
| QUIC（TLS 1.3） | `_KNSOFT_ZPIGEON_BUILD_TRANSPORT_QUIC_` | Client QUIC 适配与静态 KNSoft.Quic；当前包使用 Schannel，要求 Windows 11 / Server 2022 及以上 |

TLS 1.3 是安全协议版本。QUIC 固定要求 TLS 1.3，UDP 当前实现使用 DTLS 1.2，TLS 1.3 不作为第四种 Transport。TCP 单独提供最低 TLS 版本策略：默认协商 TLS 1.2 或更高版本，另可选择“要求 TLS 1.3”。选择后者时目标必须支持 TLS 1.3，不降低要求继续连接。加密和服务端身份验证始终保留，不提供明文 TCP/UDP 开关。

上述范围由当前源码和平台决定：TCP 的 `SCH_CREDENTIALS` / `TLS_PARAMETERS` 要求见 [Microsoft credential 文档](https://learn.microsoft.com/en-us/windows/win32/api/schannel/ns-schannel-sch_credentials)；TLS 1.3 与 DTLS 1.2 支持范围见 [Schannel 支持矩阵](https://learn.microsoft.com/en-us/windows/win32/secauthn/protocols-in-tls-ssl--schannel-ssp-)。QUIC 必须使用 TLS 1.3 见 [RFC 9001 第 4.2 节](https://www.rfc-editor.org/rfc/rfc9001.html#section-4.2)，当前 Schannel 后端的平台要求见 [MsQuic Platforms](https://github.com/microsoft/msquic/blob/main/docs/Platforms.md)。当前包不使用 OpenSSL 后端。

“连接目标”另列有序 Endpoint，每项包含 Transport、Host、Port、ServerName 和相应 TLS 策略。Endpoint 必须属于已编译传输集合；非法配置明确拒绝。勾选多个传输表示 EXE 具有这些连接能力，同一 Client 仍只有一条活动连接，按显式 Endpoint 顺序尝试。两条并发 Server 控制连接使用两个独立 Client 实例。

Web/Native 默认启动本机 TCP/QUIC `21943` 和 UDP `21944` Listener，避免 QUIC 与 DTLS 占用同一 UDP 端口，名称固定为 `localhost`。Windows 10 宿主仅开放 TCP/UDP，生成器拒绝不能连接当前 Listener 的选择；覆盖 Windows 10 的精简基线使用 TCP/TLS 1.2。生成页显示固定地址、连接顺序与最低系统要求。

## 每个模块独立宏

业务宏使用 `_KNSOFT_ZPIGEON_BUILD_MODULE_<模块名>_`，每个用户可选功能对应一个宏。 定制构建同时定义 `_KNSOFT_ZPIGEON_BUILD_CUSTOM_`，未定义此标记的普通 Debug/Release 构建保留全部功能；Web 自动传入该标记。选中时定义为 `1`，未选中时完全不定义，源码统一使用 `#if defined(...)`。不使用定义为 `0` 的宏表示关闭，避免 `#ifdef` 仍进入实现。

例如同时选择终端、文件以及 TCP 和 QUIC：

```c
#define _KNSOFT_ZPIGEON_BUILD_CUSTOM_ 1
#define _KNSOFT_ZPIGEON_BUILD_MODULE_TERMINAL_ 1
#define _KNSOFT_ZPIGEON_BUILD_MODULE_FILE_ 1
#define _KNSOFT_ZPIGEON_BUILD_TRANSPORT_TCP_ 1
#define _KNSOFT_ZPIGEON_BUILD_TRANSPORT_QUIC_ 1
```

模块声明头文件无条件包含，宏守卫源文件中的实现。以 Terminal 的 `Client.c` 为例：

```c
#include "Client.h"

#if defined(_KNSOFT_ZPIGEON_BUILD_MODULE_TERMINAL_)
/* Terminal 函数、静态数据及实现所需的库引用。 */
#endif
```

未来引入 PCH 时，`#include "pch.h"` 位于源文件最前，宏守卫位于 PCH 之后。声明头文件不因是否启用模块而改变；实现型 `.inl`、函数表和库链接指令仍属于实现，在使用它们的源码中守卫。当前项目仅设置 `PrecompiledHeaderFile=pch.h`，尚未启用 PCH。

现有请求路由、初始化和清理入口同样置于相应宏内。首版保留内部类型与状态布局，减少头文件差异；未启用模块不创建资源，也没有执行与清理引用。20 个模块中的任意 n 项，均表示为对应 n 个宏；没有按组合新增命名构建配置的要求。“只选执行”“全部功能”等最多是 Web 的快捷勾选操作，提交的仍是实际选择集合。现有架构配置仍为 x64。

远程执行模块的基础宏为 `_KNSOFT_ZPIGEON_BUILD_MODULE_EXECUTION_`。运行时探测、ShellExecute、其他身份、AppContainer、自定义 Token 和暂存执行分别使用 `_KNSOFT_ZPIGEON_BUILD_EXECUTION_<能力>_`；这些扩展要求 Execution 模块存在。Terminal 可以独立选择，只引用必要的进程启动 helper，不因此开启 Execution 的公开请求入口。

每个宏同时控制函数实现、静态数据、请求路由、状态初始化、断连清理与退出清理。Administration 中的用户、任务、防火墙等功能各有独立宏，宏保护对应实现型 `.inl` 和函数表条目。只删除请求路由、保留完整函数表与清理引用，不能视为已经移除该模块。

## 直接通过 CL 传入宏

构建入口只负责把所选功能映射为固定宏名，设置本次构建子进程的 `CL` 环境变量，再调用完整解决方案。源文件继续保留在现有项目中；未选模块的实现被预处理器移除，不依赖 MSBuild 动态筛选源文件。不生成 `BuildFeatures.g.h`、`.props` 或功能组合对应的命名配置。

Web 构建入口直接使用 C# `ProcessStartInfo`。MSBuild 路径先通过直接启动 `vswhere.exe` 的进程解析，限制安装版本 `[18.0,19.0)` 并要求 C++ x64 工具链。核心调用如下，`macros` 只由固定功能清单产生：

```csharp
var start = new ProcessStartInfo(msbuildPath)
{
    UseShellExecute = false,
    CreateNoWindow = true,
    RedirectStandardOutput = true,
    RedirectStandardError = true,
    StandardOutputEncoding = Encoding.UTF8,
    StandardErrorEncoding = Encoding.UTF8,
    WorkingDirectory = snapshotSource
};
foreach (var argument in new[]
{
    Path.Combine(snapshotSource, "KNSoft.ZPigeon.slnx"),
    "/restore", "/t:Rebuild", "/m", "/p:Configuration=Release",
    "/p:Platform=x64", "/nr:false", "/nologo", "/v:minimal"
}) start.ArgumentList.Add(argument);
start.Environment["CL"] = string.Join(' ', macros.Select(macro => $"/D{macro}"));
start.Environment.Remove("_CL_");
```

不带值的 `/D宏名` 将宏定义为 `1`，适合统一的 `#if defined(...)` 判断。`CL` 由编译器读取，不需要把宏再次转换成 MSBuild 属性；用法见 [Microsoft CL 环境变量文档](https://learn.microsoft.com/en-us/cpp/build/reference/cl-environment-variables)。Web 使用每个构建子进程自己的环境，不修改服务进程全局环境，也不接受任意 `CL` 字符串。

当前 VS 2026 的 cl.exe 19.51.36260 已通过预处理验证：裸 `/D宏名`、显式 `/D宏名=1`，以及直接放入 `CL` 的 46 个模块宏（1,888 字符）均生效。Microsoft 环境变量文档仍给出 1024 字符限制；该描述不能用于断言当前工具链超过此长度就失败。需要缩短环境变量时，可把相同 `/D` 参数写入临时响应文件，使用 `CL=@"绝对路径\features.rsp"`，包含空格的路径同样已验证。响应文件只是编译器参数列表，没有头文件或 MSBuild 逻辑，见 [Microsoft 编译器响应文件文档](https://learn.microsoft.com/en-us/cpp/build/reference/cl-command-files)。

Web 维护功能 ID 到固定宏名、依赖说明和最低系统要求的对应关系，不维护源文件与 `.props` 生成规则。共用 helper 在源码中按依赖模块的宏并集启用，例如进程启动实现使用 Execution 或 Terminal 条件；依赖非法时由构建入口与 Client 源码明确拒绝。至少一种传输必须启用，Endpoint 必须属于所选传输集合。

项目改动遵循以下边界：

1. Client.exe、Client.SDK 和相关测试通过同一次构建继承相同 `CL`，源码中的实现、请求路由和所有清理路径使用一致的宏。
2. 共用 helper 按所选功能的依赖并集编译。Terminal 所需的 Channel 与进程启动代码不等于公开 Execution 功能；Recording 所需的捕获代码也不自动开放 Audio、Video、Window 管理入口。
3. `CL` 会传给整套解决方案的原生编译单元。Server SDK、Server Native 和共享 Protocol 不使用 Client 选择宏裁剪；两端共用的 `Network/*.c` 保持完整实现，Client 通过关闭传输适配器引用使静态链接丢弃未用网络代码。
4. 保留包还原、声明头文件所需投影和普通静态库链接输入。当前 KNSoft.Quic 没有强制载入整个静态库；未选 QUIC 时移除实现引用即可阻止 QUIC 对象进入 Client。当前生成器仍为选中的 WinGet 提供 WinGet Bridge DLL 和 `.winmd` 旁文件，不能直接打包整个 OutDir；`.winmd` 用于构建期投影，其分发策略待确认。RTC/P2P 的 WebView2 Loader 静态链接进 EXE，不分发 `WebView2Loader.dll`；目标系统仍需安装 WebView2 Runtime。
5. 构建服务串行处理不同组合，在每个任务的独立源码和依赖快照中执行完整解决方案 `/t:Rebuild`，随后保存模板缓存。每个任务的中间文件和输出已隔离。缓存不能共用不同宏集合的产物；相关测试按已编译能力选择用例。
6. 继续通过 `vswhere` 解析 VS 2026，以 MSBuild 18 构建整个 `Source/KNSoft.ZPigeon.slnx`，只保留现有 Debug/Release 和架构配置。

未来 PCH 的创建与使用必须保持相同宏选项，C 与 C++ 分别使用各自 PCH。切换宏组合时重建，不直接复用另一组合的 PCH，见 [Microsoft PCH 一致性规则](https://learn.microsoft.com/en-us/cpp/build/creating-precompiled-header-files)。保留声明头文件与构建依赖不代表相应实现会进入 EXE；是否真实裁剪由最终链接结果判断。

Release 保留 `/Gy`、静态库 `/Gw` 与 EXE `/OPT:REF,ICF`。函数表中的引用须先通过宏移除，随后链接器才有机会删除未引用实现。优化行为见 [Microsoft 链接优化文档](https://learn.microsoft.com/en-us/cpp/build/reference/opt-optimizations) 和 [全局数据优化文档](https://learn.microsoft.com/en-us/cpp/build/reference/gw-optimize-global-data)。尺寸验证必须同时检查 Map 和 PE 导入，不能只按宏数量判断 EXE 大小。

## 已认证能力声明

ClientHello 已原位加入模块、传输和 Execution 扩展位图及实例 GUID。所有兼容版本保持 1，不保留中间开发格式的解码分支，不引入模块独立版本。

ClientHello 使用固定宽度的已编译模块位图、传输位图和 Execution 扩展标志。46 个现有 ModuleId 使用 64 位模块位图即可；扩展标志只用于模块内部确实可单独裁剪的能力。解码时验证三组字段的保留位、至少一种传输以及 Execution 扩展对 Execution 模块的依赖；模块位图允许为零，固定核心不占业务模块位。依据实际编译宏生成位图常量，Client 不从部署配置或下载表单读取能力声明。

认证签名覆盖 Server Challenge 和完整规范编码的 ClientHello，使公钥、版本及能力属于同一次认证。Server 仅在验签成功后保存并向 Managed、Application、Tools 和 Web 暴露能力；能力描述构建内容，不替代授权或本机权限验证。

Web 隐藏不可用页面和子功能；外部 MCP 目录提供当前全部连接支持的工具并集，调用时显式指定 ClientId。直接 REST、SDK 或协议请求仍由 Server 检查，Client 对未编译模块或扩展统一返回 `STATUS_NOT_SUPPORTED`，不会因缺少实现而关闭健康连接。连接质量依赖固定核心探测和 Server 观测，不强制包含完整 ClientStatus 模块。

## 与 Web 生成器衔接

EXE 生成分为“选择编译模板”和“嵌入部署配置”。编译模板只包含实际模块宏和传输宏所对应的实现；分组、信任根、Endpoint、DeploymentId 与 InstanceId 在生成下载文件时写入部署配置。相同宏集合可以复用模板，无需每次修改连接信息都重新编译。

生成页面显示固定架构、可选功能和传输、实际地址与端口、`ServerName`、依赖说明、实际文件大小和构建状态。生成前展示最终有效配置；无法连接当前 Listener 的传输或地址不能显示为可用。

“分组名称”用于控制台组织；Deployment 的信任根和 Server 身份构成安全边界。第一阶段仅本机回环部署时，分组标签不能被解释成独立根密钥隔离。未来启用独立 Deployment 后，按分组选择根公钥、SNI 和其授权 Listener，沿用 `Design.md` 的离线根私钥与在线 Server 证书模型。

生成页不提供可编辑的 CNG 密钥名称，仅显示实例标识、部署根指纹和当前用户密钥作用域。具体命名与多实例行为见下一节。Client 私钥在目标运行机器的 CNG 中创建，EXE 不包含 Client、部署根或 Server 私钥。

按需编译使用固定 VS 2026 构建入口；C# 直接启动工具，标准输出及错误输出通过 SSE 显示。单个 worker 最多保留 4 个待处理任务，每个任务限时 20 分钟，可取消进程树。缓存键包含实际源码内容、Native 包内容 SHA-256、工具链版本和规范化宏集合；仅当前使用的六个包参与依赖快照。包内容哈希按路径、大小及修改时间清单复用，复制后的包再次校验内容。日志有界，任务及模板最多保留 32 项，清理不删除仍被保留任务引用的模板。构建日志与结果只向本机控制端提供。部署配置在缓存模板复制后写入，绝不覆盖共享模板。当前输出为未签名 EXE，缓存 manifest 记录原始模板的 SHA-256；部署配置写入后的下载文件尚未增加签名或结果哈希记录。

## 固定 CNG 命名与多个 Client 实例

内部名称固定为：

```text
KNSoft.ZPigeon.Client.<DeploymentId>.<InstanceId>
```

`DeploymentId` 是部署根公钥 SubjectPublicKeyInfo 的 SHA-256，使用 64 个十六进制字符。`InstanceId` 是生成一个新 Client 实例时分配的随机 128 位 GUID，使用 32 个十六进制字符。完整名称为 119 个字符，在当前 128 字符配置容量内；Child Session 沿用 `.SessionWorker` 角色后缀。

两项 ID 均为公开命名空间信息，不授予管理权限。当前用户 CNG 作用域继续保留，实例私钥只在运行机器创建并持久化。IP、端口、传输、分组和 Server 叶证书不参与命名；根证书在同一根公钥下续签也不会改变 DeploymentId。

部署配置保存生成器按根公钥与 InstanceId 派生的完整固定名称；Client 校验固定格式并解析实例 GUID，不接受任意自定义名称。更新 `.zpcfg` 与实例握手元数据时原位替换开发中的格式，兼容版本保持 1，不保留旧自定义密钥名配置的解码分支。实例元数据纳入握手签名，实际身份仍由 CNG 公钥指纹识别。

| 场景 | 实例身份行为 |
| --- | --- |
| 同一电脑、同一账户，分别生成两个 Client | 不同 InstanceId，不同 CNG 密钥和公钥指纹，分组与连接身份独立 |
| 两个 Client 分别连接两个独立部署 | DeploymentId 与 InstanceId 分隔身份；两条控制连接分别属于两个实例 |
| 同一 Client 重启、重连或切换传输 | 打开原名称对应的密钥，公钥指纹稳定 |
| 更新已有实例的模块或连接信息 | 保留原 InstanceId，继续使用原身份 |
| 同一 EXE 分发到不同电脑或不同 Windows 账户 | 各自 CNG 存储创建私钥，不分发同一把私钥 |
| 同一 EXE 在同一账户复制到另一个目录 | 仍属于同一实例；需要第二个独立 Client 时生成新实例 |

Web 的“新增实例”分配新 InstanceId；“更新已有实例”保留已选择实例的 ID。实例 ID 只在部署配置阶段写入，不影响模块模板缓存。已有实例 ID 可由服务端实例记录和已认证 Client 元数据保留，目标端无需额外维护安装目录与 ID 的状态文件。

同一实例在同一账户重复启动时，以实例名称对应的进程互斥对象保证一个运行进程，避免两条连接共用分组和身份却被误认为两个独立 Client。互斥对象按当前用户及实例作用域创建；子会话 Worker 具有独立角色名称。

多个 Endpoint 是一个实例的连接候选，不代表同时接受多个 Server 的并发控制。确需两个 Server 同时管理这台电脑时，生成并运行两个独立 Client 实例。若两个 Server 使用同一根公钥，它们属于同一信任部署；独立 InstanceId 仍可分隔客户端身份。

## 验证

VS 2026 / MSBuild 18 的 Release x64 实测如下。精简组合均通过完整解决方案 Rebuild、SDK Consumer 检查、工具目录检查及对应传输的认证/探测/断线重连集成测试；不在编译集合内的传输测试明确跳过。尺寸只计 EXE，所选功能的部署旁文件另行列出。

| 编译组合 | EXE 字节数 | EXE 大小 |
| --- | ---: | ---: |
| 全部功能与传输（静态 Loader） | 1,341,952 | 约 1.28 MiB |
| 全部功能与传输（原动态 Loader 基线） | 1,320,448 | 约 1.26 MiB |
| Execution 基础 + TCP | 212,992 | 208 KiB |
| RTC + TCP（静态 Loader） | 258,560 | 252.5 KiB |
| Terminal + TCP | 217,088 | 212 KiB |
| Recording + TCP | 252,416 | 246.5 KiB |
| Registry + Firewall + Firmware + NetworkStatus + UDP | 258,048 | 252 KiB |
| Execution 基础 + QUIC/TCP/UDP | 516,096 | 504 KiB |

独立 SDK 静态链接探针确认 Loader 已包含在合并后的 Client SDK 静态库中，导入表没有 `WebView2Loader.dll`。在系统 WebView2 Runtime 154.0.4258.62 上，Runtime 发现及创建 Environment 的回调均返回 `S_OK`。RTC + TCP 静态 Loader 变体的导入表没有 `WebView2Loader.dll`，未选 RTC 的 Execution 基础 + TCP 仍为 208 KiB。

Execution/Terminal 的 TCP 精简产物只导入七个核心系统 DLL，链接 Map 中没有 QUIC、UDP、RTC、WinGet、音频或视频实现。Web 已实际生成 Execution 基础 + TCP，PE 配置中的分组、连接、固定密钥名称和根证书与生成请求一致，实例记录在 Web 重启后保留。SDK 集成验证了程序启动及退出码；独立生成 EXE 的双实例同时运行与重复启动拦截尚未实机验收。

UDP 空闲连接在 Server 停止后按现有 60 秒接收超时检测断开，随后重连并再次探测成功；测试为这个阶段单独预留 65 秒，不改变业务超时。完整 Debug/Release 的全部传输集成测试也通过。中文编译日志通过直接 MSBuild 消息/警告/错误探针确认 UTF-8 解码及进程结束前的实时读取。

以下列出进一步覆盖不同平台及模块组合时的验收要求。

生成器路径为 `GET /api/clients/build` 获取清单、`POST /api/clients/build/jobs` 创建任务、任务 `/events` 显示日志、`/cancel` 取消、`/download` 下载 EXE；部署旁文件通过固定允许列表的 `/runtime/{name}` 下载。完成的生成实例按部署写入本地 JSON，更新实例可保留离线 ID。

全部功能和关键精简宏组合均需通过整个解决方案的 Release x64 构建。只选 Execution 时分别验证 TCP、UDP、QUIC 和三者同时启用；其他模块验证独立启用、关闭及实际依赖组合，并抽样覆盖任意多模块子集。集成检查包括认证、探测、执行和退出码、取消或终止、断线重连、关闭资源回收，以及请求未编译模块时的确定错误。

多实例验收包括同机同账户两个实例的不同公钥指纹、独立分组和连接状态，以及重启/更新后身份不变、同实例重复启动被拒绝。TLS 1.3 策略与 QUIC 在支持的平台上测试，Windows 10 验证 TCP/TLS 1.2 与 UDP/DTLS 1.2。

尺寸报告记录 EXE 字节数、完整部署文件总字节数、PE 导入表和链接 Map；仅 Execution + TCP 的 Map 中不得出现 QUIC、UDP、WinGet、WebView2、RTC、摄像头或音频执行器，部署目录不附带这些功能的 DLL。用禁用模块的符号和导入检查验证真实裁剪，并比较同传输条件下增加功能的体积变化。首个实际构建后再固定尺寸上限，避免把历史产物或估计值当成精简版承诺。
