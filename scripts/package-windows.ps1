# Windows 打包：windeployqt 捆绑 Qt → NSIS 安装器（-SkipZip 可另产便携 zip）
# 用法: .\scripts\package-windows.ps1 [-Version <版本号>] [-BuildDir <构建目录>] [-SkipZip]
#   -BuildDir：本地 VM 构建时 build 在 D:\build（见 docs/windows-build-vm.md）；
#              不传则用仓库内 build\（CI 行为，保持不变）
#   -SkipZip：跳过便携 zip 只产 NSIS 安装器（2026-09-27 用户定版：Windows 分发
#              只保留安装器，VM 构建恒传此开关；zip 单份 139MB 也是 Actions
#              500MB 存储配额超限主因）
param(
    [string]$Version = "",
    [string]$BuildDir = "",
    [switch]$SkipZip
)
$ErrorActionPreference = "Stop"

$root  = Split-Path -Parent $PSScriptRoot
$build = if ($BuildDir) { $BuildDir } else { Join-Path $root "build" }
$exe   = Join-Path $build "src\md-editor.exe"
$dist  = Join-Path $root "dist\md-editor"

# 版本号：优先参数，否则从 CMakeLists.txt 读取
if (-not $Version) {
    $cmake = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
    if ($cmake -match 'project\(md-editor VERSION ([0-9.]+)') {
        $Version = $Matches[1]
    } else {
        $Version = "0.0.0"
    }
}
Write-Host "版本: $Version"

# 1. 清空并建立暂存目录
if (Test-Path (Split-Path -Parent $dist)) { Remove-Item -Recurse -Force (Split-Path -Parent $dist) }
New-Item -ItemType Directory -Force -Path $dist | Out-Null

# 2. 拷贝 exe 到暂存目录
Copy-Item $exe $dist

# 2.2 文档图标（.md/.markdown 文件类型 DefaultIcon 用，NSIS 安装器注册引用；
#      由 generate-icons-win.ps1 生成，缺文件时跳过并告警）
$docIcon = Join-Path $root "src\resources\icons\text-markdown.ico"
if (Test-Path $docIcon) { Copy-Item $docIcon $dist } else { Write-Warning "text-markdown.ico 不存在，文档图标将缺失（先跑 generate-icons-win.ps1）" }

# 2.5 PE 子系统校验：必须是 GUI(2)。若是控制台(3)（add_executable 漏 WIN32 标志），
#     双击启动会附带一个黑色空控制台窗口——此处拦截，防止回归出厂
$fs = [System.IO.File]::OpenRead((Join-Path $dist "md-editor.exe"))
$br = New-Object System.IO.BinaryReader($fs)
$fs.Seek(0x3C, 'Begin') | Out-Null
$peOff = $br.ReadInt32()
$fs.Seek($peOff + 4 + 20 + 68, 'Begin') | Out-Null   # PE 签名(4)+COFF 头(20)+OptionalHeader.Subsystem(68)
$subsys = $br.ReadUInt16()
$fs.Close()
if ($subsys -ne 2) { throw "md-editor.exe PE 子系统为 $subsys（应为 2/GUI）——add_executable 是否漏了 WIN32 标志？" }
Write-Host "PE 子系统校验: GUI (2) 通过"

# 3. windeployqt 部署 Qt DLL / WebEngine 资源。--no-compiler-runtime 显式关闭
#    VC 运行库安装器部署：Qt 6.8 的 windeployqt **默认即部署** vc_redist.x64.exe
#    （~25.6MB；2026-10-08 VM 内带 VS DevShell 环境实测确认——此前"去掉
#    --compiler-runtime 就不再部署"的判断是错的，该开关只是强制开，不带则
#    走默认开）。NSIS 从不执行它，装完只是 $INSTDIR 死重；缺运行库的机器需
#    用户自行安装 VC++ Redistributable（基线既有状况）。--no-translations 维持
& windeployqt --release --no-compiler-runtime --no-translations (Join-Path $dist "md-editor.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败 (exit $LASTEXITCODE)" }

# 3.5 安全瘦身：DevTools 前端资源（编辑器不用；CDP 取证走 WebSocket 不需要
#     前端）与 QML 调试工具（非 QML 应用）；vc_redist 行为回归护栏——
#     上方 --no-compiler-runtime 已从源头关闭部署，此处兜底防止该开关被
#     误删后 25.6MB 死重回装进安装器
$devtools = Join-Path $dist "resources\qtwebengine_devtools_resources.pak"
if (Test-Path $devtools) { Remove-Item -Force $devtools; Write-Host "已删 devtools pak (~9.2MB)" }
$qmltooling = Join-Path $dist "qmltooling"
if (Test-Path $qmltooling) { Remove-Item -Recurse -Force $qmltooling; Write-Host "已删 qmltooling (~1MB)" }
$vcRedist = Join-Path $dist "vc_redist.x64.exe"
if (Test-Path $vcRedist) { Remove-Item -Force $vcRedist; Write-Host "已删 vc_redist 残留 (~25.6MB)" }

# 3.6 virtiofs 删除传播确认（2026-10-08 实锤）：删除到目录枚举消失之间有
#     毫秒级窗口，紧随其后的 NSIS File /r 会枚举到尚未传播完的条目并在打开
#     时报 "failed opening file …" 中止打包（vc_redist.x64.exe 实测中招；
#     同批删得早两行的 devtools pak 无恙——时序窗口而非锁）。此处以目录
#     枚举（与 NSIS File /r 同用 FindFirstFile/FindNextFile 系）轮询到条目
#     真正消失再进入打包；超时硬错，不把问题推给 makensis
foreach ($p in @($devtools, $qmltooling, $vcRedist)) {
    $parent = Split-Path -Parent $p
    $leaf   = Split-Path -Leaf $p
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-ChildItem -Force -ErrorAction SilentlyContinue $parent |
            Where-Object { $_.Name -eq $leaf }) -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 150
    }
    if (Get-ChildItem -Force -ErrorAction SilentlyContinue $parent |
        Where-Object { $_.Name -eq $leaf }) {
        throw "删除未传播完成：$p 仍在目录枚举中（virtiofs 滞后超 15s）"
    }
}

# 4. 便携 zip（与 NSIS 安装器统一落 dist\，不散在仓库根；-SkipZip 跳过）
if (-not $SkipZip) {
    $zip = Join-Path $root "dist\md-editor-$Version-win64.zip"
    if (Test-Path $zip) { Remove-Item $zip }
    Compress-Archive -Path (Join-Path $dist "*") -DestinationPath $zip
}

# 5. NSIS 安装器（在仓库根目录运行，nsi 内相对路径依赖此 CWD）
#    定位 makensis：choco 装到 Program Files (x86)\NSIS，常不在 PATH，需显式解析
#    /NOCD：禁止 makensis 自动切到 .nsi 所在目录，使 File/OutFile 相对路径基于仓库根目录
#    /INPUTCHARSET UTF8：nsi 为 UTF-8 无 BOM，中文系统 makensis 默认按 ACP(GBK) 读会报 Bad text encoding
#    （注意值与选项空格分隔，等号形式 /INPUTCHARSET=UTF8 不被识别，makensis 会打印用法退出 1）
$makensis = Get-Command makensis.exe -ErrorAction SilentlyContinue
if ($makensis) {
    $makensisPath = $makensis.Source
} else {
    $candidates = @(
        "${env:ProgramFiles(x86)}\NSIS\makensis.exe",
        "${env:ProgramFiles}\NSIS\makensis.exe",
        "${env:ProgramData}\chocolatey\bin\makensis.exe"
    )
    $makensisPath = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $makensisPath) { throw "未找到 makensis.exe，请先安装 NSIS" }
}
Push-Location $root
try {
    & $makensisPath /NOCD /INPUTCHARSET UTF8 "/DVERSION=$Version" "scripts\md-editor.nsi"
    if ($LASTEXITCODE -ne 0) { throw "makensis 失败 (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}

Write-Host "=== 完成 ==="
Get-ChildItem (Join-Path $root "dist") -Filter "md-editor-$Version-win64*" | ForEach-Object {
    Write-Host ("  {0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB))
}
