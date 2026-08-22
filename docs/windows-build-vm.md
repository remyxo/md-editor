# 本地 Windows 构建虚拟机（KVM win11）

> 目标：宿主 Debian 上一条命令产出 Windows 安装包，**未提交的改动立即生效**，不再走「推送 → GitHub Actions → 下载」的往返。
> 设计文档：[`superpowers/specs/2026-08-21-windows-build-vm-design.md`](superpowers/specs/2026-08-21-windows-build-vm-design.md)

## 架构

```
宿主 Debian 13                                guest win11（libvirt system 实例，KVM）
──────────────────────                        ─────────────────────────────
~/claude-workspace/md-editor ←─ virtiofs ─→   卷标 md-editor-src（盘符动态）
./scripts/build-windows-local.sh               D:\build（本地盘构建，快）
       │                                      D:\BuildTools、D:\Qt（工具链，~20G）
       └─ ssh builder@192.168.122.10 ─→ C:\md-editor-build-launch.ps1
                                              产物落共享卷 dist\ = 宿主仓库 dist/
```

- **源码单一来源**：仓库即宿主目录，guest 经 virtiofs 直读直写；build 目录放 D: 本地盘（MSVC 在共享上大量小文件 IO 慢）
- **与 CI 关系**：工具链与 `build-windows.yml` 逐字一致（VS 2022 MSVC、Qt 6.8.2 `win64_msvc2022_64` + webengine/webchannel/positioning、NSIS、ImageMagick）；CI 仍是正式发布通道，本地只做迭代验证
- VM 构建后**保持开机**（省下次 40 秒启动）；长期不用 `virsh -c qemu:///system shutdown win11`

## 一次性搭建

### A. 宿主侧（约 2 分钟）

前置：VM 关机、当前用户在 `libvirt` 组。

```bash
virsh -c qemu:///system shutdown win11   # 若在运行
./scripts/setup-windows-vm-host.sh
```

脚本做 7 件事（幂等）：快照回滚点 `before-build-vm` → 资源调到 **6 vCPU / 6G** → 新建 60G 数据盘 `win11-data.qcow2`（挂 guest `D:`）→ domain XML 追加 virtiofs 共享（宿主仓库 → 卷标 `md-editor-src`）→ ACL 放行 `libvirt-qemu` 用户 → DHCP 固定 IP `192.168.122.10` → 生成宿主 ssh key 与 `~/.ssh/config` 别名 `win11-build`。

结束时打印带公钥的 provision 命令，复制备用。

### B. guest 侧（约 1 小时，多为下载等待）

1. 宿主启动 VM：`virsh -c qemu:///system start win11`
2. VM 内**管理员 PowerShell** 直接从仓库共享运行（virtiofs 直读宿主仓库，脚本改动即时生效；公钥见 setup 输出）：

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File <仓库盘>:\scripts\provision-windows-vm.ps1 -SshPublicKey "ssh-ed25519 AAAA... 你的用户名"
   ```

脚本做：D: 盘 NTFS → 两个 virtiofs 服务明确 tag 化（原 `VirtioFsSvc` 补 `-t daijd-shared`，新建 `VirtioFsSvc2` 挂 `md-editor-src`，盘符固定 `W:`——`-m *` 自动盘符会随开机顺序漂移，炸 `D:\build` 的 CMake 缓存）→ VS 2022 Build Tools（10-25 分钟）→ Python + aqtinstall + Qt 6.8.2（约 2G 下载）→ NSIS + ImageMagick → OpenSSH Server（WU 不通自动用 GitHub 离线包）+ `builder` 用户（随机密码自动存 `C:\builder-pass.txt`，日常构建走公钥用不到）→ 生成 ssh 构建入口 `C:\md-editor-build-launch.ps1` → 自检清单（应全绿）。

本机没有 winget（App Installer 缺失），工具安装包直连官方站静默安装；镜像站在 guest 里不稳时，先在宿主预下到 `~/Downloads`（如 `curl -L -C - -o ~/Downloads/ImageMagick-setup.exe <官方地址>`，断点续传），provision 会优先从该共享（daijd-shared）拷入。

### C. 验证

```bash
./scripts/build-windows-local.sh
```

`dist/` 出现 `md-editor-<版本>-win64.zip` / `.exe` 即成功（不散在仓库根）。可在 VM 里装一次 exe 做冒烟。

## 日常用法

```bash
./scripts/build-windows-local.sh          # 全流程：开机(若关) → 构建 → 产物在 dist/
```

- 增量构建 <1 分钟；冷构建（改 CMake/全量）约 10-15 分钟（6 vCPU）
- 构建期间宿主剩 ~9-10G 内存（另有 15G swap），避免同时跑本地大编译
- 环境变量：`MD_EDITOR_WIN_SSH`（ssh 目标）、`MD_EDITOR_WIN_WAIT`（ssh 等待秒数）
- 产物目录 `dist/` 与生成的 `.ico` 已进 `.gitignore`（CI 上传 glob 同步指向 `dist/`）

## 故障排查

| 症状 | 原因与处理 |
|------|-----------|
| 共享挂不上，宿主 `dmesg \| grep DENIED` 有 apparmor 拒绝 | system 实例 qemu 受 AppArmor 约束。在 `/etc/apparmor.d/libvirt/libvirt-qemu`（或 `local/` 对应文件）追加 `"/home/daijd/claude-workspace/md-editor/**" rwk,` 与 `"~/qemu/**" rwk,` 后 `sudo apparmor_parser -r` 对应 profile，重启 VM |
| 找不到卷标 `md-editor-src` | VM 里 `sc query VirtioFsSvc2`；没起则 `sc start VirtioFsSvc2`；确认宿主 XML 有 `<filesystem>` 段且 target 一致 |
| 两个盘同卷标 `md-editor-src`（其一 FileSystem 为 NTFS）、Downloads 共享消失 | 原服务 `VirtioFsSvc` 不带 `-t` 时会抢挂开机后先枚举到的设备。重跑 provision（步骤 2 会给两个服务都写入明确 `-t <tag>`，仓库共享再 `-m W:` 钉死盘符）；脚本探测已限定 `FileSystem='VirtIO-FS'` 排除假盘 |
| `cmake` 报 `The source "Y:..." does not match the source "Z:..." used to generate cache` | virtiofs 自动盘符漂移（`-m *` 从 Z: 倒序找空位，两服务开机顺序不定会互换盘符），`D:\build` 缓存内嵌了旧盘符的源路径。已根治：`VirtioFsSvc2` 固定挂 `W:`（在自动扫号区外永不冲突），build 脚本带缓存守卫（源路径不符自动清缓存重配置）。存量 VM 重跑 provision，或手动 `sc config VirtioFsSvc2 binpath= "\"C:\Program Files\Virtio-Win\VioFS\virtiofs.exe\" -t md-editor-src -m W:"` 后重启该服务 |
| 安装器下载报「不是完整可执行文件」 | guest 直连镜像站被掐断/拦截（这台环境每次连接约 1MB 即断）。宿主预下到 `~/Downloads` 后 provision 从共享拷入；ImageMagick 走 GitHub 最稳：`gh release download 7.1.2-29 -R ImageMagick/ImageMagick --pattern '*Q16-HDRI-x64-dll.exe' -o ~/Downloads/ImageMagick-setup.exe --clobber` |
| D: 剩余不足 5G 构建中止 | 清 `D:\build`（可整目录删，下次重新配置）或 Windows 磁盘清理；必要时 `virsh vol-resize` 扩盘 + Windows 扩卷 |
| ssh 超时 | `virsh domaddr win11` 看 IP 是否 192.168.122.10；VM 里查 sshd 服务；公钥是否在 `C:\ProgramData\ssh\administrators_authorized_keys`（注意该文件 ACL 只留 Administrators/SYSTEM） |
| `Add-WindowsCapability` 报 0x8024002e（OpenSSH 装不上） | Windows Update 通道被挡。provision 自动改用 GitHub Win32-OpenSSH 离线包（宿主先下：`gh release download 10.0.0.0p2-Preview -R PowerShell/Win32-OpenSSH -p OpenSSH-Win64.zip -o ~/Downloads/`） |
| guest 覆盖宿主属主旧文件失败（如 `.ico`、`dist/`） | 构建脚本会先删 `.ico`；`dist/` 若是宿主侧建的旧目录，宿主 `rm -rf dist` 后重试 |
| NSIS 报 `Bad text encoding` 或 makensis 打印用法退出 1 | nsi 是 UTF-8 无 BOM，中文系统（ACP=GBK）的 makensis 3.10 默认按 GBK 读会报前者。脚本已带 `/INPUTCHARSET UTF8` 修复；注意该参数值与选项**空格分隔**，写成 `/INPUTCHARSET=UTF8` 会被当未知参数直接打印用法退出 |
| 版本号不对 | 打包脚本从 `CMakeLists.txt` 读版本，与 CI 同源 |
| 构建失败 | 退出码与日志原样透传到宿主终端，按 MSVC 常规错误处理 |
| 改了含中文的 .ps1 后 VM 里行为怪异（报错行号对不上、某条命令"消失"） | Windows PowerShell 5.1 读**无 BOM** 的 .ps1 按 GBK 解码，中文注释尾字节可能吞掉换行导致行粘连（CI 用 pwsh 默认 UTF-8，无此问题）。仓库 .ps1 必须存成 UTF-8 带 BOM |
| 重装后图标仍是旧版（如白角）但 exe 内嵌图标已验证正确 | Windows 图标缓存按路径缓存且**落盘**——实测仅重启 VM 不会清除（重启后依旧白角），必须删 `%LOCALAPPDATA%\IconCache.db` 与 `%LOCALAPPDATA%\Microsoft\Windows\Explorer\iconcache_*` 后重启 explorer（或重启 VM）才重新提取。物理机同理（2026-08-22 VM 实测生效，机制与机器无关）。PowerShell 下执行：`taskkill /f /im explorer.exe; Remove-Item -Force "$env:LOCALAPPDATA\IconCache.db" -ErrorAction SilentlyContinue; Remove-Item -Force "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\iconcache_*"; Start-Process explorer.exe`。**注意 `del /a /f /q` 是 cmd 语法，PowerShell 里 `del` 是 `Remove-Item` 别名，不认这些开关会报 ParameterBindingException**——要么用上面的 `Remove-Item`，要么切到 cmd 用 `del /a /f /q "%LOCALAPPDATA%\...\iconcache_*"`。验证 exe 图标是否真有问题：`[System.Drawing.Icon]::ExtractAssociatedIcon('<exe>').ToBitmap().GetPixel(0,0).A`，0 为透明 |

## 维护

- **升级 Qt**（如跟进 CI 版本）：VM 里重跑 `py -3 -m aqt install-qt windows desktop <版本> win64_msvc2022_64 -O D:\Qt -m qtwebengine qtwebchannel qtpositioning`，并同步改 `scripts/build-windows-vm.ps1` 里的 `$qtDir`
- **调整 VM 资源**：`virsh setvcpus win11 <N> --maximum --config && virsh setvcpus win11 <N> --config`；内存同理 `setmaxmem`/`setmem`（KiB），重启 VM 生效
- **回滚到搭建前**：`virsh snapshot-revert win11 before-build-vm`（会丢数据盘上后来装的工具链，慎用）
- **重建 guest 系统**：数据盘（工具链）在独立 qcow2 上，系统盘重装后重跑 provision 的步骤 1/2/6/7 即可复用 `D:\BuildTools`、`D:\Qt`
