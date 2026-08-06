# KNSoft.ZPigeon 项目进度

更新时间：2026-08-07

总体设计基线见 `Design.md`；长期项目约束和协作约定见 `Memory.md`。本文只记录实施状态、下一步和阻塞项。

## 当前阶段

项目已完成第一版 Protocol/API 规格、通用 Protocol、Transport 无关的 Connection 核心、Client/Server SDK 公开 API 契约、配置对象所有权以及 Start/Stop 生命周期；QUIC Transport 的连接、监听、证书链和单 Stream 骨架已落地，正在接入协议握手。系统管理模块尚未开始。

当前解决方案包含：

- `KNSoft.ZPigeon.Protocol` 静态库；
- `KNSoft.ZPigeon.Client.SDK` 静态库骨架；
- `KNSoft.ZPigeon.Server.SDK` 静态库骨架；
- `UnitTest` 测试项目。

## 已完成

- 明确产品边界与纯 C、Windows 10 及以上、x86/x64 的实施基线；
- 完成 C/S、项目依赖、Transport、身份、连接加密、Protocol 和功能目录的总体设计；
- 将总体设计整理到独立的 `Design.md`；
- 明确第一版应保持薄 Network 模型，不预先实现复杂 RPC、权限或虚拟流框架；
- 定稿 Core Version 1 的 Frame、MessageType、Codec、握手密码学、Endpoint、重连、取消、Deadline 和 Buffer 生命周期；
- 实现不分配内存的 `ZpCodec_*` 小端读写和 View 解码；
- 实现 `ZpFrame_*` 长度计算、编码、流式解码及类型专用 Body 边界校验；
- 实现 ClientHello、ServerChallenge、ClientAuthenticate、Ready 和 Disconnect 类型化 Codec；
- 实现 Client/Server 握手发送与接收状态机，拒绝越序、重复及握手阶段业务消息；
- 实现任意 Transport 分片与合并交付下的 Frame 切分，完整 Frame 走零中间复制路径，分片 Buffer 按实际接收量渐进增长；
- 定义公共 Transport、Endpoint、Listener、五类不透明 Handle，以及 Client/Server 配置、生命周期状态和回调 API；
- 实现 Client/Server `Create` 与 `Close`，包括配置校验、单块内存深拷贝、默认值、关闭状态门禁和 Server 证书 Context 引用管理；
- 实现 Client/Server `Start`、`Stop`、受控状态转换和 Transport 操作表，状态回调在对象锁外执行并由活动回调计数保护 Handle 生命周期；
- 实现基于 KNSoft.Quic/MsQuic 的 Client 连接与 Server 监听骨架，包括独立 Host 解析、SNI、`knsoft-zpigeon/1` ALPN、Client 专属根证书链验证、Server 按 SNI 选证书以及一条双向 Stream 限制；
- 创建 Protocol、Server SDK 和 UnitTest 工程，并建立 Client/Server 到 Protocol 的工程依赖；
- x86/x64 的 Debug/Release 全矩阵 Rebuild 通过且无编译或链接警告；每个配置下 168 项断言全部通过。

## 下一步

1. 将握手 Codec、客户端签名和服务端验证接入 QUIC Stream 与 Connection 核心；
2. 补齐 Endpoint 轮询、重连退避和混合 Transport 路由；
3. 以 Ping/Pong 和 System.Info 完成首个端到端 C/S 验证；
4. 再按 Process、Service、File、Terminal 等模块逐步实现。

## 待确认与阻塞

- 当前无外部阻塞。
- File、Terminal、EventLog 等模块专属协议按 `Design.md` 的“仍按模块延后确定的规格”在实现前定稿。
