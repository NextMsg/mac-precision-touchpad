# 社区分支发布方案

## 建议路线

1. 在维护者的 GitHub 账号下 Fork 上游，保留提交历史、原作者和许可证。建议仓库名 `mac-precision-touchpad`，在描述中标注社区 USB 修复版。
2. 首先公开源码、双语 README、变更说明和测试。建议首个标签 `v0.1.0-alpha.1`，明确源码预览/开发者测试阶段；这个标签尚未创建。
3. 补齐云端驱动构建 CI、通用安装/备份/恢复脚本和更多设备验证后，再准备公共安装包。
4. 面向普通用户的二进制发行，应先落实可信的发布签名及安装流程，再创建正式 GitHub Release，附对应源码、SHA256、安装及回退说明。

首版仅承诺 Magic Trackpad 2 Lightning、有线、Windows 11 x64。不要照搬上游“所有型号/蓝牙均支持”的范围。

## 目前已经具备

- 明确的代码改动和回归测试；本机实际重放、编译和安装验证。
- 新版悬空误触修复已获用户复测确认。
- 专用 USB INF 已通过校验。
- 中英文公开 README、变更说明、开发文档。
- 本机安装记录和私人诊断文档已加入忽略规则；构建产物、证书和日志不进入源码提交。
- 上游 Azure issue 同步工作流已加上原仓库限制，避免 fork 的 issues 被发送到原作者平台。
- 已加入跨平台触点逻辑测试工作流；它不编译或签署 Windows 驱动。

## 仍需完成

- 社区 fork 使用 `NextMsg/mac-precision-touchpad`。源码预览以该仓库为准；后续公开安装包仍需完成以下项目。
- 上云后的 CI 运行结果、Windows 驱动构建 CI 和独立机器从零构建验证。目前“本机通过”不能当作“GitHub Actions 已通过”。
- 通用安装工具：按设备查询原 INF、导出并校验备份、安装失败自动回退、用户主动恢复时可选回旧版本。
- 干净 Windows 安装/卸载/重装、拔插、休眠唤醒、不同 USB 控制器、点击拖动及多指手势验证。
- 正式发行的签名方案和签名密钥管理。

## 签名是当前大众发行的主要缺口

本机运行的包使用维护期间创建的自签开发证书，并已由设备所有者允许加入其本机信任。这不代表其他人的 Windows 会自动接受该包。不要向普通用户发布该证书并要求将其导入系统根信任，也不要将它包装为官方认证驱动。

微软说明测试签名仅用于开发测试，不能用于生产或向客户发布：[Introduction to Test-Signing](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/introduction-to-test-signing)。当前建议先发布源码，开发者自行构建并管理各自的测试环境。

若选择微软硬件签名路线，需要满足 Hardware Dev Center/Partner Center 的资格与证书要求；提交 attestation 或 WHCP 的账号需关联有效 EV 证书：[Driver code signing requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/dashboard/code-signing-reqs)。本项目的专用包是用户态驱动，不能直接套用“所有内核驱动”的规则；具体公共签名渠道、费用和资格需要在选定发布主体后落实，也可以寻找有合规签名能力的维护者合作。

## 许可证及发布内容

此次修改的是 GPLv2 USB 驱动，保留 LICENSE.md、LICENSE-GPL.md 及已有版权声明。二进制版本要能定位到对应源码标签，并提供完整构建所需材料；不要把它整体改成 MIT。

公开源码包含代码、测试、构建与打包脚本、专用 INF 和经过整理的公开文档。不公开本机备份、证书私钥、原始触摸轨迹、个人路径及设备实例标识。原始诊断仍保存在本地，公开说明只使用汇总结果。

正式 Release 的建议资产：签名驱动包、对应源码包、SHA256SUMS、安装/恢复说明及已知限制。公开源码预览阶段不上传现有自签二进制。
