# 设计：本地 KVM Windows 构建环境（复用现有 win11 VM）

> 2026-08-21 与用户讨论定稿。运维手册见 [`docs/windows-build-vm.md`](../windows-build-vm.md)。

## 背景与目标

在 Debian 13 上开发，每次验证 Windows 端要走「推送 → GitHub Actions → 下载 → 安装」往返 15-30 分钟。
Linux→Windows 交叉编译不可行：**QtWebEngine 无法从 Linux 交叉编译**（Qt 官方仅支持目标平台原生构建；MXE / vcpkg mingw / 发行版 mingw Qt 均无 WebEngine；官方预编译仅 MSVC 版，MinGW 链不了 MSVC ABI）。

**目标**：宿主一条命令 `./scripts/build-windows-local.sh`，未提交的改动直接产出 `md-editor-x.y.z-win64.zip/.exe`。CI 不动，仍为正式发布通道。

## 架构

```
宿主 Debian 13                                     guest win11（KVM, system 实例）
────────────────────                               ────────────────────────────
~/claude-workspace/md-editor   ←─ virtiofs(新) ─→   共享卷（卷标 md-editor-src）
       │                                            D:\build（本地盘构建）
./scripts/build-windows-local.sh                    D:\Qt、D:\BuildTools（工具链）
       │                                            C:\md-editor-build-launch.ps1（trampoline）
       └── ssh ──→ 触发构建 ──→ 产物落共享卷根（= 宿主仓库根，即时可见）
```

## 关键决策

| 决策点 | 结论 | 理由 |
|--------|------|------|
| 复用现有 win11 VM | libvirt system 实例 `win11` | virtiofs 机制已通（Downloads↔daijd-shared）、系统干净、q35/UEFI/virtio 就绪 |
| 资源 | 6 vCPU / 6G 内存 | 用户选定保守档；宿主 16 线程/16G，构建时宿主余量 ~9-10G |
| 数据盘 | 新增 qcow2 → guest `D:`，`/var/lib/libvirt/images/win11-data.qcow2` 虚拟 60G | `/home` 仅剩 28G 不够；工具链+build 全在 D:，重装系统盘不丢 |
| 源码进 VM | virtiofs 追加共享（target `md-editor-src`） | 未提交改动立即生效，源码单一来源；git 同步方案会退回「先提交才能测」 |
| 构建/产物分离 | build 在 D: 本地盘；dist/zip/exe 落共享卷 | MSVC 在 virtiofs 上大量小文件 IO 慢；产物落共享即宿主可见 |
| 触发 | guest OpenSSH Server + builder 用户；宿主 wrapper | 一条命令、透传输出与退出码 |
| 盘符解析 | 按卷标 `md-editor-src` 动态定位（不硬编码 E:） | virtiofs 盘符由下一个空闲字母决定，现有 daijd-shared 可能已占 E: |
| Qt | 6.8.2 win64_msvc2022_64 + qtwebengine/qtwebchannel/qtpositioning（aqtinstall） | 与 `build-windows.yml` 逐字一致 |
| JS 测试 | VM 内跳过 | 平台无关，宿主本地可跑；省 Node 环境 |
| 多共享挂载 | `sc create` 第二个 virtiofs 服务实例 | virtio-win 每服务实例只挂一个 tag |

## 仓库改动清单

| 文件 | 性质 | 说明 |
|------|------|------|
| `scripts/setup-windows-vm-host.sh` | 新增（宿主，一次性） | 快照、资源调整、数据盘（经 libvirt 存储池创建免 sudo）、追加共享 XML、DHCP 绑定、ACL、ssh key/config |
| `scripts/provision-windows-vm.ps1` | 新增（guest，一次性） | D: 初始化、第二 virtiofs 实例、VS BuildTools/Qt/NSIS/ImageMagick、OpenSSH+builder、trampoline、自检 |
| `scripts/build-windows-vm.ps1` | 新增（guest，每次构建） | DevShell → Qt/magick PATH → 图标 → cmake(Ninja, build=D:\build) → 打包 |
| `scripts/build-windows-local.sh` | 新增（宿主入口） | 确保 VM 开机 → 等 ssh → 触发 trampoline → 列产物 |
| `scripts/package-windows.ps1` | 小改 | 加可选 `-BuildDir`（默认值不变，CI 零影响） |
| `docs/windows-build-vm.md` | 新增 | 搭建手册 + 日常用法 + 故障排查 |
| `.gitignore` | 小改 | 忽略 `dist/`、生成图标、win64 产物 |

## 错误处理

- VM 启动 2 分钟 ssh 不通 → 报错 + 排查指引（domaddr / sshd 服务 / 密钥）
- D: 剩余 <5G → 构建前中止
- 工具链缺失（DevShell/Qt/magick/共享找不到）→ 明确指出对应 provision 步骤
- MSVC/cmake/makensis 失败 → 原样透传退出码
- 已知风险：system 实例 qemu 以 libvirt-qemu 用户 + AppArmor 跑，共享 home 目录可能 DENIED → 手册给诊断与修法（dmesg/journalctl + apparmor 规则）

## 验收标准

1. 宿主改源码**不提交** → `build-windows-local.sh` → 仓库根出现 zip+exe（版本号来自 CMakeLists）
2. VM 内安装 exe 能启动（人工冒烟一次）
3. 增量构建 <1 分钟；冷构建预期 ~10-15 分钟（6 vCPU）
4. CI 行为零变化（`package-windows.ps1` 默认参数路径不变）
