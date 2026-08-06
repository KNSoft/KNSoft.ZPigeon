# KNSoft.ZPigeon 项目进度

更新时间：2026-08-07

总体设计基线见 `Design.md`；长期项目约束和协作约定见 `Memory.md`。本文只记录实施状态、下一步和阻塞项。

## 当前阶段

项目处于 SDK 总体设计完成、第一版 Protocol/API 规格待编写阶段，尚未开始实现网络协议或系统管理模块。

当前解决方案只有 `KNSoft.ZPigeon.Client.SDK` 静态库骨架，`Network/Main.c` 为空。项目已引用：

- KNSoft.NDK；
- KNSoft.MakeLifeEasier；
- KNSoft.Quic。

## 已完成

- 明确产品边界与纯 C、Windows 10 及以上、x86/x64 的实施基线；
- 完成 C/S、项目依赖、Transport、身份、连接加密、Protocol 和功能目录的总体设计；
- 将总体设计整理到独立的 `Design.md`；
- 明确第一版应保持薄 Network 模型，不预先实现复杂 RPC、权限或虚拟流框架。

## 下一步

编写并确认第一版 Protocol/API 规格：

1. 各 MessageType 的最小字段、确切位宽、长度定义和大小上限；
2. Hello、S 证书验证、客户端 Challenge 签名、Ready 和断开流程；
3. 固定版本 Payload Codec 及字符串、数组编码规则；
4. Endpoint、连接超时、重试和退避规则；
5. Protocol、Client SDK、Server SDK 的公开头文件、Handle、回调及 Buffer 生命周期；
6. Request 取消、Deadline、Ping/Pong 和 ChannelData 语义；
7. 所需辅助函数与 KNSoft.MakeLifeEasier 现有能力的逐项核对。

规格确认后：

1. 创建 KNSoft.ZPigeon.Protocol 项目；
2. 创建 KNSoft.ZPigeon.Server.SDK 项目；
3. 建立第一版 Network 和 Protocol 骨架；
4. 以 System.Info/Ping 完成首个端到端 C/S 验证；
5. 再按 Process、Service、File、Terminal 等模块逐步实现。

## 待确认与阻塞

- 当前无外部阻塞。
- 第一版规格中的未定项集中列在 `Design.md` 的“第一版规格待定项”，编码前逐项确认。
