# 远控画面与 WebRTC 媒体实验

## 生产画面路径

窗口与交互式远程桌面保留原生采集、JPEG 全帧、PNG 无损局部补丁、H.264/H.265 硬件编码，以及浏览器 WebCodecs 解码。没有把 Media Foundation 码流换成浏览器媒体轨。

`CaptureStream` 立即打开 Server 中转 WebSocket；中转打开后，后台尝试 RTC。当直连就绪时重新建立带 `directStreamId` 的画面流，关闭旧控制流并重置解码状态。新的采集订阅请求全量图像或视频关键帧，画布保留上次显示直到新帧绘制。P2P 协商失败保留中转；已使用的 P2P 失效或其控制 WebSocket 关闭时，自动重新打开一次中转，不进入重试循环。停止和路由切换先通过旧控制流释放按下的键和鼠标按钮。

RTC 的逻辑数据流保持可靠有序。分块是 16 KiB，各逻辑流按块共享发送机会；同一 StreamId 的 write 必须由调用方串行化。WebView2 排队预算根据当前选中路径实际发送字节的差值估算约 150 ms 数据量，限制为 16–128 KiB，另最多一块。该估计不是网络容量探测。发送 gate 和 bridge 等待支持流级取消，单次等待超过 1 秒终止 RTC session；调用成功表示桥接接受数据，接收/显示由画面 ACK 单独确认。浏览器本端 channel.open 与 native 处理 WebView OPEN 消息存在先后竞态，首帧通过既有 CanSend gate 等待就绪，不因 OPEN 消息迟到立即报断线；停止、取消和一秒等待期限保持不变。

## 显示反馈与画质

原生画面窗口最多 8 帧，通常限制为 2 MiB 在途数据；空窗口允许一个最多 16 MiB 的完整关键帧。窗口大小和最老在途帧年龄同时参考有效 FPS、最低 ACK 周期及当前传输/渲染平滑周期，避免旧的最低延迟在 RTT 稳定升高后制造持续积压。ACK 是累计显示进度，未知未来序号被拒绝，合法重复和落后序号不再次退休窗口。等待反馈最多 5 秒，关闭和取消立即唤醒。

JPEG/PNG 完成绘制后 ACK；视频完整解码参考链，每次 requestAnimationFrame 只绘制最新的已解码完整帧，关闭被替换的 VideoFrame，绘制后累计 ACK。待绘制帧仍计入 8 帧预算；隐藏页面关闭待绘制视频帧并发 0 ms ACK，避免后台暂停绘制造成假超时。PNG 补丁保持顺序绘制，不能任意丢弃补丁或已经编码的参考帧。浏览器限制排队与解码工作为 8 帧、待提交图像为 32 MiB；过量数据触发重新同步。解码错误、缺失参考帧、路由切换会重新建立完整图像或关键帧基线。每条原生流按自己的有效 FPS 取共享源的最新原始帧。

画面输入同样遵守 ChannelWindow 流控。SDK 将 Writable 通知沿 Native Bridge 转发到 Managed，SendAsync/UpdateAsync 串行提交输入；初始额度尚未到达或额度用尽时，等待真实 Writable 通知，不把 STATUS_RETRY 当作画面故障，也不使用定时盲重试。请求取消、Complete 和 Dispose 会唤醒等待额度与 sendLock 的调用；Dispose 等待正在进行的 native Send 退出后才关闭句柄。SDK、Native Bridge、Managed 和 Web 的开发期接口一起更新，版本保持 1。

画面策略不再用主连接的业务吞吐或绝对 RTT 档位降低分辨率和图像质量。自动预算最多 100 Mbps；手动速度档位的上限依次为 1.5、5、16、50、100 Mbps，且不会抬升调用方指定的更低上限。自适应比例作用于分辨率×FPS×质量估算与用户上限两者的较小值，避免只降低未使用的 100 Mbps 上限而实际码率不变。Native 按新 ACK 且至少一秒采样；连续两次积压降档，连续三次窗口健康缓慢恢复并更新延迟基线。浏览器报告接收至绘制耗时，Native 从 ACK 周期饱和扣除该耗时，以区分渲染压力与传输/排队压力；扣除后的周期不是纯网络 RTT。网络压力先按 3/4 降实际视频预算，预算比例降至 50% 后再降 FPS；渲染压力和图像积压先降 FPS。分辨率与图像质量继续由用户选项决定。Server 中转队列限制为 128 个 chunk 和 32 MiB 已持有 payload；发送中的 OwnedBuffer 仍占预算，释放时只归还一次。

Options 输入消息仍使用版本 1 原地定义：类型 5、最大边长 UInt16、FPS UInt8、质量 UInt8、编码位域 UInt8、视频预算 UInt32，总长 10 字节，整数采用小端。Frame ACK 原地更新为类型 4、标志 UInt8、累计序号 UInt32、接收至绘制耗时 UInt16，共 8 字节；实际绘制至少 1 ms，恢复/跳过绘制使用 0 ms，不更新渲染估计。Server、Client 和 Web 必须一起更新，兼容版本保持 1。

自动图像/视频切换参考实际脏区域联合面积和图像编码字节成本，持续高变化或图像成本超过估算视频预算至少 500 ms 后进入视频，连续低变化至少 3 秒后退出；无新画面同样计入静止时间。未缩放的 PNG 补丁只读回对应 GPU 区域。编码器在设置输出类型前请求正确的低延迟 BOOL 属性、零 B 帧、CBR 和 GOP 大小，并检查结果；动态码率使用 ICodecAPI，仅在属性未实现或不可修改时重建。属性不可更新时，自动变化至少 25% 且距上次更新至少 3 秒才重建；用户下调硬上限时立即处理，避免小幅恢复不断冷启动。关键帧恢复优先原地请求，并按实际时间请求约 2 秒的刷新。恢复时屏蔽新关键帧之前的 delta，保留完整参考链。异步编码器单次 API 最多返回一帧并保留其余可取输出，画面静止时继续非阻塞取出最后的输出，有待取输出时最多等待 10 ms，空闲时最多等待 100 ms；原始帧限速不会延迟已经编码好的输出。异步输出格式变化后等待下一份 HaveOutput 事件，避免无输出许可的重复调用。等待异步输入事件改为非阻塞 GetEvent、最多 5 ms 取消等待、2 秒期限；期限不能抢占驱动内部 ProcessInput、ProcessOutput 或 SetValue 的 COM 调用。

悬停画面状态可查看实际接收帧率、平均输入码率、接收至绘制 P95 和待处理帧数。P95 只覆盖浏览器本地接收至绘制，样本最多 120 个；不是跨机器采集年龄或键鼠输入延迟。

## STUN 生命周期

内置 STUN 仍是 IP 放行，不是逐会话凭证认证。每份授权独立持有、幂等释放，最长 60 秒；过期权限即使尚未被定时清理也立即拒绝访问。Offer、Answer 和通道建立共同受 60 秒协商期限约束，Answer 发出后收回用于候选收集的 STUN lease。迟到的 native open 仍被观察并清理，避免超时后遗留会话。

Binding 报文检查头、长度及属性 TLV 边界，保留每 IP 64 次/秒限制。同一 NAT 出口共享 IP，HTTP 与 STUN 的出口不一致仍会影响授权。当前未增加 TURN、短期 TURN 凭证或公网身份边界；已有 `P2p:IceServers` 可指定外部 STUN URL。媒体轨中转和 TURN allocation 的会话释放需要在选择生产媒体架构后单独实现。

## 视频轨实验

运行 Web 后打开 `/rtc-media-lab.html`，或从 Web 交互式远控标题栏点击“视频轨实验”。实验建立本机两个 PeerConnection，使用真实 RTP 视频轨 API，但不连接现有 Client，不代表两机网络性能。

来源可以是明确选择的桌面/窗口，或无需采集桌面的合成图。可比较直接采集轨和 RGBA 缓冲区物化成 VideoFrame 的生成轨，并调整 `text/detail/motion`、退化偏好、编码器、码率和 FPS 上限。RGBA 模式测浏览器内原始帧读取与创建成本，未接入 native IPC、GPU texture 共享或 MF 编码器。

建连最多 30 秒，连接后实验最多 300 秒。导出 JSON 记录参数变化、实际编码器、码率、帧率、网络与编解码统计、RGBA 数据量和 P95，不包含画面。每秒统计最多 300 条，帧样本最多 6,000 个。仅在浏览器提供 captureTime 时记录采集到预期呈现估计；不提供的统计项不填默认值。停止、分享源结束或页面关闭会释放轨和连接。

在本机/两机实际测试后再决定是否迁移生产路径。比较中文小字、代码、滚动、拖动、连续视频和静止场景，覆盖 1080p/4K、RTT、丢包及带宽突降。记录相同文字可读性下的实际流量、P95 响应时延、队列年龄、CPU/GPU 和内存。实验 API 合约测试不能替代真实 RTP、硬件编码和弱网测量。

## 回归验证

使用 vswhere 选择 Visual Studio 2026，运行其 MSBuild\Current\Bin\MSBuild.exe，对 `Source/KNSoft.ZPigeon.slnx` 分别构建 x64 Debug 和 Release。不要拆分 native 与 managed 或改用 MSBuild 17。

- `node --test Source/CaptureFramesTest.mjs Source/CaptureStreamTest.mjs Source/RtcDirectTest.mjs Source/RtcMediaLabTest.mjs`
- `node Source/I18nTest.mjs`
- `UnitTest.exe -Run`、`ConsumerTest.exe`、`ToolCatalogTest.exe`
- 合成 NV12 的 `WindowMediaH264` / `WindowMediaH265` 验证硬件编码、码率更新、关键帧恢复，硬件不存在时明确跳过；不采集桌面。

测试包含取消与迟到回调、P2P 恢复、中转先起播、累计 ACK、序号回绕、解码积压、STUN 独立 lease/到期/限流/IPv4/IPv6、实际 UDP Binding、手动预算及高 RTT 保分辨率。性能结论应附独立运行结果，不能从源码或 mock 测试推断收益百分比。

2026-10-15 本机回归结果：

| 检查 | 结果 |
| --- | --- |
| Visual Studio 2026 / MSBuild 18 全解决方案 x64 Debug、Release | 均构建成功，0 警告、0 错误 |
| 原生全量测试 | Debug 807/807、Release 806/806；均 22 个入口，无失败或跳过 |
| H.264 / H.265 合成 NV12 硬件测试 | 两种配置、两个编码各 105 项断言通过；40/40 帧、5 个关键帧、2 次码率重建；取消/超时后同一编码器恢复 |
| ConsumerTest | Debug、Release 均成功 |
| ToolCatalogTest | Debug、Release 均成功，含真实 UDP STUN、策略、中转缓冲预算、端口和 Client 模板测试 |
| 画面 / RTC / 实验页面 Node 合约与回归测试 | 41/41 通过 |
| Edge 155 生产 WebCodecs 解码 | 实际 MF H.264/H.265 码流各 40/40 绘制与 8 字节 ACK；最后序号 40，无恢复或异常 |
| I18nTest | 1007 项 key、2638 项 source、155 项 dynamic、47 项 Native/Managed，通过 |
| 改动格式 | git diff --check、媒体原生与托管文件编码/CRLF/120 列检查通过 |

## 本机验证限制

本机合成 NV12 测试在 Debug 和 Release 中都确认 H.264/H.265 各 40 个输入全部取回 40 帧、各 5 个关键帧；最后输入停止后仍能收齐输出，两次码率重建后恢复关键帧。H.264 码流检查确认无 B slice、具有 IDR；H.265 检查确认 IRAP，未解析 HEVC slice 类型来证明其无 B slice。所用硬件 MFT 在运行中把码率属性报告为只读（0x80070032），因此本机验证的是明确重建恢复，并未证明这个驱动支持原地动态码率。

Edge 155 真实 RTP 合成画面测试建立本机两个 PeerConnection，每个配置运行 10 秒。720p/detail/H.264/5 Mbps 上限下，直接视频轨与 RGBA 生成轨均约 30 FPS；RGBA 物化 P95 为 9.3 ms、最后样本原始数据量约 110 MB/s。4K 相同设置下，直接轨约 29.94 FPS，RGBA 约 14.79 FPS，RGBA 物化 P95 为 66.8 ms、最后样本约 491 MB/s。4K 采集至呈现 P95 受启动与不同帧数影响，不据此认定 RGBA 延迟更低。这些结果支持保留已有原生 GPU/硬件编码路径；没有新增 native libwebrtc。

原生硬件测试可通过 `ZP_WINDOW_MEDIA_ARTIFACT_DIR` 指定已存在的外部目录导出带生产 21 字节 header 的合成码流。真实 Edge 测试将每帧拆成三段交给当前 CaptureFrameDecoder，两种编码各 40 帧全部绘制、ACK，最后帧取回，接收至绘制 P95 分别 17.4/17.9 ms。这验证生产解码器与 MF 码流互通；128×128 合成输入不代表完整桌面性能。

Hyper-V Windows 11 Build 28000、真实交互 Session 2 的 Client 采集 1282×733 合成窗口，Edge 155 生产解码/显示/8 字节 ACK 路径持续工作：正常 18 秒显示 311 帧，随后 2 Mbps 浏览器接收节流 18 秒仍显示 29 帧，夹具排队峰值约 1.3 MiB。本次配置 captureMode=0 强制图像，实际帧均为 JPEG；不能据此判断该 VM 是否提供视频编码能力，也不能证明 GPU 硬件采集吞吐。

该轮 P2P 路由仅维持约 6 ms，未证明直连帧传输。原夹具解除限速时让新字节越过旧 FIFO，导致后续重同步/超时；已修正夹具，不能把这次超时归为生产中转故障。首帧 OPEN 竞态的最小代码修复已构建并通过本机回归，但修正夹具后的完整 P2P/恢复复测命令返回 `blocked by policy`，6 ms 退回的具体原因尚未实测确认。该通用返回没有提供具体策略来源，不能直接归因为自动审批或 Edge 策略。

目前没有跨机真实丢包、RTT 注入或 native→JS 原始帧 IPC 测量；RGBA 实验仅覆盖浏览器内物化。虚拟机接收节流也不是实际网络丢包。首次宿主机完整 Client 启动命令同样返回策略拒绝，没有通过提权绕过。CUA 在无法可靠识别浏览器 URL 后结束，未修改 Edge 策略。性能结论不宣称压缩或延迟提升百分比。

## 更新 App 后的启动复测

2026-10-15 23:26（Asia/Shanghai）后重试，functions.exec 内 tools.exec_command 已成功启动宿主隔离 Web、TLS/TCP Client 和 Headless Edge，均返回 exit_code=0；Hyper-V 的正常提权、Guest 部署和真实交互 Session 建立也成功。本轮未调用 CUA，因此没有验证其 URL 识别问题是否恢复。

宿主捕获测试实际执行后发现初始输入额度竞态：OpenWindowCaptureAsync 完成后立即 Update，peer 的 ChannelWindow 尚未到达时返回 STATUS_RETRY（0xC000022D）。这是产品流控状态，不是工具策略错误；现已接通 Writable 通知并改用异步发送，完整链路仍需重测。隐藏启动合成 Form 也会使其不可见，夹具已用首个 timer tick 显式显示自己的 Form，并独立确认 flags39。

本轮再次出现的策略拒绝是准备 Guest 更新脚本的命令：tools.exec_command 在 CreateProcess 前返回 `rejected: blocked by policy`；update-web.ps1 没有写入，也没有执行 Guest 更新。完整原始命令参数和被截断的原始异常保存在本机 Staging/remote-media-retry-20261015/policy-denied-call.json。该动作已停止，没有通过其他工具重建或执行。此时尚无本轮 P2P/恢复通过结果。

受控 UDP 损伤夹具仅完成独立随机 loopback 端口自测，覆盖双向转发、延迟、丢包、动态限速、队列上限及来源锁定；没有启动真实 WebRTC 损伤测试。该夹具的准备状态不能作为生产弱网或跨 NAT 测试结果。

输入 credit 修复后的本机回归：VS2026/MSBuild18 完整解决方案 Debug/Release 均成功；原生全量分别 807/807、806/806；ConsumerTest 与 ToolCatalogTest 两种配置均通过，后者新增初始 Update 等待额度、ACK 顺序、请求取消、Complete/Dispose 唤醒 credit 与发送锁等待的验证；浏览器回归 41/41、I18n 与格式检查通过。尚未部署修复到 VM 或取得修复后的生产 P2P 通过结果。拒绝后已恢复本轮精确检查点、VM Off、删除临时凭据，并关闭宿主测试进程、删除本次 CNG key 和测试私钥证书。

## RustDesk 参考与取舍

本轮重新阅读 `E:/3rdRepo/rustdesk` 的 `e910029f5`：`src/server/video_qos.rs` 的实际 bitrate 调整与 FPS 退让、`video_service.rs` 的确认期限与避免编码器反复冷启动，以及 `flutter/lib/models/web_video_frame_queue.dart` 保留最新已解码帧的做法。上述策略应用于现有 GPU/MF/WebCodecs 路径；没有复制 RustDesk 源码或引入其运行时。

RustDesk 的默认发送确认不总是实际绘制确认：`connection.rs` 中未启用 video_ack_required 时，notify_video_frame_fetched 在 await send 之前发生；另有显式 VideoReceived 分支。ZPigeon 保留实际绘制 ACK 与有界在途窗口，不能把两者的默认 ACK 含义混同。WebView Runtime 的 DataChannel 提供 SCTP 流控，但不会替应用上的 MF 码流自动实施 RTP 的编码码率、丢帧或视频拥塞控制；直接迁移需要解决原始帧进入视频轨的成本。

## 2026-10-16 图像传输崩溃修复

本轮优先修复真实传图中的 Client 崩溃，暂缓其余画质、P2P 和弱网优化。输入 credit 修复部署到 Windows 11 Build 28000 的交互 Session 2 后，Client 成功采集 1264×663 合成窗口；浏览器实际绘制并反馈 166 个 JPEG 帧，随后 Client 发生 0xC0000005。Application Error 1000 和 WER 记录的故障偏移均为 0x2260CD。使用该次 Client 的原始 EXE/PDB 离线解析，定位到 ZpQuic_SendFrame + 0x1CD，Source/Network/Quic.inl:84 的发送时间戳读取。

MsQuicStreamSend 提交成功后，完成回调可能已经执行 ZpQuic_CompleteSend 并释放整个发送 allocation；原代码随后读取 Send->EnqueuedTickCount，形成释放后访问。现在把时间戳保存在 ZpQuic_SendFrame 的栈上，成功提交后不再读取 Send。失败路径仍归还额度并释放一次，成功路径仍由完成回调释放；Client 和 Server 共用此修复，不改变线上帧格式，也未增加锁、引用计数或运行时依赖。

NetworkConnection 新增正常延后完成、提交返回前完成、同步提交失败三种回归。测试在真实完成处理释放 context 时写入失效标记，并推迟实际 free，稳定验证旧代码的错误访问，不依赖堆内存偶然内容。旧实现实跑 89/90，仅最大发送排队延迟断言失败；修复后 Debug、Release 均为 90/90。

| 修复后检查 | 结果 |
| --- | --- |
| VS2026 / MSBuild 18 全解决方案 x64 Debug、Release | 均成功，构建日志无警告或错误 |
| 原生全量测试 | Debug 837/837、Release 836/836；均 22 个入口，无失败或跳过 |
| SDKQuicIntegration | 两种配置均通过真实本机 QUIC 连接回归 |
| ConsumerTest、ToolCatalogTest | 两种配置均通过 |
| 源码格式 | git diff --check 通过；Quic.inl、Network.c 保留 UTF8-BOM / CRLF |

本轮另对既有输入 credit 修复完成 Debug 500 次四场景循环，以及 Debug、Release 各 200 轮×32 writer 的并发额度、取消、关闭测试，共 12,800 次并发发送操作，无挂起或意外错误。这些结果验证输入流控与生命周期，不替代真实画面采集。

一次旧 Client 的真实传图重复试验完成了正常接收、FIFO 接收节流与恢复，累计显示 399 帧且进程仍存活；该轮未再次触发这个间歇性崩溃，不能视为新修复的验证。浏览器先前显示的 0x80410006 是 QUIC CONNECTION_TIMEOUT，在 Client 崩溃后才出现；已有证据不支持把它作为崩溃原因。

修复后的连续传图、反复启停采集和虚拟机验证尚未完成。负责 VM 复测的 collaboration 子任务返回 `This content was flagged for possible cybersecurity risk` 并结束，没有执行修复后复测；这是子任务错误，不是 Windows 返回值。此前宿主隔离 Web/Client/合成窗口启动调用由 functions.exec 内 tools.exec_command 在 CreateProcess 前返回 `rejected: blocked by policy`，输入命令完整，错误显示被截断。这两项均未通过换工具、包装命令或提权重试。

VM 已恢复本轮精确检查点并关机，删除本轮检查点，保留原有两个检查点；只读检查确认宿主私有 RDP 夹具已退出。随后删除本轮 DPAPI 临时凭据并写停止标记的清理命令也由 tools.exec_command 在 CreateProcess 前返回 `rejected: blocked by policy`，未执行，因此 RemoteMediaTest20261016-002210.xml 仍留在本机 lab Credentials 目录。完整清理调用与返回摘要保存在 Staging/remote-media-test-20261016-host/cleanup-policy-denied-call.json。

崩溃符号定位、旧码失败与修复后通过日志、完整构建/回归日志保存在本机 Staging/remote-media-test-20261016-host；旧版 EXE/PDB、传图记录和 VM 恢复结果保存在 Staging/remote-media-test-20261016-002210。上述证据只支持已定位的发送 context 生命周期修复，不宣称修复后长时间跨机传图已通过。
## 2026-10-16 连续采集与启停实测

本节是上述 QUIC 修复之后的新结果。使用 Hyper-V Windows 11 Build 28000、Edge 154.0.4258.48、真实交互 Session 2 的普通 Medium Client；Web、Client 和浏览器均在同一 Guest 内，通过生产中转、CaptureStream、CaptureFrameDecoder 和显示 ACK 路径测试。captureMode=0 强制图像模式，不代表 H.264/H.265、P2P、跨机或弱网验证。

夹具使用持续重绘的可见窗口，并按实际 Canvas 绘制后的序号关联成功发送的显示 ACK。正常停止后核验 socket 已关闭，画布、绘制数和 ACK 不再变化，解码队列和待处理字节归零；下一次采集不能改写前一次状态。立即取消允许建连中的 WebSocket 报错及 1006 关闭，不把这种预期取消算作解码错误。后续桌面重测增加了源窗口重绘心跳断言。

| 被测版本与阶段 | 实际结果 |
| --- | --- |
| QUIC 修复版，窗口连续采集 | 约 5 分钟，4058 帧实际绘制及 4058 次显示 ACK |
| 同版首次桌面采集 | 1484 帧后触发超过 5 秒无绘制；Client 与 Web 存活，未完成该阶段 |
| 同版刷新源窗口后的桌面重测 | 约 2 分钟，1922 帧绘制及 ACK |
| 同版正常启停 | 30/30 次，窗口、桌面各 15 次；355 帧绘制及 ACK |
| 同版立即取消及恢复 | 10/10 次立即取消，均 0 帧；随后新采集 20 帧绘制及 ACK |
| 增加 WGC Close 后的窗口、桌面连续采集 | 约 5 分钟 4948 帧、约 2 分钟 1996 帧，绘制与 ACK 数一致 |
| 同版 100 次正常启停计划 | 仅完成 3 次，第 4 次 Client 崩溃；未进入取消阶段 |
| 同版独立启停再现 | 完成 13 次，第 14 次 Client 再次崩溃，取得完整进程转储 |

上述约 5 分钟和约 2 分钟阶段的首帧至末帧跨度分别大于 300 秒和 120 秒；JSON 中 elapsed 还包含建连、首帧等待和停止核验，不能直接当作纯绘制时长。首次桌面停滞的直接原因未确定：刷新可见的动态源后重测通过，不足以证明原停滞完全来自夹具。

功能通过不代表资源稳定。同一 QUIC 修复版 Client PID 8220 在连续桌面末段约为 20.75 MiB private bytes、592 handles、16 threads；30 次启停及 10 次立即取消结束后的采样仍为 144.22 MiB、1973 handles、55 threads，过程中随启停持续增长。该轮不能报告为无泄漏。

因此补上 GraphicsCaptureSession 和 Direct3D11CaptureFramePool 的 IClosable::Close：先移除 FrameArrived，再关闭 session 和 frame pool，最后释放 COM 引用及 handler。此前只有 Release，缺少显式资源关闭。修改后的连续传图通过，但启停暴露了另一处真实崩溃，尚不能宣称资源增长已经解决。

两次新崩溃均为 0xC0000005，Application Error/WER 标识 GraphicsCapture.dll_unloaded。完整转储进一步确认执行地址是已卸载的 GraphicsCapture.dll + 0x1143f，与原先 ZpQuic_SendFrame 的发送 context 释放后访问不同。反复启停时采集源线程各自 RoInitialize/RoUninitialize，最后一个 MTA 线程离开会拆除共享运行时；DLL 卸载证据支持这一寿命方向，但新修复尚缺实际复测，不能据此宣布完整根因已验证。

当前最小修正是在启用 Window 或 Recording 的 SDK Client 对象创建时 CoIncrementMTAUsage，最终关闭并完成模块清理后 CoDecrementMTAUsage，使 MTA 支持跨越采集 worker 的间隔。采集线程自身仍保留成对 RoInitialize/RoUninitialize，不增加常驻自建线程或原生 libwebrtc。Microsoft 的 API 合约允许跨线程释放成功取得的 cookie，并保持 MTA 在没有 MTA 线程时仍存活：[CoIncrementMTAUsage](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coincrementmtausage)。

另外修正 Window capture worker 的退出计数顺序：原先 CallbackCount-- 后还会调用 ReleaseInput 并访问 Owner->Lock，最终 Client Close 可能在两者之间释放 Owner。现在 ReleaseInput 完成后才持锁减少 CallbackCount，随后只释放 channel 引用。此问题由代码审查确认，未把它归因为已取得的 GraphicsCapture.dll 崩溃。独立审查还确认最后一个共享采集的关闭会等待 source thread 完成 ZpWindowCapture_Close；最终 MTA cookie 释放不会早于采集 COM 清理。

最终源码的 VS2026 / MSBuild 18 完整解决方案 x64 Debug、Release 均构建成功，日志无警告或错误；原生全量分别 837/837、836/836，均 22 个入口、0 失败、0 跳过。ConsumerTest、ToolCatalogTest 两种配置均通过。git diff --check 以及本轮原生文件 UTF8-BOM、CRLF、120 列检查通过。这些本机测试不能替代 WGC 真实反复启停。

新 MTA 版本的 Guest 部署/100 次启停测试调用由 functions.exec → tools.exec_command 在 CreateProcess 前返回 rejected: blocked by policy，脚本没有写入、进程没有启动。错误显示包含 8345 chars truncated，但返回没有指出具体策略，不能认定截断是拒绝原因；没有换工具或提权重试这一操作。因此 MTA 与 worker 收尾修复后的真实连续采集、100 次启停、取消恢复，以及启停后资源回落仍待验证。

本轮证据位于本机 Staging/remote-media-stability-20261016-024500 和 Staging/remote-media-lifecycle-20261016-032200，包括各阶段 JSON、浏览器截图、进程资源采样、close-only 原始 EXE/PDB、108 MB 转储及离线分析、本机最终构建/测试日志、mta-deployment-policy-denied.json。最终 Debug Client SHA256 为 706E36AE1A553433D758B415E27F269480986646A42A2258F072ABD19FDD1018；已实测的 close-only Client 为 3350FB63B9E808F8DBBD59D144F72C37AC1D4F17DEDCEEA9802473AC69701D00，两者不可混用验证结论。

已恢复本轮精确检查点 88711423-5e45-4b1b-b9bc-4fc5576bcee9、VM Off，删除本轮检查点且保留原有两个；本轮私有 RDP 夹具已退出，RemoteMediaLifecycle20261016-032200.xml 已删除。更早两轮被拒绝清理的 DPAPI 凭据没有再次尝试删除，保留各自拒绝记录。
## 2026-10-16 最终复核与再次部署

本轮继续按轻量原生采集 / Media Foundation / WebCodecs / DataChannel 方案完善实现，未引入 native libwebrtc，未修改协议兼容版本或公共库包。子任务仅获得具体媒体文件和测试目录边界。最终源码仍有下述真实运行验证缺口，不将“全部完成”作为本轮结论。

新增的确定修正：

- Capture 内新增私有 ResourceLock，使源尺寸变化时释放 Staging、VideoProcessor、VideoEnumerator 等资源，与图像编码、NV12 sample 转换互斥。Shared 仍用原 ConvertLock 保护缓存；锁顺序是 Source lock → ConvertLock → ResourceLock，采集线程先释放 ResourceLock 后才取得 Source lock 发布帧。WGC 等待帧和 GDI 帧率等待都在资源锁外，避免空闲时持锁一秒。
- EncodeFrame / CreateSample 在资源锁内拒绝尺寸已过期的帧，返回 S_FALSE 且不写输出；现有调用链会跳过该结果，避免旧纹理使用新尺寸读回。MaxDimension 变化时同时释放旧 SampleAllocator，后续按新尺寸重建。
- Shared_Open 检查 NtResumeThread 失败，终止仍挂起的线程、发布失败与 ReadyEvent 后统一清理，避免永等就绪。
- 浏览器实际绘制 ACK 钳制为 1–65535 ms；恢复与跳过绘制仍使用 0 ms。图像队列归还计数后才上报统计，最后一张图处理结束后 pendingFrames 正确为 0。两条新增回归在旧实现上分别稳定得到错误值 [0,0] 和 [2,1]，修正后通过。

独立只读复核确认上述锁顺序没有发现新增逆序；旧尺寸 S_FALSE 的图像、视频及同步截图调用点没有读取未写输出。该审查不能证明驱动内部 COM 调用有界，也不能替代真实 WGC 尺寸变化和长时间启停。

| 本轮最终源码检查 | 结果 |
| --- | --- |
| VS2026 / MSBuild 18 完整解决方案 x64 Debug、Release | 均成功，日志无 warning / error |
| 原生全量 | Debug 837/837、Release 836/836；均 22 个入口，无失败或跳过 |
| ConsumerTest、ToolCatalogTest | 两种配置全部通过 |
| 浏览器 CaptureFrames / CaptureStream / RtcDirect / RtcMediaLab 回归 | 43/43，通过 |
| I18N | 1007 keys、2638 source、155 dynamic、47 Native/Managed，通过 |
| 本轮源码格式 | git diff --check、原生 BOM、原有 CRLF、120 列检查通过 |

最终 Debug Client SHA256 为 AB8A49C2EE5CC606EEC2FE03A13B779173A2EECE45226E5EF7CD07C27B339B77，Release 为 9EFB62999BB03A05F5B842225561DAE05CE91FC2055E2E9A4EEA26141A2C1F35。本轮在新检查点 5cf01bf1-a2a9-4f44-967a-d03ce4cf9fc8 下成功启动 VM；Guest 的计划内更新重启第一次中断账户创建，随后正常恢复。最终 Debug EXE 已复制并按 hash 验证到 Guest，隔离 Web PID 2084 在 Session 0 启动，私有 RDP 交互会话建立成功。没有启动最终 Client 采集验收，不能报告该版本的交互 Client token、帧数或资源回落结果。

最终连续测试夹具已准备：窗口与桌面热身后静置；实际首帧至末帧跨度至少 300 秒 / 120 秒；30 秒窗口反复缩放；100 次交替启停；10 次立即取消；取消后窗口和桌面分别恢复至少 20 帧；最后再次静置。夹具按实际绘制关联成功发送的显示 ACK，输出真实 Canvas PNG，冻结旧流与队列归零断言保留。源命令与心跳使用本轮专用文件名，并显式要求传入真实交互源用户的 LocalAppData。心跳 File.Replace 使用有效 .bak 路径，避免 PowerShell 5 将 null 备份参数绑定为空字符串。

受控网络夹具也已准备，并仅做语法检查：通过测试页内的 PeerConnection 装饰重写候选，两个临时 UDP 端口在本机 Guest 内转发真实 DataChannel 报文；要求选中 ICE 端口和实际代理收发计数证明路径。预设单向 50 ms、1% 丢包、5 Mbps 后降至 1.5 Mbps、恢复及断开 P2P 后回落中转阶段。该夹具未实际运行，不能报告丢包、RTT、带宽突降、P2P 恢复或 WAN 性能通过。

本轮两项工具拒绝均发生在 CreateProcess 前：

| 调用 | 返回与影响 |
| --- | --- |
| 子任务 tools.exec_command 写入 run-lifecycle.ps1 自动采样脚本 | rejected: blocked by policy；显示 17673 chars truncated；文件未创建 |
| 主线程 functions.exec → tools.exec_command 写入 launch-capture-test.ps1 | rejected: blocked by policy；显示 4452 chars truncated；文件未创建，采集验收未启动 |

返回未提供具体策略或规则，不能归因为 UAC、Edge 策略或命令截断。没有通过其他工具、编码、拆分或提权重建这些被拒绝的脚本。自动资源采样与最终实际采集启动因此仍缺失；既有历史实测不得用于替代最终二进制的验收。

本轮构建、测试、准备夹具、hash、部署与拒绝元数据保存在 Staging/remote-media-final-20261016-035800。VM 已恢复精确检查点并关机、删除本轮检查点，原有两个检查点保留；私有 RDP PID 11500 已退出，本轮 DPAPI 临时凭据已删除。打包夹具的静态副本已更新，生产最终 EXE / Web 模块 hash 保留。未改动宿主既有服务或工作区的其他改动，未提交 Git。宿主 powercfg /a 确认可休眠，但“全部优化及测试完成”条件尚未满足，未执行休眠。

## 2026-11-12：TCP 双向收发锁死与浏览器测试页修正

Debug 现场 Client PID 30052 的转储确认 TCP 锁序反转：接收线程持有 TCP critical section，处理画面通道额度通知时等待 Client Owner 锁；采集发送线程持有 Owner 锁，发送画面时等待同一个 TCP critical section。其余业务请求随后也阻塞在 Owner 锁。原实例的系统信息请求在约 10143 ms 后返回 HTTP 500 / NtStatus 0xC00000B5，符合用户报告的“鼠标后画面停止、刷新后数据超时”。输入不是单独的崩溃根因。

最小修正在 Network/Tcp.c：上层 Connected、Receive 回调前释放 TCP 锁，回调后重新获取。TLS 状态处理与发送仍受锁保护；当前接收 IO 引用覆盖回调寿命，下一个接收只在当前处理完成后提交。没有新增队列、线程、拷贝或超时回退。

SDKTcpIntegration 新增跨线程 TryEnterCriticalSection 检查，分别覆盖握手成功与认证接收回调。保留旧 TCP 实现时新增断言失败，修正后通过。完整解决方案经 VS2026 / MSBuild 18 构建 x64 Debug、Release 成功；原生全量分别 838/838、837/837，均 22 个入口、无失败或跳过；两种配置的 ConsumerTest、ToolCatalogTest 均通过。两个本轮 C 文件保留 UTF8-BOM、CRLF，最长行不超过 120 列，diff --check 通过。

用户手动启动最终 Debug Client PID 3016、Web PID 19636，API 确认同一连接始终为 TCP（transport=2）。本机 Edge 157.0.4341.42 实测：

| 检查 | 结果 |
| --- | --- |
| 两轮约 25 秒桌面采集 | 实际绘制 261 / 252 帧，成功发送的对应显示 ACK 同为 261 / 252 |
| 远程鼠标输入 | 成功发送 550 个鼠标包，临时测试窗口累计收到 50 次左键按下；仅在用户要求不干扰鼠标之前执行 |
| 传图期间系统信息、窗口列表并发请求 | 100/100 HTTP 200，最长约 35.8 ms，无 0xC00000B5 |
| 随后正常采集启停 | 10/10，每次确认首帧绘制与 ACK；停止后无继续绘制、socket 关闭、解码队列归零 |
| 正式 client.html#system 页面 | 首次加载加两次刷新均成功，所观察 API 全部 HTTP 200，无脚本异常 |
| 连接状态 | 同一 TCP 连接保留，失败请求与待处理请求均为 0 |

上述测试页最初在握手前出现 1006。六组无键鼠输入的对照已定位为夹具文档加载方式：Fetch.fulfillRequest 合成文档的 initiatorIPAddressSpace 为 Unknown，保留或关闭拦截均复现 1006，未发出 WebSocket HTTP 握手；先从本机实际 HTTP 服务加载文档、再设置测试内容时，地址空间为 Loopback，两组均返回 101 并建连。两种文档的普通状态 HTTP 请求均成功，Origin 字符串一致。当前夹具改用实际 HTTP 文档后加载测试内容，没有修改 Edge 策略、权限或生产服务。浏览器的地址空间检查背景见 [Chrome Local Network Access](https://developer.chrome.com/docs/modern-web-guidance/guides/security/local-network-access)。

此处结论针对上述夹具的 1006，不把其他任意 1006 一概归为同一原因。实测是本机回环 TCP，并不替代跨机丢包、RTT、带宽突降或最终 VM 二进制验收。

现场完整转储、原二进制及 PDB、锁环证据、旧实现失败与修正后成功日志、最终二进制 hash、实际 TCP/输入结果和六组浏览器对照保存在 C:\Users\91521\AppData\Local\KNSoft\ZPigeonDebug\desktop-freeze-20261112-024400。用户随后要求不干扰鼠标；临时窗口已结束，后续验证仅观察正常桌面变化并发送画面 ACK，不发送键鼠输入，不导出桌面截图。

## 2026-11-13：后台采集验收与编码器 Shutdown

按用户本轮授权，在不操作宿主键鼠、不创建可见测试窗口的条件下，用 Headless Edge 157.0.4341.46 接收生产画面并保存解码后的 Canvas PNG。源为用户正常使用中的桌面；夹具只发送显示 ACK 和编解码能力，不提供键鼠输入方法。本轮所有实测均为本机闭环，不代表跨 NAT 或受控弱网验证。

默认 TCP 4433 位于 Windows 端口排除范围 4349–4448，因此隔离 Web 显式使用 `--Transport:Port=21943`。测试 Client 复制自完整解决方案产物，仅在既有 bootstrap 区设置 TCP、21943 和独立临时 CNG key 名称，脚本验证其余字节不变。源码默认值、系统端口排除表和浏览器策略均未调整。

上一轮尚未验收的 D3D 设备复用、MTA 寿命及采集清理修复，在实际 WGC / Intel D3D 设备路径上完成复测。接手时的设备复用版先通过约 300.35 秒、2263 帧绘制与 ACK，以及 100 次启停；没有再现此前第 17 次启停时的 `ControlLib.dll_unloaded` 崩溃。

额外发现视频编码器释放缺口：异步 MFT 结束时原先只发送 END_OF_STREAM、FLUSH 并 Release，没有调用其 `IMFShutdown::Shutdown`。`Encoder.c` 现于释放接口前查询并关闭 `IMFShutdown`，覆盖正常关闭及创建失败清理，不新增线程、锁或运行时依赖。接口合约见 [Asynchronous MFTs / Shutting Down the MFT](https://learn.microsoft.com/en-us/windows/win32/medfound/asynchronous-mfts#shutting-down-the-mft)。

旧版已热身 Client 再交替重启 H.264/H.265 六次后，30 秒静置的 private bytes 从 510,799,872 增至 687,116,288，增加约 168.1 MiB。修正后的新 Client 连续完成两轮六次强制视频启停，每轮均确认真实视频关键帧及 delta 已绘制；第一轮静置约 83.3 MiB，第二轮从 83.3 MiB 到 84.2 MiB，未再现旧版的大幅持续增长。冷启动、驱动缓存及延迟清理会影响绝对值，不能按单次峰值推断泄漏。

| 最终源码 / 二进制检查 | 结果 |
| --- | --- |
| VS2026 / MSBuild 18 完整解决方案 x64 Debug、Release | 均成功，0 警告、0 错误 |
| 原生全量、ConsumerTest、ToolCatalogTest | Debug 838/838、Release 837/837；两个消费端测试均通过 |
| 浏览器回归与 I18N | 43/43；1007 keys、2638 source、155 dynamic、47 Native/Managed，通过 |
| 最终 Debug 桌面连续采集 | 绘制跨度 120.13 秒，842 帧绘制及 842 次显示 ACK，1280×800 |
| 最终 Debug 启停 / 立即取消 / 恢复 | 100/100 次正常启停、10/10 次立即取消、随后恢复，通过 |
| 最终 Release 启停 / 立即取消 / 恢复 | 100/100、10/10、随后恢复，通过；单独初始采集 28 帧及 ACK |
| 三消费者并发及关闭其中一路 | 35 次采样通过；一路停止时绘制 192 帧，其余两路继续至 324 / 322 帧 |
| 真实 DataChannel 与恢复中转 | 直接绘制跨度 30.19 秒；断开前采样 96 帧、3,382,259 接收字节；主动关闭 DataChannel 后恢复中转，再观察 10 秒 |
| 自动图像 / 视频模式 | H.264/H.265 各观察 30 秒，均出现实际视频关键帧与 delta；分别绘制 283 / 4 个视频帧，其余为 JPEG/PNG，不将 H.265 该轮称为连续视频测试 |
| 最终 Debug 采集期间的数据请求 | 530/530 成功，最长约 35.42 ms；无 0xC00000B5 |
| 正式系统信息页面及刷新 | 初次加载和两次刷新均成功，23 个 API 响应全部 HTTP 200，无脚本异常 |

每次正常停止均检查 socket 关闭、旧 Canvas 与绘制计数冻结、解码队列和字节归零；各阶段保留同一个 Client 进程和 TCP 连接，失败请求为 0。最终 Debug 的 100 次启停后，ALPC Port 句柄仍有延迟回落：120 秒静置时总句柄 650，随后并发阶段结束后回落至 599，与该轮开始前 601 接近；不能把即时的逐轮计数上升直接报告为永久泄漏，也不据此宣称所有资源长期零增长。Application Error / WER 中未出现本轮 Client、ControlLib 或 GraphicsCapture 新崩溃记录。

最终 Client SHA256：Debug `2AF46EF488FD6A3FABC1138FF48AB990FD5FF8744F15C19009DD2B99A24C2594`；Release `AFB526DD978961CAE3293D292A479A1D62FD6FCE4DD02C15EECA36A585609D51`。编码器保留 UTF8-BOM / CRLF，本文保留 UTF8 / CRLF，`git diff --check` 通过。

夹具、原始分阶段 JSON、截图、构建 / 回归日志、独立 bootstrap 元数据及 `acceptance-summary.json` 位于 `C:\Users\91521\AppData\Local\Temp\opencode\zpigeon-takeover-20261113`。测试结束关闭本轮 Client / Web 和测试页，删除本轮独立 CNG keys；用户正在使用电脑，本轮未执行休眠。

## 2026-11-13：默认端口与无参数启动补验

上一节使用显式 `--Transport:Port=21943` 和修改 bootstrap 的测试副本，未覆盖直接启动原始 EXE 的默认路径。用户随后无参数运行 Web 时，原默认 TCP 4433 因 Windows 端口排除而返回 Winsock 10013 / `0x0000271D`，启动异常未处理；此前媒体测试通过不能用于证明默认启动通过。

按用户要求，将原始 Client、Managed Server、Web 的默认基础端口统一改为 21943，UDP 为 21944；同步生成模板校验、传输目录、中英文说明及现有默认端口测试。当前本机 TCP/UDP 排除范围均不包含这两个端口。

本轮对 VS2026 / MSBuild 18 完整解决方案重新构建 x64 Debug、Release，均 0 警告、0 错误；两种配置的 ToolCatalogTest、ConsumerTest 和 I18N 检查通过。实测直接启动各自 `Source/OutDir/x64/<配置>` 下的原始 Web.exe、Client.exe：两个程序均不传参数，没有设置 Transport 端口环境变量，没有复制或修改 EXE，启动前后 SHA256 一致。

| 无参数原始产物检查 | Debug | Release |
| --- | --- | --- |
| Web 启动并提供 9983 HTTP 服务 | 通过 | 通过 |
| Web 所有的 TCP 21943、QUIC UDP 21943、DTLS UDP 21944 Listener | 通过 | 通过 |
| 生成目录返回 21943 / 21943 / 21944 | 通过 | 通过 |
| 原始 Client 自动连接 | QUIC（transport=1） | QUIC（transport=1） |
| 系统信息请求 | 成功 | 成功 |
| 生产画面采集、绘制及显示 ACK | 36 / 36 | 37 / 37 |

新的 Client SHA256：Debug `390C63E01A7B85DB3AA8DE932933A28794F13585265914F94E10A357BBDB1781`，Release `D45ECA26F1236A5F6A8C307C9155536762A86245FA267E591985DEBDC799583B`。无参数命令、原始路径、未修改配置标记、PID 对应 Listener、帧记录及日志保存在 `C:\Users\91521\AppData\Local\Temp\opencode\default-port-20261113`。本轮测试进程和测试页已关闭，全程没有操作宿主键鼠。

## 2026-11-13：WGC 帧寿命与黑块排查

用户报告本机自我镜像远控中出现随鼠标移动变化的黑色三角块。本轮先确认当前实例的监听为网页 HTTP 9983、控制连接 TCP/QUIC 21943、DTLS 21944；ASP.NET 的 `Now listening on: http://127.0.0.1:9983` 只报告 HTTP 地址。此前被排除的是旧控制端口 4433。

采集代码存在已实证的纹理寿命错误：`NextWgcFrame` 取得 Surface 的 `ID3D11Texture2D` 后就释放 `Direct3D11CaptureFrame`，但共享源及编码线程仍使用该纹理。纹理的 COM 引用只保留资源对象，并不阻止帧池重新写入同一缓冲区。Microsoft 的 [WGC 帧处理约定](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture#process-capture-frames) 明确以 CaptureFrame 寿命控制缓冲区归还。

修复在原始帧对象中保留对应 WGC Frame，最后一个消费者释放原始帧时才 Close / Release；GDI 自有纹理路径显式没有 WGC Frame。失败路径同样归还帧。共享源退出时先释放最后缓存的原始帧，再关闭采集对象和帧池，避免在清理阶段继续持有已借出的缓冲区。该修复不增加逐帧 GPU 图像副本。

独立诊断程序直接编译生产 `Capture.c`，读取真实 2560×1600 WGC 桌面帧，在继续采集 5 秒期间保留同一个原始帧；用 GPU staging readback 逐行计算像素 CRC，不注入键鼠：

| 同一原始帧的延迟读取 | 修复前 | 修复后 |
| --- | --- | --- |
| 期间取得的后续帧 | 67 | 14 |
| 后续帧复用被持有纹理的次数 | 67 | 0 |
| 首次 / 延迟读取 CRC | 690883697 / 3040643383 | 1034527917 / 1034527917 |
| 内容是否在持有期间改变 | 是 | 否 |

两次运行的源桌面内容及变化频率不同，不将后续帧数量当作吞吐对照，也不比较跨运行的 CRC。回归目标是同一次运行中被持有帧的内容必须保持不变。

最终源码经 VS2026 / MSBuild 18 完整构建 x64 Debug、Release，均 0 警告、0 错误；原生全量均 844/844，两个配置的 ConsumerTest、ToolCatalogTest 均通过。经用户批准更新并后台重启当前 Debug Client/Web 后，生产采集、解码及显示 ACK 路径结果如下：

| 本轮 Debug 实测 | 结果 |
| --- | --- |
| 图像连续采集 | 实际绘制跨度 60.16 秒，464 帧及 464 次显示 ACK |
| 正常启停、立即取消、恢复 | 100/100、10/10、随后恢复，通过 |
| 三消费者并发、关闭其中一路 | 通过，其余两路继续显示 |
| 强制 H.264、H.265 各约 30 秒 | 分别 49 / 57 个真实视频帧全部绘制与 ACK，均有关键帧和 delta |
| P2P 与中转恢复 | 直接绘制跨度 30.92 秒，54 帧；关闭 DataChannel 后恢复中转 |
| 并发系统信息 / 窗口请求 | 410/410 成功 |

这些结果确认帧复用错误已修正，以及新关闭顺序未在上述采集 / 启停中挂起；本轮没有操纵物理鼠标来重现用户截图的递归镜像时序，不能据此宣称该画面所有黑块来源均已排除。额外尝试的完全离屏图案窗口返回整帧黑底，其像素检查失败，未将这个无效夹具计入画质通过项。

证据、原始 JSON、诊断程序与图像位于 `C:\Users\91521\AppData\Local\Temp\opencode\frame-lifetime-20261113`。最终 Client SHA256：Debug `9156DE7DB0F7085A2A4D765E7BD718409E098AB82F80069E6DC6B4EB8A8A13AE`，Release `ACFCC53C6AA8052345A8F95951D33AB7C23E1252EA648672D1A6E00CBC92724E`。本轮创建的测试页与离屏窗口已关闭；重启后的 Debug Client/Web 保留供用户继续使用。
