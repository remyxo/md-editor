# Windows 打包：windeployqt 捆绑 Qt → 便携 zip + NSIS 安装器
# 用法: .\scripts\package-windows.ps1 [-Version <版本号>] [-BuildDir <构建目录>] [-SkipZip]
#   -BuildDir：本地 VM 构建时 build 在 D:\build（见 docs/windows-build-vm.md）；
#              不传则用仓库内 build\（CI 行为，保持不变）
#   -SkipZip：跳过便携 zip 只产 NSIS 安装器（CI 用：Release 分发只保留 exe，
#              zip 制品占 139MB 是 Actions 500MB 存储配额超限主因）
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

# 3. windeployqt 部署 Qt DLL / WebEngine 资源 / VC 运行库（部署到 exe 旁）
& windeployqt --release --compiler-runtime --no-translations (Join-Path $dist "md-editor.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败 (exit $LASTEXITCODE)" }

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
