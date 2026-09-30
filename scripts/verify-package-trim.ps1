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
# dumpbin 直查 D:\BuildTools（provision 钉死的安装位）：sshd 非交互会话里
# vswhere 自动探测不可靠（build-windows-vm.ps1 同款结论，2026-09-29 实测踩坑）
$dumpbin = Get-ChildItem 'D:\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe' `
    -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty FullName
if (-not $dumpbin) {
    # 环境故障降级（同 pre-push 语义）：跳过依赖链检查，不阻断第 3 步 A/B
    Write-Warning "未找到 dumpbin（D:\BuildTools）——依赖链检查跳过"
} else {
    foreach ($dll in @("md-editor.exe", "Qt6WebEngineCore.dll", "Qt6Gui.dll")) {
        Write-Host "--- $dll 依赖 ---"
        & $dumpbin /DEPENDENTS (Join-Path $dist $dll) | Select-String "\.dll" | ForEach-Object { $_.ToString().Trim() }
    }
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
    # md-editor.exe 是 GUI 子系统程序：PowerShell & 直接调用不阻塞（$LASTEXITCODE
    # 恒空，export.json 未生成即读——2026-09-29 首跑误报根因）；Start-Process -Wait
    # 在 sshd 会话同样拿不到 ExitCode。经 cmd.exe console 宿主转调：阻塞等待且
    # 退出码可靠（实测 cmd /c 直跑 exit 0、三产物齐）
    $exe = Join-Path $dist "md-editor.exe"
    $ej = Join-Path $abOut "export.json"
    if (Test-Path $ej) { Remove-Item $ej }
    & cmd.exe /c "`"$exe`" --selftest export --file `"$bench`" --outdir `"$abOut`" --out `"$ej`""
    $code = $LASTEXITCODE
    if ($code -eq 0 -and (Test-Path $ej)) {
        $ok = (Get-Content $ej -Raw | ConvertFrom-Json).ok
        Write-Host ("A/B selftest export: ok={0}（true=三候选隔离后渲染导出仍通）" -f $ok)
    } else {
        Write-Host ("A/B selftest export: 失败（exit={0}, json={1}——三候选不可全砍或环境异常）" -f $code, (Test-Path $ej))
    }
} finally {
    foreach ($c in $candidates) {   # 恢复现场：任何结局（含启动失败）都移回，防 ~35MB DLL 滞留隔离区
        $q = Join-Path $quarantine $c
        if (Test-Path $q) { Move-Item $q (Join-Path $dist $c) -Force }
    }
}
