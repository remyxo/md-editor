# guest 内构建脚本：由宿主 scripts/build-windows-local.sh 经 ssh 触发（经 C:\md-editor-build-launch.ps1），也可在 VM 里手动运行
# 前置：provision-windows-vm.ps1 已跑过（工具链就绪）
param(
    [string]$SrcRoot = "",     # 仓库共享根目录；为空时按卷标 md-editor-src 自动定位
    [switch]$CompileOnly       # 预检模式：只配置+编译，跳过图标生成与打包（pre-push 钩子用）
)
$ErrorActionPreference = "Stop"

# ---- 0. 定位源码共享 ----
if (-not $SrcRoot) {
    # virtiofs 卷不出现在 Get-Volume（WinFsp 挂载），用 Win32_LogicalDisk 按卷标（=tag）定位；
    # 限定 FileSystem='VirtIO-FS'，排除原服务不带 -t 抢挂时留下的 NTFS 同卷标假盘
    $dev = (Get-CimInstance Win32_LogicalDisk -Filter "VolumeName='md-editor-src' AND FileSystem='VirtIO-FS'" -ErrorAction SilentlyContinue |
            Select-Object -First 1).DeviceID
    if (-not $dev) { Write-Error "找不到 virtiofs 共享 md-editor-src（VirtioFsSvc2 服务？重跑 provision）"; exit 90 }
    $SrcRoot = "$dev\"
}
Write-Host "==> 源码: $SrcRoot"

# ---- 1. D: 空间检查 ----
if ((Get-PSDrive D).Free / 1GB -lt 5) { Write-Error "D: 剩余空间不足 5G（windeployqt/NSIS 暂存需要）"; exit 1 }

# ---- 2. VS 开发环境（MSVC + VS 自带 CMake/Ninja 进 PATH）----
$devshell = 'D:\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
if (-not (Test-Path $devshell)) { Write-Error "未找到 VS DevShell（provision 步骤 3）"; exit 1 }
Import-Module $devshell
# sshd 非交互会话里 vswhere 自动探测不可靠，显式给安装路径
Enter-VsDevShell -VsInstallPath 'D:\BuildTools' -SkipAutomaticLocation -DevCmdArguments '-arch=x64' | Out-Null

# ---- 3. Qt 与 ImageMagick 进 PATH ----
$qtDir = 'D:\Qt\6.8.2\msvc2022_64'
if (-not (Test-Path "$qtDir\bin\windeployqt.exe")) { Write-Error "未找到 Qt（provision 步骤 4）"; exit 1 }
$env:Path = "$qtDir\bin;$env:Path"
$env:CMAKE_PREFIX_PATH = "$qtDir"
$magick = Get-ChildItem "$env:ProgramFiles\ImageMagick-*\magick.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
if ($magick) { $env:Path = "$($magick.Directory);$env:Path" }
elseif (-not $CompileOnly) { Write-Error "未找到 magick.exe（provision 步骤 5）"; exit 1 }

# ---- 4. 应用图标（内容有变才替换、mtime 不动则 ninja 无需重编 .rc，见 generate-icons-win.ps1；
#      勿在此预删旧 ico——会把 hash 跳过分支打掉，每次都重链 exe；-CompileOnly 跳过）----
if (-not $CompileOnly) {
    & (Join-Path $SrcRoot 'scripts\generate-icons-win.ps1')
}

# ---- 5. 配置 + 构建（build 在 D: 本地盘，源码在共享卷 W:，provision 已钉死盘符）----
# 缓存守卫：CMake 缓存内嵌绝对源路径，与当前源路径不符（盘符漂移的历史遗留，或
# 手动 -SrcRoot 指到别处）会硬报「does not match the source used to generate
# cache」——自动清掉失效缓存重新配置（一次性代价，换后续增量构建）
$srcNorm = $SrcRoot.TrimEnd('\').Replace('\', '/')
if (Test-Path 'D:\build\CMakeCache.txt') {
    $hit = Select-String -Path 'D:\build\CMakeCache.txt' -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$' | Select-Object -First 1
    if ($hit -and $hit.Matches[0].Groups[1].Value.TrimEnd('/') -ne $srcNorm) {
        Write-Host "==> 源路径已变（$($hit.Matches[0].Groups[1].Value) → $srcNorm），清理失效 CMake 缓存"
        Remove-Item -Force 'D:\build\CMakeCache.txt' -ErrorAction SilentlyContinue
        Remove-Item -Recurse -Force 'D:\build\CMakeFiles' -ErrorAction SilentlyContinue
    }
}
Push-Location $SrcRoot
try {
    Write-Host "==> cmake 配置（D:\build，源 $SrcRoot）"
    cmake -S $SrcRoot -B D:\build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
    if ($LASTEXITCODE -ne 0) { Write-Error "cmake 配置失败 (exit $LASTEXITCODE)"; exit $LASTEXITCODE }

    Write-Host "==> 构建"
    cmake --build D:\build
    if ($LASTEXITCODE -ne 0) { Write-Error "构建失败 (exit $LASTEXITCODE)"; exit $LASTEXITCODE }

    # ---- 6. 打包：windeployqt → NSIS 安装器（产物落共享卷 dist\ = 宿主仓库 dist/）----
    # 2026-09-27 用户定版：Windows 分发只保留 NSIS 安装器，不再产便携 zip
    if (-not $CompileOnly) {
        Write-Host "==> 打包"
        & (Join-Path $SrcRoot 'scripts\package-windows.ps1') -BuildDir 'D:\build' -SkipZip
    } else {
        Write-Host "==> 编译通过（-CompileOnly，跳过打包）"
    }
} finally {
    Pop-Location
}
exit 0
