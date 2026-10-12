#Requires -RunAsAdministrator
# win11 构建机一键初始化（guest 内管理员 PowerShell 运行，可重复执行）
# 完整流程见 docs/windows-build-vm.md。宿主侧先跑 scripts/setup-windows-vm-host.sh。
#
# 步骤：
#   1. 数据盘（按卷标 build-data 定位，尽力钉 D:，全脚本经 $DataRoot 引用——防盘符漂移）
#   2. 第二个 virtiofs 共享实例（挂宿主仓库，卷标 md-editor-src，盘符钉 W:）
#   3. VS 2022 Build Tools → <数据盘>:\BuildTools（MSVC v143 + Win11 SDK + CMake + Ninja）
#   4. Python + aqtinstall → Qt 6.8.2 msvc2022_64 + webengine/webchannel/positioning → <数据盘>:\Qt
#   5. NSIS + ImageMagick
#   6. OpenSSH Server + builder 用户 + 公钥
#   7. 生成 C:\md-editor-build-launch.ps1（宿主 ssh 的固定入口）
#   8. 自检清单
#
# 用法：
#   .\provision-windows-vm.ps1                                  # 不传公钥，事后手动配
#   .\provision-windows-vm.ps1 -SshPublicKey "ssh-ed25519 AAAA... user@host"
param(
    [string]$SshPublicKey = ""
)
$ErrorActionPreference = "Stop"
$ShareTag = "md-editor-src"

function Step($msg)  { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Ok($msg)    { Write-Host "    [OK] $msg" -ForegroundColor Green }
function Warn($msg)  { Write-Host "    [!]  $msg" -ForegroundColor Yellow }
function Refresh-Path {
    $env:Path = [System.Environment]::GetEnvironmentVariable('Path','Machine') + ';' +
                [System.Environment]::GetEnvironmentVariable('Path','User')
}
# 本机没有 winget（App Installer 缺失），所有工具改直连官方安装包静默安装
$ProgressPreference = 'SilentlyContinue'   # 关进度条：PS5.1 下 Invoke-WebRequest 提速一个数量级
function Get-Installer([string]$name, [string]$uri, [string]$file) {
    $out = Join-Path $InstallerDir $file
    if (Test-Path $out) { Write-Host "    $name 已就位，复用"; return $out }
    # 优先从宿主 Downloads 共享拷（宿主预下好的最稳；guest 直连镜像站曾拿到坏内容）
    $share = (Get-CimInstance Win32_LogicalDisk -Filter "VolumeName='daijd-shared' AND FileSystem='VirtIO-FS'" -ErrorAction SilentlyContinue |
        Select-Object -First 1).DeviceID
    if ($share -and (Test-Path "$share\$file")) {
        Copy-Item "$share\$file" $out
        Write-Host "    $name 从宿主共享拷入"
    } elseif ($uri) {
        Write-Host "    下载 $name ..."
        Invoke-WebRequest -Uri $uri -OutFile $out -UseBasicParsing `
            -UserAgent 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
    } else {
        throw "$name 无处获取：宿主共享无此文件且未提供下载地址"
    }
    # 校验 PE 头（MZ）+ 体积，防止把镜像站错误页/半截文件当安装器
    $ok = $false
    try {
        $fs = [IO.File]::OpenRead($out)
        $m = $fs.ReadByte(); $z = $fs.ReadByte(); $fs.Close()
        $ok = ($m -eq 0x4D -and $z -eq 0x5A -and (Get-Item $out).Length -gt 100KB)
    } catch { $ok = $false }
    if (-not $ok) {
        Remove-Item $out -Force -ErrorAction SilentlyContinue
        throw "$name 内容不是完整可执行文件（下载被截/安全软件拦？）。可在宿主执行 curl -L -o ~/Downloads/$file <官方地址> 后重跑"
    }
    return $out
}

# ---------- 1. 数据盘（按卷标 build-data 定位，防盘符漂移） ----------
# 坑：数据盘盘符由「当前系统」的挂载管理器分配（持久化在本系统注册表），重装/换 VM 后
#     不保证还是 D:（CD-ROM 或其他卷可能抢 D:）。两层防御：
#   a) 本脚本所有数据盘路径经 $DataRoot（按卷标解析，盘符无关）——脚本层
#   b) 解析出非 D: 且 D: 空闲时钉回 D:——工具链层（VS/Qt 注册表内嵌 D:\BuildTools、
#      D:\Qt 绝对路径，盘符漂移直接失效，钉回可复用旧数据盘上已装好的工具链）
# 实现通道：WMI Win32_LogicalDisk + diskpart/label（系统核心组件）。不用 Get-Volume 等
#     Storage 管理模块——它们属 Windows PowerShell 系统模块，精简 LTSB 会整个删掉（2026-10-12
#     实测 Get-Volume 不存在）；Win32_LogicalDisk 走 WMI 服务，任何系统都在
Step "1/8 数据盘（卷标 build-data）"
function Get-DataDisk {
    Get-CimInstance Win32_LogicalDisk -Filter "VolumeName='build-data' AND FileSystem='NTFS'" -ErrorAction SilentlyContinue |
        Select-Object -First 1
}
$data = Get-DataDisk
if (-not $data) {
    # 兼容无卷标的既有数据盘：D: 存在且 NTFS 就收编并补卷标（label.exe 为系统组件）
    $dvol = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='D:' AND FileSystem='NTFS'" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($dvol) {
        label D: build-data
        $data = Get-DataDisk
        Ok "收编既有 D:（NTFS），补卷标 build-data"
    }
}
if ($data) {
    $letter = $data.DeviceID.TrimEnd(':')
    if ($letter -ne 'D') {
        $dUsed = Get-CimInstance Win32_LogicalDisk -Filter "DeviceID='D:'" -ErrorAction SilentlyContinue
        if (-not $dUsed) {
            # diskpart 钉回 D:（remove 再 assign，避免「盘符已指派」冲突）
            $dp = "$env:TEMP\dp-fixletter.txt"
            Set-Content -Path $dp -Value "select volume $letter`r`nremove letter=$letter`r`nassign letter=D" -Encoding ASCII
            diskpart /s $dp | Out-Null
            $data = Get-DataDisk
            Ok "数据盘钉回 D:（原 ${letter}:，卷标 build-data）"
        } else {
            Warn "数据盘在 ${letter}: 而 D: 被其他卷占用——D:\BuildTools/D:\Qt 的注册表路径可能失效，建议磁盘管理手动改回 D:"
        }
    } else { Ok "D: 已存在（卷标 build-data）" }
} else {
    # 全新未初始化盘：本机用不到（旧盘已带工具链），留明确指引不自动化
    throw "找不到数据盘（无卷标 build-data、无 NTFS 的 D:）。若是全新盘：磁盘管理手动初始化 GPT → 新建简单卷(NTFS, 卷标 build-data, 盘符 D:) 后重跑"
}
if (-not $data) { throw "数据盘定位后意外为空（diskpart 钉盘符后未出现？）" }
$DataRoot = $data.DeviceID          # 如 'D:'（DeviceID 自带冒号）
$InstallerDir = "$DataRoot\installers"      # 落数据盘：断点复用，也避开 C: 的 TEMP（曾出过损坏文件）
New-Item -ItemType Directory -Path $InstallerDir -Force | Out-Null

# ---------- 2. virtiofs 双共享（两个服务都必须带明确 tag）----------
Step "2/8 virtiofs 共享 '$ShareTag'"
# 坑 1：WinFsp 挂载的 virtiofs 卷不出现在 Get-Volume 里，必须用 Win32_LogicalDisk 探测
# 坑 2：virtiofs.exe 不带 -t 时会抢挂开机后先枚举到的设备（且挂出的卷 FileSystem 显示 NTFS、
#        卷标跟随设备 tag），曾造成「两个盘同卷标 md-editor-src、Downloads 共享消失」——
#        所以原服务也要补上明确 tag，探测也要限定 FileSystem='VirtIO-FS' 排除假盘
$exe = 'C:\Program Files\Virtio-Win\VioFS\virtiofs.exe'
if (-not (Test-Path $exe)) { throw "找不到 $exe——请先安装 virtio-win guest tools（含 VioFS）" }

function Get-ShareDrive([string]$label) {
    (Get-CimInstance Win32_LogicalDisk -Filter "VolumeName='$label' AND FileSystem='VirtIO-FS'" -ErrorAction SilentlyContinue |
        Select-Object -First 1).DeviceID
}
function Ensure-VirtioFsService([string]$name, [string]$tag, [string]$mount = '*') {
    # 坑 3：-m '*' 自动找空盘符（WinFsp 从 Z: 倒序扫），两个服务开机顺序不定会互换
    #        盘符——数据盘 build 目录的 CMake 缓存内嵌绝对源路径，盘符一漂（实测 Z:→Y:）即报
    #        「does not match the source used to generate cache」。仓库共享钉死 W:
    #        （在自动扫号区 Z:/Y: 之外，永不冲突）；Downloads 共享无路径依赖，保持自动
    $want = "`"$exe`" -t $tag -m $mount"
    if (Get-Service -Name $name -ErrorAction SilentlyContinue) {
        $cur = (Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\$name" -Name ImagePath).ImagePath
        if ($cur -ne $want) {
            Set-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Services\$name" -Name ImagePath -Value $want
            if ((Get-Service -Name $name).Status -eq 'Running') { Restart-Service -Name $name -Force }
            Ok "$name binPath 已修正（-t $tag -m $mount）"
        }
    } else {
        # 用 New-Service 而非 sc.exe：PS 5.1 给原生命令传内嵌引号会错乱（服务建不出来的根因）
        New-Service -Name $name -BinaryPathName $want -StartupType Automatic | Out-Null
        Ok "已创建服务 $name（-t $tag -m $mount）"
    }
    if ((Get-Service -Name $name).Status -ne 'Running') { Start-Service -Name $name }
}
Ensure-VirtioFsService 'VirtioFsSvc'  'daijd-shared'   # 原服务（宿主 Downloads，装 guest tools 时自带）
Ensure-VirtioFsService 'VirtioFsSvc2' $ShareTag 'W:'   # 本脚本加的（宿主仓库，盘符固定 W:）

$drive = Get-ShareDrive $ShareTag
if (-not $drive) {
    # 已知坑：原服务曾不带 tag 与 Svc2 挂在同一设备上，释放其中一个会话会把另一个也
    # 带崩（服务仍显示 Running 但盘消失）——重启挂载服务重新抢一次
    Restart-Service VirtioFsSvc2 -Force
    foreach ($i in 1..15) {
        $drive = Get-ShareDrive $ShareTag
        if ($drive) { break }
        Start-Sleep 1
    }
}
if (-not $drive) {
    throw "卷标 $ShareTag 未出现。VirtioFsSvc=$((Get-Service VirtioFsSvc).Status) VirtioFsSvc2=$((Get-Service VirtioFsSvc2).Status)"
}
# 共享可写性：构建要往仓库写 .ico 与 dist/，宿主 ACL 没配好现在就暴露
$probe = Join-Path $drive '.__write_probe__'
try   { Set-Content -Path $probe -Value 'ok'; Remove-Item $probe }
catch { throw "共享 $drive 不可写——宿主侧 setfacl 是否给 libvirt-qemu rwX？" }
Ok "仓库共享已挂载为 $drive（卷标 $ShareTag，可写）"

# ---------- 3. VS Build Tools（官方引导器直装，与 winget 同一安装器同一参数）----------
Step "3/8 VS 2022 Build Tools → $DataRoot\BuildTools（约 10-25 分钟）"
if (Test-Path "$DataRoot\BuildTools\VC\Tools\MSVC") {
    Ok "已安装，跳过"
} else {
    $exe = Get-Installer 'VS BuildTools 引导器' 'https://aka.ms/vs/17/release/vs_BuildTools.exe' 'vs_BuildTools.exe'
    Write-Host "    静默安装中（无窗口，耐心等）..."
    $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList `
        '--quiet','--wait','--norestart','--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended','--installPath',"$DataRoot\BuildTools"
    if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { throw "VS Build Tools 安装失败 (exit $($p.ExitCode))" }
    Refresh-Path
    Ok "安装完成（exit $($p.ExitCode)；3010=需重启，可忽略）"
}

# ---------- 4. Python + Qt 6.8.2 ----------
Step "4/8 Qt 6.8.2 msvc2022_64 → $DataRoot\Qt（下载约 2G）"
if (Test-Path "$DataRoot\Qt\6.8.2\msvc2022_64\bin\windeployqt.exe") {
    Ok "Qt 已安装，跳过（Python 仅作 aqtinstall 下载通道，Qt 既在则一并跳过）"
} else {
    if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
        $exe = Get-Installer 'Python 3.12.10' 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe' 'python-3.12.10-amd64.exe'
        $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList '/quiet','InstallAllUsers=1','PrependPath=1'
        if ($p.ExitCode -ne 0) { throw "Python 安装失败 (exit $($p.ExitCode))" }
        Refresh-Path
        Ok "Python 已安装"
    } else { Ok "Python 已存在" }
    py -3 -m pip install --quiet --upgrade aqtinstall
    if ($LASTEXITCODE -ne 0) { throw "aqtinstall 安装失败 (exit $LASTEXITCODE)" }
    py -3 -m aqt install-qt windows desktop 6.8.2 win64_msvc2022_64 `
        -O $DataRoot\Qt -m qtwebengine qtwebchannel qtpositioning qtpdf
    if ($LASTEXITCODE -ne 0) { throw "Qt 安装失败 (exit $LASTEXITCODE)" }
    Ok "Qt 6.8.2 安装完成"
}

# ---------- 5. NSIS + ImageMagick ----------
Step "5/8 NSIS + ImageMagick"
$makensisCandidates = @("${env:ProgramFiles(x86)}\NSIS\makensis.exe", "$env:ProgramFiles\NSIS\makensis.exe")
if ($makensisCandidates | Where-Object { Test-Path $_ }) {
    Ok "NSIS 已存在，跳过"
} else {
    $exe = Get-Installer 'NSIS 3.10' 'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.10/nsis-3.10-setup.exe' 'nsis-3.10-setup.exe'
    $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList '/S'
    if ($p.ExitCode -ne 0) { throw "NSIS 安装失败 (exit $($p.ExitCode))" }
    Refresh-Path
    Ok "NSIS 安装完成"
}
if (Get-ChildItem "$env:ProgramFiles\ImageMagick-*\magick.exe" -ErrorAction SilentlyContinue) {
    Ok "ImageMagick 已存在，跳过"
} else {
    # 版本号滚动，从官方目录列表动态抓最新稳定版文件名；宿主共享里有预下好的则直接用（$pick 留空）
    $pick = ''
    try {
        $list = (Invoke-WebRequest -Uri 'https://download.imagemagick.org/archive/binaries/' -UseBasicParsing).Links.href |
            Where-Object { $_ -match '^ImageMagick-[\d\.]+-\d+-Q16-HDRI-x64-dll\.exe$' }
        if ($list) {
            $pick = $list | Select-Object -First 1
            if ($pick -notmatch '^https?://') { $pick = "https://imagemagick.org/archive/binaries/$pick" }
        }
    } catch { }
    $exe = Get-Installer 'ImageMagick' $pick 'ImageMagick-setup.exe'
    $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART'
    if ($p.ExitCode -ne 0) { throw "ImageMagick 安装失败 (exit $($p.ExitCode))" }
    Refresh-Path
    Ok "ImageMagick 安装完成"
}

# ---------- 6. OpenSSH Server + builder 用户 ----------
Step "6/8 OpenSSH Server + builder 用户"
if (Get-Service sshd -ErrorAction SilentlyContinue) {
    Ok "OpenSSH Server 已存在"
} else {
    # 统一走 GitHub Win32-OpenSSH 离线包（不走 Add-WindowsCapability：精简 LTSB 无 DISM
    # 模块且 Windows Update 通道常被挡）。查找顺序：C:\ 预下 → daijd-shared 共享 → 直连 GitHub
    $zip = $null
    if (Test-Path 'C:\OpenSSH-Win64.zip') { $zip = 'C:\OpenSSH-Win64.zip' }
    if (-not $zip) {
        $share = (Get-CimInstance Win32_LogicalDisk -Filter "VolumeName='daijd-shared' AND FileSystem='VirtIO-FS'" -ErrorAction SilentlyContinue |
            Select-Object -First 1).DeviceID
        if ($share -and (Test-Path "$share\OpenSSH-Win64.zip")) { $zip = "$share\OpenSSH-Win64.zip" }
    }
    if (-not $zip) {
        try {
            $out = "$env:TEMP\OpenSSH-Win64.zip"
            Invoke-WebRequest -Uri 'https://github.com/PowerShell/Win32-OpenSSH/releases/latest/download/OpenSSH-Win64.zip' -OutFile $out -UseBasicParsing
            $zip = $out
            Write-Host "    已直连 GitHub 下载离线包"
        } catch { }
    }
    if (-not $zip) {
        throw "OpenSSH-Win64.zip 无处获取。宿主执行：curl -L -o ~/Downloads/OpenSSH-Win64.zip https://github.com/PowerShell/Win32-OpenSSH/releases/latest/download/OpenSSH-Win64.zip 后重跑"
    }
    Expand-Archive $zip 'C:\Program Files\OpenSSH' -Force
    & 'C:\Program Files\OpenSSH\OpenSSH-Win64\install-sshd.ps1' | Out-Null
    Ok "OpenSSH Server（Win32-OpenSSH 离线包）已安装"
}
Set-Service sshd -StartupType Automatic
if ((Get-Service sshd).Status -ne 'Running') { Start-Service sshd }
Ok "sshd 运行中（开机自启）"

if (-not (Get-LocalUser -Name 'builder' -ErrorAction SilentlyContinue)) {
    # 登录走 ssh 公钥，密码仅是账户占位（Windows 禁止空密码账户网络登录）；
    # 随机生成、存档到 VM 本地文件，无需人工输入
    # （Create().GetBytes() 是 PS5.1/.NET Framework 与 pwsh7/.NET 两者共有的 API）
    $bytes = New-Object byte[] 24
    [Security.Cryptography.RandomNumberGenerator]::Create().GetBytes($bytes)
    $pw = [Convert]::ToBase64String($bytes)
    New-LocalUser -Name 'builder' -Password ($pw | ConvertTo-SecureString -AsPlainText -Force) `
        -PasswordNeverExpires -FullName 'md-editor builder'
    Add-LocalGroupMember -Group 'Administrators' -Member 'builder'
    Set-Content -Path 'C:\builder-pass.txt' -Value $pw -Encoding ASCII
    Ok "builder 用户已创建并加入 Administrators（随机密码存于 C:\builder-pass.txt，日常构建用不到）"
} else { Ok "builder 用户已存在" }

$adminKeys = 'C:\ProgramData\ssh\administrators_authorized_keys'
if ($SshPublicKey) {
    $existing = if (Test-Path $adminKeys) { Get-Content $adminKeys -Raw } else { '' }
    if ($existing -notlike "*$SshPublicKey*") { Add-Content -Path $adminKeys -Value $SshPublicKey }
    icacls $adminKeys /inheritance:r /grant 'Administrators:F' /grant 'SYSTEM:F' | Out-Null
    Ok "公钥已写入 $adminKeys"
} else {
    Warn "未传 -SshPublicKey：宿主 ssh 会连不上。事后手动："
    Warn "  notepad $adminKeys   # 粘贴宿主 ~/.ssh/id_ed25519.pub 内容"
    Warn "  icacls `"$adminKeys`" /inheritance:r /grant `"Administrators:F`" /grant `"SYSTEM:F`""
}

# ---------- 7. ssh 构建入口（trampoline）----------
Step "7/8 生成 C:\md-editor-build-launch.ps1"
$launch = @'
# 宿主 ssh 触发的固定入口：按卷标定位仓库共享，再执行其 scripts\build-windows-vm.ps1
# 注意：virtiofs 卷不出现在 Get-Volume，用 Win32_LogicalDisk 探测；
#      限定 FileSystem='VirtIO-FS' 排除原服务抢挂时留下的 NTFS 假盘
param([switch]$CompileOnly)
$ErrorActionPreference = "Stop"
$dev = (Get-CimInstance Win32_LogicalDisk -Filter "VolumeName='md-editor-src' AND FileSystem='VirtIO-FS'" -ErrorAction SilentlyContinue |
        Select-Object -First 1).DeviceID
if (-not $dev) {
    Write-Error "找不到卷标 md-editor-src（VirtioFsSvc2 服务是否运行？）"
    exit 90
}
. ($dev + '\scripts\build-windows-vm.ps1') -CompileOnly:$CompileOnly
'@
# 带 BOM 写出：内容含中文注释，pwsh7 的 Set-Content -Encoding UTF8 无 BOM，万一以后
# 被 PowerShell 5.1 读会按 GBK 行粘连（见 docs/windows-build-vm.md 对应坑条目）
$utf8bom = New-Object System.Text.UTF8Encoding $true
[IO.File]::WriteAllText('C:\md-editor-build-launch.ps1', $launch, $utf8bom)
Ok "已生成"

# ---------- 8. 自检 ----------
Step "8/8 自检清单"
function Check($name, $ok) {
    if ($ok) { Ok $name } else { Warn "$name —— 缺失（重跑本脚本或见 docs/windows-build-vm.md）" }
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msvcPath = if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath } else { '' }
Check "MSVC v143（$DataRoot\BuildTools）"  ($msvcPath -like "$DataRoot\BuildTools*")
Check "CMake（VS 自带）"                  (Test-Path "$DataRoot\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
Check "Ninja（VS 自带）"                  (Test-Path "$DataRoot\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
Check "Qt 6.8.2 windeployqt"              (Test-Path "$DataRoot\Qt\6.8.2\msvc2022_64\bin\windeployqt.exe")
Check "Qt6WebEngineCore.dll"              (Test-Path "$DataRoot\Qt\6.8.2\msvc2022_64\bin\Qt6WebEngineCore.dll")
Check "makensis"                          ([bool]($makensisCandidates | Where-Object { Test-Path $_ }))
Check "magick"                            ([bool](Get-ChildItem "$env:ProgramFiles\ImageMagick-*\magick.exe" -ErrorAction SilentlyContinue))
Check "sshd 运行中"                       ((Get-Service sshd).Status -eq 'Running')
Check "仓库共享（md-editor-src）"          ([bool](Get-ShareDrive 'md-editor-src'))
Check "ssh 构建入口"                       (Test-Path 'C:\md-editor-build-launch.ps1')

Write-Host "`n=== 初始化完成。宿主侧验证：./scripts/build-windows-local.sh ===" -ForegroundColor Cyan
