# progress-1

## 背景

本文档记录 `plan-1.md` 当前这一轮已经实际落到仓库中的实现进度，重点覆盖 Windows GUI、MSI 安装包和 GitHub Release 自动化三部分。

记录时间：2026-04-12

## 当前结论

- `plan-1` 已经从纯计划进入代码落地阶段
- 阶段 1 的主体代码已经写入仓库
- 阶段 2 和阶段 3 的骨架与脚本已经写入仓库
- Windows launcher 基线已经切换为 `.NET 10`
- `Visual Studio Community 2026 + .NET 10 SDK` 是当前支持的本地开发环境
- 在当前这轮会话里，关键构建链路已经完成一次真实验证：
  - `dotnet test` 已通过
  - `dotnet publish` 已通过
  - `tools/package-windows-installer.ps1` 已成功生成 MSI 与 SHA256
- 已完成一轮安装问题定位：
  - 非管理员权限下 `perMachine` 安装会触发 `Error 1925`，并以 `1603` 失败
  - 在具备权限后，MSI 安装可成功落地
- 已定位并修复“安装后启动无界面”问题：
  - 根因是 WPF `TextBox.Text` 默认 `TwoWay` 绑定到只读属性（`HostdPath`）导致启动即崩溃
  - 已在 `MainWindow.xaml` 将只读展示字段绑定改为 `Mode=OneWay`
- 修复后已重新打包 MSI（`artifacts/release/NetworkSpeaker-0.1.0-win-x64.msi`）
- 当前剩余工作已主要收敛到 CI 发布闭环验证与跨环境安装回归验证

## 已完成：阶段 1（GUI MVP 与 hostd 托管）

### 新增项目结构

- 新增 `apps/windows-launcher/NetworkSpeaker.Launcher.Core`
- 新增 `apps/windows-launcher/NetworkSpeaker.Launcher`
- 新增 `apps/windows-launcher/NetworkSpeaker.Launcher.Core.Tests`

### 已落地功能

- 新增 `LaunchConfiguration`、参数枚举和校验逻辑
- 新增 `HostdCommandBuilder`
  - 将 GUI 表单参数映射为 `hostd.exe` 命令行
  - 当前覆盖：
    - `host`
    - `port`
    - `source=wasapi/sine`
    - `wasapi-role`
    - `seconds`
- 新增 `LauncherSettingsStore`
  - 配置持久化到 `%AppData%\NetworkSpeaker\settings.json`
  - 已补充无配置文件、无效 JSON、部分读取异常时回退默认值的处理
- 新增 `HostdLocator`
  - 支持按如下顺序发现 `hostd.exe`
    - 环境变量 `NSPEAKER_HOSTD_PATH`
    - GUI 同目录
    - 仓库常见构建目录 `build-windows-release` / `build-windows-verify` / `build-windows` / `build`
- 新增 `HostdProcessController`
  - 启动隐藏的 `hostd.exe`
  - 读取 stdout/stderr
  - 跟踪状态切换
  - `Stop` 时结束整个进程树
  - 已补充：
    - 重复启动保护
    - 异常退出转入 `Faulted`
    - 停止超时保护
    - 清理事件订阅和活动进程引用
- 新增 WPF 主窗口
  - 支持输入目标 IP、端口、source、WASAPI role、seconds
  - 展示：
    - `hostd.exe` 路径
    - 实际命令预览
    - 运行状态
    - 日志输出
- 新增 ViewModel
  - 负责 UI 状态、配置加载/保存、启动/停止命令协调
  - 已补充启动、停止、初始化失败时的用户可见错误提示
- 已修复窗口关闭时同步等待 `ShutdownAsync()` 可能造成 UI 卡死的问题
  - 改为异步关闭流程

### 已补充测试

- 新增命令行映射测试
- 新增配置持久化测试
- 新增无效配置文件回退默认值测试
- 新增进程状态机测试
- 新增子进程异常退出测试
- 新增重复点击 `Start` 的保护测试
- 新增 `hostd.exe` 基础定位测试

## 已完成：阶段 2（安装器、产物装配与本地打包骨架）

### 新增安装工程

- 新增 `installer/windows/NetworkSpeaker.Installer.wixproj`
- 新增 `installer/windows/Product.wxs`

### 已落地打包逻辑

- 新增 `tools/package-windows-installer.ps1`
- 当前脚本已串联以下步骤：
  1. 读取仓库版本号
  2. 检查 `.NET 10 SDK`
  3. 初始化 Visual Studio C++ 构建环境
  4. 用 CMake 构建 `hostd.exe`
  5. `dotnet publish` 自包含 WPF
  6. 复制 `hostd.exe` 到发布目录
  7. 调用 WiX 生成 MSI
  8. 生成 SHA256 校验文件

### 已处理的安装器细节

- 安装目标为 `x64`
- 安装范围为 `perMachine`
- MSI 包含：
  - `NetworkSpeaker.exe`
  - `hostd.exe`
  - Start Menu 快捷方式
  - 卸载入口
- 版本升级策略已加入 `MajorUpgrade`
- 同版本重装策略已显式配置 `AllowSameVersionUpgrades="yes"`

## 已完成：阶段 3（GitHub Release 自动化骨架）

### 新增文件

- 新增 `.github/workflows/release.yml`
- 新增 `.github/release-notes-template.md`
- 新增仓库根目录 `global.json`

### 已落地流程

- `push tags: v*` 触发正式发布
- `workflow_dispatch` 触发 dry run
- workflow 当前包括：
  - checkout
  - setup-dotnet
  - 初始化 MSVC
  - 运行 launcher core tests
  - 调用打包脚本
  - 上传 MSI 与 SHA256 artifact
  - tag 场景下发布 GitHub Release

### 已处理的细节

- 版本号从 tag 或手动输入中提取
- `vX.Y.Z` 会转成显示版本 `X.Y.Z`
- Release 资产命名对齐 `NetworkSpeaker-<version>-win-x64`
- README 已增加 Releases 下载入口

## 已完成：文档与仓库配套

- 更新 `.gitignore`
  - 忽略 launcher / installer 的 `bin`、`obj`
  - 忽略 `artifacts/`
- 更新 `README.md`
  - 转为更偏使用者的说明
  - 加入 GUI、MSI、Release 下载说明
- 更新 `docs/CONTRIBUTE.md`
  - 增加 GUI 构建、测试、打包说明
- 更新 `docs/DESIGN.md`
  - 增加 Windows GUI / MSI / Release 架构说明

## 本轮新增：安装验证与启动崩溃修复

### 安装验证进展

- 已执行静默安装验证并留存日志：
  - 失败样本日志显示 `Error 1925`（权限不足）
  - 对应 MSI 返回码 `1603`
- 已确认一次安装成功（注册表卸载项和安装目录均存在）
- 已完成本机手动安装与功能验证，当前 MSI 可正常安装并可正常使用

### 启动崩溃根因与修复

- 通过 Windows 事件日志定位到 `.NET Runtime 1026` 未处理异常：
  - `System.InvalidOperationException`
  - 关键信息：无法对 `MainViewModel.HostdPath` 只读属性进行 `TwoWay/OneWayToSource` 绑定
- 已修复文件：
  - `apps/windows-launcher/NetworkSpeaker.Launcher/MainWindow.xaml`
- 修复内容：
  - `HostdPath` 文本框绑定改为 `Mode=OneWay`
  - `CommandPreview` 文本框绑定改为 `Mode=OneWay`
  - `LogsText` 文本框绑定改为 `Mode=OneWay`
- 修复后验证：
  - `dotnet build`（Launcher）通过
  - 发布版 `NetworkSpeaker.exe` 启动自检不再“秒退”
  - 已重新执行打包脚本并生成新的 MSI + SHA256

## 未完成 / 未验证

### 本机尚未完成的验证

- 未完成 GitHub tag 触发的完整 Release 发布闭环验证

### 原因

- 当前仓库已经不再依赖 `.NET 8 SDK`
- 当前会话已完成：
  - `dotnet test .\apps\windows-launcher\NetworkSpeaker.Launcher.Core.Tests\NetworkSpeaker.Launcher.Core.Tests.csproj -c Release`
  - `tools/package-windows-installer.ps1`（包含 hostd 构建、launcher publish、WiX 出包、SHA256 生成）
- 本机安装验证结论：
  - 非管理员执行 `perMachine` 安装仍会触发 `Error 1925`（历史失败样本）
  - 具备管理员权限时安装与使用验证已通过
- WiX 出包仍保留 1 条警告（ICE61），原因是安装器显式启用了 `AllowSameVersionUpgrades="yes"`

## 当前风险

- GitHub Actions workflow 逻辑已写好，但还没有经过一次真实 tag 发布验证
- 安装器当前保留 1 条 ICE61 警告，需确认是否继续保留同版本重装策略

## 下一步建议

1. 创建测试 tag，跑通 `.github/workflows/release.yml`
2. 从 GitHub Release 下载 MSI，在无开发环境机器上做一次真实安装验证
3. 评估是否保留 `AllowSameVersionUpgrades="yes"`（若保留，则接受 ICE61 警告）
4. 补做一次“标准用户权限”安装指引说明，明确 `perMachine` 需要管理员权限

## 本轮涉及的主要文件

- `apps/windows-launcher/NetworkSpeaker.Launcher.Core/`
- `apps/windows-launcher/NetworkSpeaker.Launcher/`
- `apps/windows-launcher/NetworkSpeaker.Launcher.Core.Tests/`
- `installer/windows/NetworkSpeaker.Installer.wixproj`
- `installer/windows/Product.wxs`
- `tools/package-windows-installer.ps1`
- `.github/workflows/release.yml`
- `.github/release-notes-template.md`
- `README.md`
- `docs/CONTRIBUTE.md`
- `docs/DESIGN.md`
- `global.json`
