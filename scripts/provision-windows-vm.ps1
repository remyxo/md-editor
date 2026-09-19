#Requires -RunAsAdministrator
# win11 构建机一键初始化（guest 内管理员 PowerShell 运行，可重复执行）
# 完整流程见 docs/windows-build-vm.md。宿主侧先跑 scripts/setup-windows-vm-host.sh。
#
# 步骤：
#   1. D: 数据盘（NTFS）
#   2. 第二个 virtiofs 共享实例（挂宿主仓库，卷标 md-editor-src）
#   3. VS 2022 Build Tools → D:\BuildTools（MSVC v143 + Win11 SDK + CMake + Ninja）
#   4. Python + aqtinstall → Qt 6.8.2 msvc2022_64 + webengine/webchannel/positioning → D:\Qt
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
$InstallerDir = 'D:\installers'            # 落数据盘：断点复用，也避开 C: 的 TEMP（曾出过损坏文件）
New-Item -ItemType Directory -Path $InstallerDir -Force | Out-Null
function Get-Installer([string]$name, [string]$uri, [string]$file) {
    $out = Join-Path $InstallerDir $file
    if (Test-Path $out) { Write-Host "    $name 已就位，复用"; return $out }
    # 优先从宿主 Downloads 共享拷（宿主预下好的最稳；guest 直连镜像站曾拿到坏内容）
    $share = (Get-CimInstance Win32_LogicalDisk -Filter "VolumeName='daijd-shared'" -ErrorAction SilentlyContinue |
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

# ---------- 1. D: 数据盘 ----------
Step "1/8 D: 数据盘"
if (Get-Volume -DriveLetter D -ErrorAction SilentlyContinue) {
    Ok "D: 已存在"
} else {
    $raw = Get-Disk | Where-Object { $_.PartitionStyle -eq 'RAW' } | Select-Object -First 1
    if (-not $raw) { throw "D: 不存在且找不到未初始化磁盘——宿主侧 setup-windows-vm-host.sh 是否已加数据盘？" }
    Initialize-Disk -Number $raw.Number -PartitionStyle GPT
    New-Partition -DiskNumber $raw.Number -UseMaximumSize -DriveLetter D |
        Format-Volume -FileSystem NTFS -NewFileSystemLabel build-data
    Ok "已初始化磁盘 $($raw.Number) 为 D: (NTFS)"
}

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
    #        盘符——D:\build 的 CMake 缓存内嵌绝对源路径，盘符一漂（实测 Z:→Y:）即报
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
Step "3/8 VS 2022 Build Tools → D:\BuildTools（约 10-25 分钟）"
if (Test-Path 'D:\BuildTools\VC\Tools\MSVC') {
    Ok "已安装，跳过"
} else {
    $exe = Get-Installer 'VS BuildTools 引导器' 'https://aka.ms/vs/17/release/vs_BuildTools.exe' 'vs_BuildTools.exe'
    Write-Host "    静默安装中（无窗口，耐心等）..."
    $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList `
        '--quiet','--wait','--norestart','--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended','--installPath','D:\BuildTools'
    if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) { throw "VS Build Tools 安装失败 (exit $($p.ExitCode))" }
    Refresh-Path
    Ok "安装完成（exit $($p.ExitCode)；3010=需重启，可忽略）"
}

# ---------- 4. Python + Qt 6.8.2 ----------
Step "4/8 Qt 6.8.2 msvc2022_64 → D:\Qt（下载约 2G）"
if (-not (Get-Command py -ErrorAction SilentlyContinue)) {
    $exe = Get-Installer 'Python 3.12.10' 'https://www.python.org/ftp/python/3.12.10/python-3.12.10-amd64.exe' 'python-3.12.10-amd64.exe'
    $p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList '/quiet','InstallAllUsers=1','PrependPath=1'
    if ($p.ExitCode -ne 0) { throw "Python 安装失败 (exit $($p.ExitCode))" }
    Refresh-Path
    Ok "Python 已安装"
} else { Ok "Python 已存在" }

if (Test-Path 'D:\Qt\6.8.2\msvc2022_64\bin\windeployqt.exe') {
    Ok "Qt 已安装，跳过"
} else {
    py -3 -m pip install --quiet --upgrade aqtinstall
    if ($LASTEXITCODE -ne 0) { throw "aqtinstall 安装失败 (exit $LASTEXITCODE)" }
    py -3 -m aqt install-qt windows desktop 6.8.2 win64_msvc2022_64 `
        -O D:\Qt -m qtwebengine qtwebchannel qtpositioning
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
    $installed = $false
    # 首选 Windows 功能（走 Windows Update，可能被策略/网络挡：0x8024002e）
    try {
        $cap = Get-WindowsCapability -Online | Where-Object { $_.Name -like 'OpenSSH.Server*' }
        if ($cap.State -ne 'Installed') { Add-WindowsCapability -Online -Name $cap.Name | Out-Null }
        $installed = $true
        Ok "OpenSSH Server（Windows 功能）已安装"
    } catch {
        Warn "Add-WindowsCapability 失败（0x8024002e?）——改用 GitHub Win32-OpenSSH 离线包"
    }
    if (-not $installed) {
        # 离线包：宿主预下的 OpenSSH-Win64.zip（经 daijd-shared 共享）
        $share = (Get-CimInstance Win32_LogicalDisk -Filter "VolumeName='daijd-shared'" -ErrorAction SilentlyContinue |
            Select-Object -First 1).DeviceID
        $zip = if ($share) { "$share\OpenSSH-Win64.zip" } else { $null }
        if (-not ($zip -and (Test-Path $zip))) {
            throw "共享无 OpenSSH-Win64.zip。宿主执行：gh release download 10.0.0.0p2-Preview -R PowerShell/Win32-OpenSSH -p OpenSSH-Win64.zip -o ~/Downloads/ 后重跑"
        }
        Expand-Archive $zip 'C:\Program Files\OpenSSH' -Force
        & 'C:\Program Files\OpenSSH\OpenSSH-Win64\install-sshd.ps1' | Out-Null
        Ok "OpenSSH Server（Win32-OpenSSH 离线包）已安装"
    }
}
Set-Service sshd -StartupType Automatic
if ((Get-Service sshd).Status -ne 'Running') { Start-Service sshd }
Ok "sshd 运行中（开机自启）"

if (-not (Get-LocalUser -Name 'builder' -ErrorAction SilentlyContinue)) {
    # 登录走 ssh 公钥，密码仅是账户占位（Windows 禁止空密码账户网络登录）；
    # 随机生成、存档到 VM 本地文件，无需人工输入
    $rng = New-Object Security.Cryptography.RNGCryptoServiceProvider
    $bytes = New-Object byte[] 24
    $rng.GetBytes($bytes)
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
Set-Content -Path 'C:\md-editor-build-launch.ps1' -Value $launch -Encoding UTF8
Ok "已生成"

# ---------- 8. 自检 ----------
Step "8/8 自检清单"
function Check($name, $ok) {
    if ($ok) { Ok $name } else { Warn "$name —— 缺失（重跑本脚本或见 docs/windows-build-vm.md）" }
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$msvcPath = if (Test-Path $vswhere) { & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath } else { '' }
Check "MSVC v143（D:\BuildTools）"        ($msvcPath -like 'D:\BuildTools*')
Check "CMake（VS 自带）"                  (Test-Path 'D:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
Check "Ninja（VS 自带）"                  (Test-Path 'D:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe')
Check "Qt 6.8.2 windeployqt"              (Test-Path 'D:\Qt\6.8.2\msvc2022_64\bin\windeployqt.exe')
Check "Qt6WebEngineCore.dll"              (Test-Path 'D:\Qt\6.8.2\msvc2022_64\bin\Qt6WebEngineCore.dll')
Check "makensis"                          ([bool]($makensisCandidates | Where-Object { Test-Path $_ }))
Check "magick"                            ([bool](Get-ChildItem "$env:ProgramFiles\ImageMagick-*\magick.exe" -ErrorAction SilentlyContinue))
Check "sshd 运行中"                       ((Get-Service sshd).Status -eq 'Running')
Check "仓库共享（md-editor-src）"          ([bool](Get-ShareDrive 'md-editor-src'))
Check "ssh 构建入口"                       (Test-Path 'C:\md-editor-build-launch.ps1')

Write-Host "`n=== 初始化完成。宿主侧验证：./scripts/build-windows-local.sh ===" -ForegroundColor Cyan
