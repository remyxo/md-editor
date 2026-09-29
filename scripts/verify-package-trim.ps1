# 打包瘦身验证（VM 上运行）：①体积对账 ②dumpbin 静态依赖链 ③激进砍 A/B（selftest export）
# 路径按 VM 实际布局：仓库经 virtiofs 挂为卷标 md-editor-src、盘符 provision 钉死 W:
#   ——脚本/源码/dist 都在 W:\，D: 是 BuildTools/build/Qt 工具盘
$ErrorActionPreference = "Stop"
$dist = "W:\dist\md-editor"

Write-Host "=== 1. 体积对账 ==="
$total = (Get-ChildItem $dist -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("安装目录: {0:N1} MB（基线 287MB，安全砍预期 ~250MB）" -f $total)
foreach ($f in @("vc_redist.x64.exe", "resources\qtwebengine_devtools_resources.pak", "qmltooling")) {
    $p = Join-Path $dist $f
    Write-Host ("  {0}: {1}" -f $f, $(if (Test-Path $p) { "仍在（未砍掉！）" } else { "已移除" }))
}
$installer = Get-ChildItem "W:\dist" -Filter "md-editor-*-win64.exe" | Select-Object -First 1
if ($installer) { Write-Host ("安装器: {0:N1} MB（基线 113.9MB）" -f ($installer.Length / 1MB)) }

Write-Host "=== 2. dumpbin 静态依赖链（激进砍候选） ==="
$dumpbin = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -find "**\dumpbin.exe" | Select-Object -First 1
if (-not $dumpbin) { throw "未找到 dumpbin（vswhere）" }
foreach ($dll in @("md-editor.exe", "Qt6WebEngineCore.dll", "Qt6Gui.dll")) {
    Write-Host "--- $dll 依赖 ---"
    & $dumpbin /DEPENDENTS (Join-Path $dist $dll) | Select-String "\.dll" | ForEach-Object { $_.ToString().Trim() }
}

Write-Host "=== 3. 激进砍 A/B（隔离候选后跑 selftest export） ==="
$candidates = @("opengl32sw.dll", "dxcompiler.dll", "dxil.dll")
# 隔离区与 A/B 输出放 $env:TEMP：不放 W:\（virtiofs 会写回宿主仓库，污染 repo）、
# 也不放 D:\（BuildTools/build/Qt 工具盘）
$quarantine = Join-Path $env:TEMP "md-trim-quarantine"
$abOut = Join-Path $env:TEMP "md-trim-ab"
New-Item -ItemType Directory -Force -Path $quarantine | Out-Null
New-Item -ItemType Directory -Force -Path $abOut | Out-Null
foreach ($c in $candidates) {
    $p = Join-Path $dist $c
    if (Test-Path $p) { Move-Item $p (Join-Path $quarantine $c); Write-Host "已隔离 $c" }
}
$bench = "W:\src\resources\web\markdown-guide.md"
try {
    & (Join-Path $dist "md-editor.exe") --selftest export --file $bench `
        --outdir $abOut --out (Join-Path $abOut "export.json")
    $ok = (Get-Content (Join-Path $abOut "export.json") -Raw | ConvertFrom-Json).ok
    Write-Host ("A/B selftest export: ok={0}（true=三候选隔离后渲染导出仍通）" -f $ok)
} finally {
    foreach ($c in $candidates) {   # 恢复现场：任何结局（含启动失败）都移回，防 ~35MB DLL 滞留隔离区
        $q = Join-Path $quarantine $c
        if (Test-Path $q) { Move-Item $q (Join-Path $dist $c) -Force }
    }
}
