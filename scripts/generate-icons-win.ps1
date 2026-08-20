# 从 SVG 生成 Windows 应用图标 .ico（CI 内使用：windows-latest）
# 依赖：ImageMagick（choco install imagemagick，命令为 magick）
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$svg  = Join-Path $root "src\resources\icons\md-editor.svg"
$out  = Join-Path $root "src\resources\icons\md-editor.ico"

& magick $svg -background none -define icon:auto-resize=256,128,64,48,32,16 $out
if ($LASTEXITCODE -ne 0) { throw "magick 转换失败 (exit $LASTEXITCODE)" }

Write-Host "已生成: $out"
