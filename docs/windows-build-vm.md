# 本地 Windows 构建虚拟机（KVM win11）

> 目标：宿主 Debian 上一条命令产出 Windows 安装包，**未提交的改动立即生效**，不再走「推送 → GitHub Actions → 下载」的往返。
> 设计文档：[`superpowers/specs/2026-08-21-windows-build-vm-design.md`](superpowers/specs/2026-08-21-windows-build-vm-design.md)

## 架构

```
宿主 Debian 13                                guest win11（libvirt system 实例，KVM，UEFI+TPM）
──────────────────────                        ─────────────────────────────
~/claude-workspace/md-editor ←─ virtiofs ─→   卷标 md-editor-src（盘符钉 W:）
./scripts/build-windows-local.sh               <数据盘>:\build（本地盘构建，快）
       │                                      <数据盘>:\BuildTools、<数据盘>:\Qt（工具链，~20G）
       └─ ssh builder@192.168.122.10 ─→ C:\md-editor-build-launch.ps1
                                              产物落共享卷 dist\ = 宿主仓库 dist/
```

- **源码单一来源**：仓库即宿主目录，guest 经 virtiofs 直读直写；build 目录放数据盘（MSVC 在共享上大量小文件 IO 慢）
- **与 CI 关系**：工具链与 `build-windows.yml` 逐字一致（VS 2022 MSVC、Qt 6.8.2 `win64_msvc2022_64` + webengine/webchannel/positioning、NSIS、ImageMagick）；CI 仍是正式发布通道，本地只做迭代验证
- **剪贴板共享**：SPICE + qxl + spicevmc 通道 + guest 内 spice vdagent（已装），virt-manager 窗口宿主↔VM 双向文本复制粘贴即用
- VM 构建后**保持开机**（省下次 40 秒启动）；长期不用 `virsh -c qemu:///system shutdown win11`

## 盘符防漂移

数据盘盘符由「当前系统」的挂载管理器分配（持久化在本系统注册表），**重装系统/换 VM 后不保证还是 D:**（CD-ROM 或其他卷可能抢 D:）；virtiofs `-m *` 自动盘符则随服务启动顺序漂移。三层定位策略：

| 卷 | 定位方式（脚本层，盘符无关） | 盘符（工具链层） |
|----|------------------------------|------------------|
| 数据盘（NTFS，文件 `~/qemu/win11/win10-data.qcow2`，文件名历史遗留） | **卷标 `build-data`**（`Win32_LogicalDisk` WMI 查询；provision 与 build 脚本均按此解析 `$DataRoot`） | provision 解析出非 D: 且 D: 空闲时用 diskpart **钉回 D:**——VS/Qt 注册表内嵌 `D:\BuildTools`、`D:\Qt` 绝对路径，钉回才能复用旧盘工具链 |
| 仓库共享（virtiofs） | 卷标 `md-editor-src`（`Win32_LogicalDisk`，virtiofs 卷不出现在 Get-Volume） | 服务 `VirtioFsSvc2` 钉 `-m W:`（自动扫号区 Z:/Y: 之外，永不冲突） |
| Downloads 共享（virtiofs） | 卷标 `daijd-shared` | 自动盘符（无路径依赖） |

存储操作用 **WMI + diskpart/label** 实现而非 `Get-Volume`/`Set-Partition` 等 Storage 模块 cmdlet——后者属 Windows PowerShell 系统模块，深度精简系统会整个删掉（2026-10-12 在精简 LTSB 上实测不可用）；WMI/diskpart 是系统核心组件，任何 Windows 都在，且完整系统上行为一致。

另带 CMake 缓存守卫（build 脚本：缓存内嵌源路径与当前不符自动清缓存重配置）兜底历史漂移。

## 一次性搭建

### A. 宿主侧（约 2 分钟）

前置：VM 已建好并关机、工具链数据盘已挂 `vdb`、当前用户在 `libvirt` 组。

```bash
virsh -c qemu:///system shutdown win11   # 若在运行
./scripts/setup-windows-vm-host.sh
```

脚本做 8 件事（幂等）：快照回滚点 `before-build-vm` → 资源调到 **6 vCPU / 6G** → 确认数据盘挂 `vdb`（缺失时按 `~/qemu/win11/win10-data.qcow2` 补挂，不自动创建）→ 补 `<memoryBacking>` 共享内存（**新 VM 默认 XML 没有，缺了 define virtiofs 段时报 `'virtiofs' requires shared memory`**）→ 追加两个 virtiofs 共享（宿主仓库 → `md-editor-src`、`~/Downloads` → `daijd-shared`）→ ACL 放行 `libvirt-qemu` 用户 → 打印 AppArmor 放行命令（需 root，复制执行）→ DHCP 固定 IP `192.168.122.10` → 生成宿主 ssh key 与 `~/.ssh/config` 别名 `win11-build`。

结束时打印带公钥的 provision 命令，复制备用。

**AppArmor**：system 实例 qemu 受 AppArmor 约束。libvirt 启动 VM 时生成的 per-VM profile 带 `#include if exists <libvirt/<uuid>.files>`——把路径放行写进 `/etc/apparmor.d/libvirt/<uuid>.files`（setup 第 6 步打印的命令）即持久生效，profile 再生成也会带上，无需手动 `apparmor_parser`。

### B. guest 侧（约 1 小时，多为下载等待）

1. 宿主启动 VM：`virsh -c qemu:///system start win11`
2. VM 内**管理员 PowerShell** 直接从仓库共享运行（virtiofs 直读宿主仓库，脚本改动即时生效；公钥见 setup 输出）：

   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File <仓库共享盘>:\scripts\provision-windows-vm.ps1 -SshPublicKey "ssh-ed25519 AAAA... 你的用户名"
   ```

脚本做：数据盘按卷标定位（WMI + diskpart 钉回 D:）→ 两个 virtiofs 服务明确 tag 化（原 `VirtioFsSvc` 补 `-t daijd-shared`，新建 `VirtioFsSvc2` 挂 `md-editor-src` 钉 `W:`）→ VS 2022 Build Tools（10-25 分钟）→ Python + aqtinstall + Qt 6.8.2（约 2G 下载；**Qt 已在数据盘时 Python 一并跳过**）→ NSIS + ImageMagick → OpenSSH Server（统一走 GitHub Win32-OpenSSH 离线包：C:\ 预下 → daijd-shared 共享 → 直连兜底）+ `builder` 用户（随机密码自动存 `C:\builder-pass.txt`，日常构建走公钥用不到）→ 生成 ssh 构建入口 `C:\md-editor-build-launch.ps1`（带 BOM 写出）→ 自检清单（应全绿）。

本机没有 winget（App Installer 缺失），工具安装包直连官方站静默安装；镜像站在 guest 里不稳时，先在宿主预下到 `~/Downloads`（如 `curl -L -C - -o ~/Downloads/ImageMagick-setup.exe <官方地址>`，断点续传），provision 会优先从该共享（daijd-shared）拷入。

### C. 验证

```bash
./scripts/build-windows-local.sh
```

`dist/` 出现 `md-editor-<版本>-win64.exe` 即成功（不散在仓库根）。可在 VM 里装一次 exe 做冒烟。

## 重建 guest 系统（宿主重装/换 VM 后复用数据盘）

**2026-10-12 实际重建记录**（宿主重装 Debian、旧 domain XML 丢失、复用 `~/qemu/win11/` 下原系统盘 + 数据盘）：

1. **重写 domain XML**（`virsh define`）：q35 + UEFI（`<os firmware='efi'>`，libvirt 自动管理 OVMF CODE+VARS）+ swtpm 2.0 + 6 vCPU/6G + memfd 共享内存 + 两块 virtio 盘（vda 系统 / vdb 数据）+ virtio 网卡（**沿用 DHCP 绑定过的 MAC** 即得固定 IP）+ spice/qxl/spicevmc + **guest agent channel**（`org.qemu.guest_agent.0`，重建的关键工具）+ 两个 virtiofs 共享段
2. **AppArmor**：define 后拿新 UUID，按 setup 第 6 步命令写 `<uuid>.files`
3. **NVRAM 丢失**：旧引导变量随宿主重装没了，全新 OVMF VARS 启动——实测 Windows 引导项自动找到正常开机；万一进 UEFI 界面，`Boot Maintenance Manager → Boot From File → vda EFI 分区 → EFI\Microsoft\Boot\bootmgfw.efi` 一次性手动引导，进系统后启动项自动重建
4. **推新公钥免手动**：宿主 `~/.ssh` 随重装丢失时，用 qemu-ga 远程执行（guest 里 qemu-ga 随 guest tools 自启）：
   ```bash
   virsh -c qemu:///system qemu-agent-command win11 '{"execute":"guest-exec","arguments":{"path":"cmd.exe","arg":["/c","echo <新公钥> >> C:\\ProgramData\\ssh\\administrators_authorized_keys && icacls C:\\ProgramData\\ssh\\administrators_authorized_keys /inheritance:r /grant Administrators:F /grant SYSTEM:F"],"capture-output":true}}'
   ```
   （输出经 base64+GBK 解码查看；guest 里 virtiofs 服务/vdagent/sshd 全部随系统盘原样自启，数据盘在旧系统里本就是 D:，无需重钉）
5. 验证：`./scripts/build-windows-local.sh --compile-only`（增量 <1 分钟）

**教训（win10 精简版 LTSB 尝试，已弃）**：深度精简镜像缺 Windows Installer（msiexec /qn 假成功 rc=0 不装东西）、Windows PowerShell 系统模块（Get-Volume/DISM 全没了）、WinFsp 只能 MSI 分发装不上——virtiofs/MSI 路线全断。guest 用**完整版** Windows，别用精简镜像。

## 日常用法

```bash
./scripts/build-windows-local.sh          # 全流程：开机(若关) → 构建 → 产物在 dist/
```

- 预检模式：`./scripts/build-windows-local.sh --compile-only` 只配置+编译（跳过图标与打包），
  pre-push 钩子自动调用（见 `.githooks/pre-push` 与 spec
  `docs/superpowers/specs/2026-08-30-prepush-cross-compile-verify-design.md`）；
  环境故障（virsh/ssh）exit 75 放行；MSVC 编译失败日志透传、包装器统一 exit 1（预检模式下 VM 内环境故障 exit 90/255 放行为 75）
- 构建触发前自动探测 guest 里 `pwsh`/`powershell` 哪个可用（精简系统装 pwsh 7 的场景；正常 win11 用 powershell）
- 增量构建 <1 分钟；冷构建（改 CMake/全量）约 10-15 分钟（6 vCPU）
- 构建期间宿主剩 ~9G 内存（另有 swap），避免同时跑本地大编译
- 环境变量：`MD_EDITOR_WIN_SSH`（ssh 目标）、`MD_EDITOR_WIN_WAIT`（ssh 等待秒数）
- 产物目录 `dist/` 与生成的 `.ico` 已进 `.gitignore`（CI 上传 glob 同步指向 `dist/`）

## 故障排查

| 症状 | 原因与处理 |
|------|-----------|
| define 共享段报 `'virtiofs' requires shared memory` | 新建 VM 的默认 XML 没有 `<memoryBacking>`。`virsh edit win11` 在 `<vcpu` 前加 `<memoryBacking><source type='memfd'/><access mode='shared'/></memoryBacking>`（setup 第 4 步已自动做） |
| 共享挂不上，宿主 `dmesg \| grep DENIED` 有 apparmor 拒绝 | 执行 setup 第 6 步打印的 AppArmor 放行命令（写 `/etc/apparmor.d/libvirt/<uuid>.files`），重启 VM |
| 找不到卷标 `md-editor-src` | VM 里 `sc query VirtioFsSvc2`；没起则 `sc start VirtioFsSvc2`；确认宿主 XML 有 `<filesystem>` 段且 target 一致 |
| 两个盘同卷标 `md-editor-src`（其一 FileSystem 为 NTFS）、Downloads 共享消失 | 原服务 `VirtioFsSvc` 不带 `-t` 时会抢挂开机后先枚举到的设备。重跑 provision（步骤 2 会给两个服务都写入明确 `-t <tag>`，仓库共享再 `-m W:` 钉死盘符）；脚本探测已限定 `FileSystem='VirtIO-FS'` 排除假盘 |
| `cmake` 报 `The source "Y:..." does not match the source "Z:..." used to generate cache` | virtiofs 自动盘符漂移（`-m *` 从 Z: 倒序找空位，两服务开机顺序不定会互换盘符）。已根治：`VirtioFsSvc2` 固定挂 `W:`（在自动扫号区外永不冲突），build 脚本带缓存守卫（源路径不符自动清缓存重配置）。存量 VM 重跑 provision，或手动 `sc config VirtioFsSvc2 binpath= "\"C:\Program Files\Virtio-Win\VioFS\virtiofs.exe\" -t md-editor-src -m W:"` 后重启该服务 |
| 数据盘不是 D:（重装系统/挂新 VM 后） | 正常现象，provision 会按卷标 `build-data` 定位并用 diskpart 钉回 D:。若 D: 被其他卷占住钉不回：磁盘管理把占位卷挪走、数据盘改回 D:（VS/Qt 注册表内嵌 D: 绝对路径，不钉回则工具链失效需重装）；构建脚本本身按卷标解析、任意盘符可跑 |
| `VirtioFsSvc` 起不来，`failed to load WinFsp DLL` | WinFsp 缺失（virtio-win guest tools 不带它）。完整 Windows 装 WinFsp MSI（winfsp releases）即可；精简系统 MSI 装不上——见「重建」一节教训，换完整版系统 |
| 安装器下载报「不是完整可执行文件」 | guest 直连镜像站被掐断/拦截。宿主预下到 `~/Downloads` 后 provision 从共享拷入；ImageMagick 走 GitHub 最稳：`curl -L -o ~/Downloads/ImageMagick-setup.exe https://github.com/ImageMagick/ImageMagick/releases/download/7.1.2-29/ImageMagick-7.1.2-29-Q16-HDRI-x64-dll.exe` |
| 数据盘剩余不足 5G 构建中止 | 清 `<数据盘>:\build`（可整目录删，下次重新配置）或 Windows 磁盘清理；必要时 `virsh vol-resize` 扩盘 + Windows 扩卷 |
| ssh 超时 | `virsh domifaddr win11` 看 IP 是否 192.168.122.10；VM 里查 sshd 服务；公钥是否在 `C:\ProgramData\ssh\administrators_authorized_keys`（注意该文件 ACL 只留 Administrators/SYSTEM；宿主换 key 后可用 qemu-ga 推入，见「重建」一节） |
| guest 覆盖宿主属主旧文件失败（如 `.ico`、`dist/`） | 构建脚本会先删 `.ico`；`dist/` 若是宿主侧建的旧目录，宿主 `rm -rf dist` 后重试 |
| NSIS 报 `Bad text encoding` 或 makensis 打印用法退出 1 | nsi 是 UTF-8 无 BOM，中文系统（ACP=GBK）的 makensis 3.10 默认按 GBK 读会报前者。脚本已带 `/INPUTCHARSET UTF8` 修复；注意该参数值与选项**空格分隔**，写成 `/INPUTCHARSET=UTF8` 会被当未知参数直接打印用法退出 |
| 版本号不对 | 打包脚本从 `CMakeLists.txt` 读版本，与 CI 同源 |
| 构建失败 | 日志透传到宿主终端、包装器统一 exit 1（MSVC 退出码见错误消息；输出中文可能显示为 GBK 乱码，不影响判定）；预检模式下 VM 内环境故障 exit 90/255 放行为 75；按 MSVC 常规错误处理 |
| 改了含中文的 .ps1 后 VM 里行为怪异（报错行号对不上、某条命令"消失"） | Windows PowerShell 5.1 读**无 BOM** 的 .ps1 按 GBK 解码，中文注释尾字节可能吞掉换行导致行粘连（CI 用 pwsh 默认 UTF-8，无此问题）。仓库 .ps1 必须存成 UTF-8 带 BOM。**cmd 批处理（.cmd/.bat）同理必须纯 ASCII**（2026-10-12 实测 UTF-8 中文注释被 GBK 切碎成乱码命令） |
| 重装后图标仍是旧版（如白角）但 exe 内嵌图标已验证正确 | Windows 图标缓存按路径缓存且**落盘**——实测仅重启 VM 不会清除（重启后依旧白角），必须删 `%LOCALAPPDATA%\IconCache.db` 与 `%LOCALAPPDATA%\Microsoft\Windows\Explorer\iconcache_*` 后重启 explorer（或重启 VM）才重新提取。物理机同理（2026-08-22 VM 实测生效，机制与机器无关）。PowerShell 下执行：`taskkill /f /im explorer.exe; Remove-Item -Force "$env:LOCALAPPDATA\IconCache.db" -ErrorAction SilentlyContinue; Remove-Item -Force "$env:LOCALAPPDATA\Microsoft\Windows\Explorer\iconcache_*"; Start-Process explorer.exe`。**注意 `del /a /f /q` 是 cmd 语法，PowerShell 里 `del` 是 `Remove-Item` 别名，不认这些开关会报 ParameterBindingException**——要么用上面的 `Remove-Item`，要么切到 cmd 用 `del /a /f /q "%LOCALAPPDATA%\...\iconcache_*"`。验证 exe 图标是否真有问题：`[System.Drawing.Icon]::ExtractAssociatedIcon('<exe>').ToBitmap().GetPixel(0,0).A`，0 为透明 |

## 维护

- **升级 Qt**（如跟进 CI 版本）：VM 里重跑 `py -3 -m aqt install-qt windows desktop <版本> win64_msvc2022_64 -O D:\Qt -m qtwebengine qtwebchannel qtpositioning`，并同步改 `scripts/build-windows-vm.ps1` 里的 `$qtDir`
- **调整 VM 资源**：`virsh setvcpus win11 <N> --maximum --config && virsh setvcpus win11 <N> --config`；内存同理 `setmaxmem`/`setmem`（KiB），重启 VM 生效
- **回滚到搭建前**：`virsh snapshot-revert win11 before-build-vm`（会丢数据盘上后来装的工具链，慎用）
- **重建 guest 系统**：数据盘（工具链）在独立 qcow2 上（`~/qemu/win11/win10-data.qcow2`，文件名历史遗留），系统盘重装/宿主重装后按上方「重建 guest 系统」一节操作；guest 里装 guest tools + 重跑 provision 即可复用 `D:\BuildTools`、`D:\Qt`
