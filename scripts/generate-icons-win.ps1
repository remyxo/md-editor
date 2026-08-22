# 从 SVG 生成 Windows 应用图标 .ico（CI 内使用：windows-latest）
# 依赖：ImageMagick（choco install imagemagick，命令为 magick）
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$svg  = Join-Path $root "src\resources\icons\md-editor.svg"
$out  = Join-Path $root "src\resources\icons\md-editor.ico"

# 注意 1：-background none 必须放在 SVG 之前——它是 setting，只作用于其后读取的图像；
#          放后面时 SVG 已用默认白色画布栅格化，四角 alpha 已丢（装出来图标白边）
# 注意 2：先出到临时文件、内容有变化才替换。mtime 不动则 ninja 视 .rc 无需重编
#          （CMake 不解析 .rc 内部对 ico 的引用，靠 src/CMakeLists.txt 的
#          OBJECT_DEPENDS 显式挂依赖，而该依赖只看 mtime）
$tmp = "$out.tmp.ico"
& magick -background none $svg -define icon:auto-resize=256,128,64,48,32,16 $tmp
if ($LASTEXITCODE -ne 0) { Remove-Item $tmp -Force -ErrorAction SilentlyContinue; throw "magick 转换失败 (exit $LASTEXITCODE)" }

if ((Test-Path $out) -and ((Get-FileHash $tmp).Hash -eq (Get-FileHash $out).Hash)) {
    Remove-Item $tmp -Force
    Write-Host "图标未变化，跳过替换（保持 mtime）"
} else {
    Move-Item $tmp $out -Force
}

Write-Host "已生成: $out"
