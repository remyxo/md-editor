# Windows 打包：windeployqt 捆绑 Qt → 便携 zip + NSIS 安装器
# 用法: .\scripts\package-windows.ps1 [-Version <版本号>]
param(
    [string]$Version = ""
)
$ErrorActionPreference = "Stop"

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build"
$exe   = Join-Path $build "src\md-editor.exe"
$dist  = Join-Path $root "dist\md-editor"

# 版本号：优先参数，否则从 CMakeLists.txt 读取
if (-not $Version) {
    $cmake = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
    if ($cmake -match 'project\(md-editor VERSION ([0-9.]+)\)') {
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

# 3. windeployqt 部署 Qt DLL / WebEngine 资源 / VC 运行库（部署到 exe 旁）
& windeployqt --release --compiler-runtime --no-translations (Join-Path $dist "md-editor.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt 失败 (exit $LASTEXITCODE)" }

# 4. 便携 zip
$zip = Join-Path $root "md-editor-$Version-win64.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $dist "*") -DestinationPath $zip

# 5. NSIS 安装器（在仓库根目录运行，nsi 内相对路径依赖此 CWD）
#    定位 makensis：choco 装到 Program Files (x86)\NSIS，常不在 PATH，需显式解析
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
    & $makensisPath /DVERSION=$Version "scripts\md-editor.nsi"
    if ($LASTEXITCODE -ne 0) { throw "makensis 失败 (exit $LASTEXITCODE)" }
} finally {
    Pop-Location
}

Write-Host "=== 完成 ==="
Get-ChildItem $root -Filter "md-editor-$Version-win64*" | ForEach-Object {
    Write-Host ("  {0}  {1:N1} MB" -f $_.Name, ($_.Length / 1MB))
}
