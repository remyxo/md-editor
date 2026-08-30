# 推送前跨平台编译预检（pre-push 钩子）实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把「只有 MSVC/Clang 才能发现的编译错误」拦截在 `git push` 之前——改动含 C++ 时自动跑容器 clang 语法检查 + win11 KVM 内 MSVC 编译，编译错误硬阻断、基础设施故障放行。

**Architecture:** 新增 `.githooks/pre-push` 钩子（stdin 读待推引用、算改动集、无 C++ 秒过）；检查一 `scripts/verify-clang.sh` 在 docker 容器（`mdeditor-clang:trixie`，同路径只读挂载仓库）内按 `build/compile_commands.json` 对 src/ 全部 .cpp 做 `-fsyntax-only`；检查二复用现有 Windows VM 构建链，加 `--compile-only` 参数跳过图标与打包。退出码协议：`0` 通过、`1` 编译错误（阻断）、`75` 环境不可用（放行）。

**Tech Stack:** bash（钩子与预检脚本）、python3（解析 compile_commands.json）、docker（debian:trixie + clang 19.1.7 + Qt6 dev 头文件镜像）、PowerShell（VM 内构建脚本）、libvirt/ssh（win11 KVM 触发链）。

## Global Constraints

- 所有文档与注释用中文，代码标识符用英文（CLAUDE.md 关键原则 1）
- 提交信息：`feat:` / `fix:` / `chore:` 前缀 + 中文描述（CLAUDE.md 关键原则 3）
- **不推版本号**：2026-08-24 当周修 bug 不推版本，收口统一推（用户记忆）——本计划不改 `CMakeLists.txt` 版本、不打 tag、不动 `CHANGELOG.md`；版本推进由用户在收口时决定
- **退出码协议全局一致**：`0`=通过，`1`=编译诊断存在（硬阻断推送），`75`=环境不可用（警告放行）；钩子把「非 0 非 75」一律按环境不可用放行（保守例外：MSVC 退出码透传，任何非 0 非 75 都阻断——见 Task 4 步骤说明）
- clang 检查**必须跑在容器内**：宿主机存在内核级页缓存/mmap 跨文件串页缺陷，宿主 clang 直读宿主 `/usr/include` 必崩（2026-08-30 取证结论，spec 风险节）
- 临时文件一律放 `/tmp/md-editor-claude-<日期>/` 专属目录，不用 /tmp 根目录简单文件名（用户记忆）
- 镜像 `mdeditor-clang:trixie` 基于 `docker.m.daocloud.io/library/debian:trixie`（宿主 docker hub 直连卡死，daocloud 镜像源可用）
- spec 原文：`docs/superpowers/specs/2026-08-30-prepush-cross-compile-verify-design.md`（已获用户确认）

---

### Task 1: clang 预检 Docker 镜像（可复现 Dockerfile）

**Files:**
- Create: `scripts/docker/Dockerfile.mdeditor-clang`

**Interfaces:**
- Consumes: 无（首个任务）
- Produces: 本地 docker 镜像 `mdeditor-clang:trixie`，内含 clang 19.1.7 + libstdc++-14-dev + Qt6 dev 头文件（QtCore/QtGui/QtWidgets/QtWebEngine/QtWebChannel 等）。Task 2 的 `verify-clang.sh` 按此镜像名引用；镜像缺失时 Task 2 以 exit 75 放行并打印本任务的构建命令。

**背景（执行者需知）:** 本地已有一个手工 `docker commit` 的同名镜像（仅 clang + libstdc++，**无 Qt 头文件**）。本任务用 Dockerfile 可复现地重建并覆盖同名 tag，补齐 Qt6 dev 头文件。容器内 Qt6 头文件位于 `/usr/include/x86_64-linux-gnu/qt6/<Module>/`，与宿主 CMake 生成的 `-I` 路径一致（compile_commands.json 中 `-I/usr/include/x86_64-linux-gnu/qt6/QtCore` 之类），因此**同路径挂载后无需任何头文件路径转换**。

- [ ] **Step 1: 写 Dockerfile**

创建 `scripts/docker/Dockerfile.mdeditor-clang`：

```dockerfile
# clang 语法预检镜像（git pre-push 钩子用，见 scripts/verify-clang.sh）
# 构建：docker build -t mdeditor-clang:trixie -f scripts/docker/Dockerfile.mdeditor-clang scripts/docker/
# 基础镜像走 daocloud 镜像源（宿主直连 docker hub 卡死）
# Qt dev 包与 CLAUDE.md「环境要求」一致，保证 src/ 全部 .cpp 的 #include 可解析
FROM docker.m.daocloud.io/library/debian:trixie

RUN apt-get update && apt-get install -y --no-install-recommends \
        clang \
        libstdc++-14-dev \
        qt6-base-dev \
        qt6-webchannel-dev \
        qt6-webengine-dev \
    && rm -rf /var/lib/apt/lists/*

# 自检：clang 与 Qt 头文件就位（构建期失败即镜像不可用）
RUN clang++ --version && test -d /usr/include/x86_64-linux-gnu/qt6/QtCore
```

- [ ] **Step 2: 构建镜像（覆盖旧的同名手工镜像）**

```bash
cd /home/daijd/claude-workspace/md-editor
docker build -t mdeditor-clang:trixie -f scripts/docker/Dockerfile.mdeditor-clang scripts/docker/
```

预期：构建成功（基础镜像本地已有缓存层；apt 下载 Qt 包约 1GB，需几分钟）。若 daocloud 拉取失败，重试一次再排查网络。

- [ ] **Step 3: 验证镜像内 clang + Qt 头文件可用（最小翻译单元）**

```bash
mkdir -p /tmp/md-editor-claude-$(date +%Y%m%d)
cat > /tmp/md-editor-claude-$(date +%Y%m%d)/probe-qt.cpp <<'EOF'
// 镜像自检样本：覆盖预检实际用到的 Qt 模块头文件
#include <QString>
#include <QWidget>
#include <QWebEngineView>
#include <QWebChannel>
int main() { QString s; return s.isEmpty() ? 0 : 1; }
EOF
docker run --rm --network=none \
  -v "/tmp/md-editor-claude-$(date +%Y%m%d):/probe:ro" \
  mdeditor-clang:trixie \
  clang++ -fsyntax-only -std=gnu++17 \
    -I/usr/include/x86_64-linux-gnu/qt6/QtCore \
    -I/usr/include/x86_64-linux-gnu/qt6/QtGui \
    -I/usr/include/x86_64-linux-gnu/qt6/QtWidgets \
    -I/usr/include/x86_64-linux-gnu/qt6/QtWebEngineWidgets \
    -I/usr/include/x86_64-linux-gnu/qt6/QtWebChannel \
    /probe/probe-qt.cpp
echo "exit=$?"
```

预期：`exit=0`，无任何诊断输出。若报头文件找不到，检查 Step 1 包列表是否漏装对应模块的 dev 包。

- [ ] **Step 4: 事故样本回归（镜像必须能拦住 2026-08-30 两类事故错误）**

```bash
cat > /tmp/md-editor-claude-$(date +%Y%m%d)/probe-bad.cpp <<'EOF'
// 2026-08-30 事故的两类写法复现（无 Qt 依赖）：lambda 捕获 static + 花括号收窄
static const int Flag = 1;
auto bad1 = [&Flag] { return Flag; };   // clang: cannot be captured ... no automatic storage duration
long big = 5000000000L;
struct Span { int pos; int len; };
Span bad2{big, 7};                       // clang: non-constant-expression cannot be narrowed
int main() { return 0; }
EOF
docker run --rm --network=none \
  -v "/tmp/md-editor-claude-$(date +%Y%m%d):/probe:ro" \
  mdeditor-clang:trixie \
  clang++ -fsyntax-only -std=gnu++17 /probe/probe-bad.cpp 2>&1
echo "exit=$?"   # 注意：不要用管道接 head，会吞退出码；直接看输出
```

预期：`exit=1`，输出含 `cannot be captured because it does not have automatic storage duration` 与 `cannot be narrowed`。两项都出现才算通过。

- [ ] **Step 5: Commit**

```bash
git add scripts/docker/Dockerfile.mdeditor-clang
git commit -m "feat: clang 预检镜像 Dockerfile（debian:trixie + clang 19 + Qt6 头文件）"
```

---

### Task 2: scripts/verify-clang.sh（容器内 clang 语法预检）

**Files:**
- Create: `scripts/verify-clang.sh`（需 `chmod +x`）

**Interfaces:**
- Consumes: Task 1 的镜像 `mdeditor-clang:trixie`；宿主 `build/compile_commands.json`（脚本自行幂等重配置生成）。
- Produces: 命令 `./scripts/verify-clang.sh`，退出码协议 `0`/`1`/`75`（Task 4 钩子按此分流）。Task 4 以路径 `"$REPO/scripts/verify-clang.sh"` 调用。

**关键设计（执行者需知）:**
- **同路径挂载免去路径转换**：`docker run -v "$REPO:$REPO:ro"` 把仓库挂到容器内相同绝对路径，compile_commands.json 里的绝对路径（含 `-I .../build/src/md-editor_autogen/include` 等生成头目录，build/ 在仓库内一并可见）原样可用。
- command 字段是 shell 命令行字符串（含 `-DMD_EDITOR_VERSION=\"0.13.0\"` 转义），python `shlex.split` 解析为 argv、`shlex.quote` 重打包，容器内经 `eval "$0"` 按 shell 语义还原——语义无损。
- 并行：容器内 `xargs -d '\n' -P "$(nproc)" -n1 sh -c 'eval "$0"'`，每条命令 stderr 落独立日志文件，失败只记索引，宿主端串行汇总输出（避免诊断交错）。

- [ ] **Step 1: 先跑失败测试（脚本不存在，钩子链路的调用方应得 127 → 交给 Task 4 兜底；本任务直接验证调用入口报错）**

```bash
cd /home/daijd/claude-workspace/md-editor
./scripts/verify-clang.sh; echo "exit=$?"
```

预期：`No such file or directory`，`exit=127`。（记录在案，Task 4 对非 0/非 75 放行只依赖 exit 75 语义，脚本本体在 Step 3 落地。）

- [ ] **Step 2: 写完整实现**

创建 `scripts/verify-clang.sh` 并 `chmod +x`：

```bash
#!/usr/bin/env bash
# 推送前 clang 语法预检（容器版）：对 src/ 全部 .cpp 逐个 clang++ -fsyntax-only
# 为何必须容器：宿主机内核存在页缓存/mmap 跨文件串页缺陷，宿主 clang 直读宿主
# /usr/include 必崩；容器读自身 overlayfs 文件页不受影响（spec 风险节，2026-08-30 取证）
# 编译参数取自 build/compile_commands.json（GCC 配置生成，clang 兼容其旗标）
# 退出码协议：0=全部通过；1=存在编译诊断（硬阻断推送）；75=环境不可用（调用方警告放行）
set -u
cd "$(dirname "$0")/.." || exit 75
REPO=$(pwd)
IMAGE="mdeditor-clang:trixie"
DOCKERFILE="scripts/docker/Dockerfile.mdeditor-clang"

say()  { printf '\033[36m[verify-clang] %s\033[0m\n' "$*"; }
warn() { printf '\033[33m[verify-clang] %s\033[0m\n' "$*" >&2; }
die_env() { warn "$*"; exit 75; }

# ---- 1. 环境检查：docker 与镜像 ----
command -v docker >/dev/null 2>&1 || die_env "docker 未安装——预检跳过"
docker image inspect "$IMAGE" >/dev/null 2>&1 \
    || die_env "镜像 $IMAGE 不存在——预检跳过。构建：docker build -t $IMAGE -f $DOCKERFILE $DOCKERFILE 的上级目录 scripts/docker/"

# ---- 2. 幂等重配置，确保 compile_commands.json 就绪（复用现有 build/ 缓存，首次约 20s）----
say "刷新 compile_commands.json"
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null 2>&1 \
    || die_env "cmake 配置失败（build/ 缓存可能损坏，可删 build/ 重配后重试）"
[ -f build/compile_commands.json ] || die_env "build/compile_commands.json 未生成"

# ---- 3. 生成容器内执行脚本 ----
# 同路径只读挂载仓库（含 build/ 的 autogen 生成头），compile_commands 绝对路径原样可用
WORK=$(mktemp -d /tmp/md-editor-verify-clang-XXXXXX)
mkdir -p "$WORK/logs"
trap 'rm -rf "$WORK"' EXIT

python3 - "$REPO" "$WORK" <<'PYEOF' || die_env "compile_commands.json 解析失败"
import json, os, shlex, sys
repo, work = sys.argv[1], sys.argv[2]
db = json.load(open(os.path.join(repo, "build/compile_commands.json")))
cmds, files = [], []
for e in db:
    f = e["file"]
    if not (f.startswith(repo + "/src/") and f.endswith((".cpp", ".c"))):
        continue  # 只检查 src/，不碰 tests/（CI 侧 Windows/macOS 均 BUILD_TESTS=OFF）
    if "command" not in e:
        print(f"非 command 形式条目，不支持: {f}", file=sys.stderr); sys.exit(1)
    argv = shlex.split(e["command"])
    out, skip = ["clang++", "-fsyntax-only"], False
    for a in argv[1:]:
        if skip:
            skip = False; continue
        if a == "-o":
            skip = True; continue   # 丢弃 -o 及其目标参数
        if a == "-c":
            continue                # -c 与 -fsyntax-only 语义冲突，丢弃
        out.append(a)
    idx = len(cmds)
    log = os.path.join(work, "logs", f"{idx}.txt")
    quoted = " ".join(shlex.quote(x) for x in out)
    cmds.append(f'( cd {shlex.quote(e["directory"])} && {quoted} ) 2>{shlex.quote(log)}'
                f' || echo {idx} >> {shlex.quote(os.path.join(work, "failed.idx"))}')
    files.append(f"{idx} {f}")
if not cmds:
    print("compile_commands.json 中没有 src/ 下的 .cpp 条目", file=sys.stderr); sys.exit(1)
open(os.path.join(work, "commands.txt"), "w").write("\n".join(cmds) + "\n")
open(os.path.join(work, "files.txt"), "w").write("\n".join(files) + "\n")
open(os.path.join(work, "run.sh"), "w").write(
    "#!/bin/bash\n"
    "xargs -d '\\n' -P \"$(nproc)\" -n1 sh -c 'eval \"$0\"' < " + shlex.quote(os.path.join(work, "commands.txt")) + "\n"
    "[ ! -f " + shlex.quote(os.path.join(work, "failed.idx")) + " ]\n")
PYEOF

# ---- 4. 容器内并行执行（仓库只读；WORK 可写以回传失败索引）----
say "容器内 clang 语法检查（src/ 下 .cpp，并行）"
docker run --rm --read-only --network=none \
    -v "$REPO:$REPO:ro" -v "$WORK:$WORK" \
    "$IMAGE" bash "$WORK/run.sh"
rc=$?
[ "$rc" -eq 0 ] && { say "全部通过"; exit 0; }
[ "$rc" -eq 1 ] || die_env "容器执行异常（docker rc=$rc），预检跳过"

# ---- 5. 汇总失败翻译单元的诊断（每文件前 20 行，串行输出避免交错）----
echo "-------- clang 诊断（失败的翻译单元）--------" >&2
while read -r idx; do
    f=$(awk -v i="$idx" '$1 == i {print $2}' "$WORK/files.txt")
    echo "✗ $f" >&2
    sed -n '1,20p' "$WORK/logs/$idx.txt" >&2
done < "$WORK/failed.idx"
exit 1
```

- [ ] **Step 3: 干净树全量验证**

```bash
time ./scripts/verify-clang.sh; echo "exit=$?"
```

预期：`exit=0`，输出 `[verify-clang] 全部通过`，耗时 ~30–60s。若个别文件报 unknown argument 之类 GCC 专用旗标不兼容，在 Step 2 python 循环里对 `out.append(a)` 前加对应旗标的显式跳过（白名单），重跑直至全绿。

- [ ] **Step 4: 事故回归——真实坏代码必须被拦下**

```bash
cp src/core/MarkdownHandler.cpp /tmp/md-editor-claude-$(date +%Y%m%d)/MarkdownHandler.cpp.bak
git show 70ff8d0~1:src/core/MarkdownHandler.cpp > src/core/MarkdownHandler.cpp
./scripts/verify-clang.sh; echo "exit=$?"
cp /tmp/md-editor-claude-$(date +%Y%m%d)/MarkdownHandler.cpp.bak src/core/MarkdownHandler.cpp
git status --short   # 必须干净，确认还原
```

预期：`exit=1`，诊断含 `cannot be captured because it does not have automatic storage duration` 或 `cannot be narrowed`（该坏版本含 2026-08-30 全部 14 处错误）；还原后 `git status --short` 无输出。

- [ ] **Step 5: 环境缺失路径——镜像不在时必须 exit 75 放行**

```bash
docker tag mdeditor-clang:trixie mdeditor-clang:trixie-bak
docker rmi mdeditor-clang:trixie
./scripts/verify-clang.sh; echo "exit=$?"
docker tag mdeditor-clang:trixie-bak mdeditor-clang:trixie
docker rmi mdeditor-clang:trixie-bak
docker image inspect mdeditor-clang:trixie >/dev/null && echo "镜像已还原"
```

预期：`exit=75`，stderr 含「镜像 mdeditor-clang:trixie 不存在」；最后输出「镜像已还原」。

- [ ] **Step 6: Commit**

```bash
git add scripts/verify-clang.sh
git commit -m "feat: scripts/verify-clang.sh 容器内 clang 语法预检"
```

---

### Task 3: Windows 构建链 -CompileOnly 预检模式（三处脚本 + VM 内 launch 手工更新）

**Files:**
- Modify: `scripts/build-windows-vm.ps1:3-5`（param 加开关）、`:36`（magick 检查放宽）、`:38-40`（图标条件跳过）、`:65-67`（打包条件跳过）
- Modify: `scripts/build-windows-local.sh:5-12`（用法/参数解析）、`:22-28`（infra 错误预检模式下 75）、`:33-45`（ssh 超时同上）、`:48-66`（透传参数、跳过产物列举）
- Modify: `scripts/provision-windows-vm.ps1:260-275`（launch 生成段支持参数透传）
- Modify: `docs/windows-build-vm.md`（「日常用法」节加 --compile-only 说明）
- VM 内一次性手工更新：`C:\md-editor-build-launch.ps1`（不进仓库，与 provision 生成段保持一致）

**Interfaces:**
- Consumes: 现有 VM 构建链（win11 KVM、`builder@192.168.122.10`、virtiofs 共享卷标 `md-editor-src`）。
- Produces: 命令 `./scripts/build-windows-local.sh --compile-only`，退出码协议：MSVC 退出码透传（非 0 → Task 4 阻断）；`75`=预检模式下环境故障（virsh 连不上 / VM 启动失败 / ssh 超时）。Task 4 以此调用。`build-windows-vm.ps1` 新增 `[switch]$CompileOnly`（launch 脚本经 `. ...build-windows-vm.ps1 -CompileOnly:$CompileOnly` dot-source 传参）。

- [ ] **Step 1: build-windows-vm.ps1 加 -CompileOnly 开关**

第 3–5 行 param 块改为（含 §4/§6/magick 三处联动，完整改动如下）：

```powershell
param(
    [string]$SrcRoot = "",     # 仓库共享根目录；为空时按卷标 md-editor-src 自动定位
    [switch]$CompileOnly       # 预检模式：只配置+编译，跳过图标生成与打包（pre-push 钩子用）
)
```

第 36 行 magick 检查改为（图标才需要 magick，预检模式缺失不挡）：

```powershell
if ($magick) { $env:Path = "$($magick.Directory);$env:Path" }
elseif (-not $CompileOnly) { Write-Error "未找到 magick.exe（provision 步骤 5）"; exit 1 }
```

第 4 步图标（原 38–40 行）改为：

```powershell
# ---- 4. 应用图标（内容有变才替换、mtime 不动则 ninja 无需重编 .rc，见 generate-icons-win.ps1；
#      勿在此预删旧 ico——会把 hash 跳过分支打掉，每次都重链 exe；-CompileOnly 跳过）----
if (-not $CompileOnly) {
    & (Join-Path $SrcRoot 'scripts\generate-icons-win.ps1')
}
```

第 6 步打包（原 65–67 行）改为：

```powershell
    # ---- 6. 打包：windeployqt → 便携 zip + NSIS 安装器（产物落共享卷 dist\ = 宿主仓库 dist/）----
    if (-not $CompileOnly) {
        Write-Host "==> 打包"
        & (Join-Path $SrcRoot 'scripts\package-windows.ps1') -BuildDir 'D:\build'
    } else {
        Write-Host "==> 编译通过（-CompileOnly，跳过打包）"
    }
```

- [ ] **Step 2: provision-windows-vm.ps1 的 launch 生成段支持参数透传**

第 261–273 行 `$launch = @'...'@` 的 here-string 整体替换为：

```powershell
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
```

- [ ] **Step 3: VM 内一次性手工更新 C:\md-editor-build-launch.ps1（与 Step 2 生成内容一致）**

不重跑整个 provision（避免动 VM 其他配置），直接下发新 launch：

```bash
mkdir -p /tmp/md-editor-claude-$(date +%Y%m%d)
cat > /tmp/md-editor-claude-$(date +%Y%m%d)/launch-new.ps1 <<'EOF'
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
EOF
scp /tmp/md-editor-claude-$(date +%Y%m%d)/launch-new.ps1 \
    builder@192.168.122.10:'C:/Windows/Temp/launch-new.ps1'
ssh builder@192.168.122.10 "powershell -NoProfile -Command \"Copy-Item C:\\Windows\\Temp\\launch-new.ps1 C:\\md-editor-build-launch.ps1 -Force\""
```

预期：scp 与 ssh 均无报错。（VM 需在运行；若 ssh 不通先 `virsh -c qemu:///system start win11` 再等约 40s。）

- [ ] **Step 4: build-windows-local.sh 加 --compile-only 参数与 75 语义**

用法注释（5–8 行）补一行，并在 `LAUNCH=` 行（13 行）后加参数解析；改动汇总如下（新增/修改的完整片段）：

```bash
# 用法: ./scripts/build-windows-local.sh [--compile-only]
#   --compile-only  预检模式：只编译不打包；virsh/ssh 等环境故障 exit 75 放行（pre-push 钩子用）
```

参数解析与预检退出函数（插在 `LAUNCH='C:\md-editor-build-launch.ps1'` 之后）：

```bash
COMPILE_ONLY=0
[ "${1:-}" = "--compile-only" ] && COMPILE_ONLY=1

# 预检模式下环境故障放行（75 语义，钩子警告继续）；普通模式保持硬错
die_precheck() {
    if [ "$COMPILE_ONLY" -eq 1 ]; then
        printf '\033[33m[预检] %s——环境不可用，放行\n\033[0m' "$*" >&2
        exit 75
    fi
    die "$*"
}
```

§1 三处 infra 错误改走 `die_precheck`：`virsh 连不上`、`VM 启动失败` 两处 `die` 改 `die_precheck`；§2 ssh 超时块的 `exit 1` 改 `die_precheck "ssh 到 $SSH_TARGET 超时"`（heredoc 排查提示保留在 die_precheck 之前打印）。

§3 触发命令（50 行）改为：

```bash
say "触发远程构建（首次冷构建约 10-15 分钟）"
EXTRA_ARGS=""
[ "$COMPILE_ONLY" -eq 1 ] && EXTRA_ARGS=" -CompileOnly"
ssh -o BatchMode=yes "$SSH_TARGET" "powershell -NoProfile -ExecutionPolicy Bypass -File $LAUNCH$EXTRA_ARGS"
rc=$?
```

§4 产物列举前加分支（54 行 `if [ "$rc" -eq 0 ]; then` 之后）：

```bash
if [ "$rc" -eq 0 ]; then
    if [ "$COMPILE_ONLY" -eq 1 ]; then
        say "编译通过（--compile-only，跳过打包与产物列举）"
        exit 0
    fi
    say "构建完成，产物（dist/）"
    # ……原有列举逻辑不动……
```

- [ ] **Step 5: docs/windows-build-vm.md「日常用法」节（58 行起）追加说明**

在该节 `./scripts/build-windows-local.sh` 用法说明之后追加：

```markdown
- 预检模式：`./scripts/build-windows-local.sh --compile-only` 只配置+编译（跳过图标与打包），
  pre-push 钩子自动调用（见 `.githooks/pre-push` 与 spec
  `docs/superpowers/specs/2026-08-30-prepush-cross-compile-verify-design.md`）；
  环境故障（virsh/ssh）exit 75 放行，MSVC 编译失败退出码透传
```

- [ ] **Step 6: 真 VM 温启动验证——--compile-only 编译通过**

```bash
time ./scripts/build-windows-local.sh --compile-only; echo "exit=$?"
```

预期：`exit=0`；输出含「编译通过（-CompileOnly，跳过打包）」与「--compile-only，跳过打包与产物列举」；**不出现**「打包」「产物（dist/）」字样；VM 已开机时总耗时 <2 分钟（增量 <1 分钟 + ssh 开销）。

- [ ] **Step 7: 真 VM 坏代码回归——MSVC 错误必须透传阻断**

```bash
cp src/app/main.cpp /tmp/md-editor-claude-$(date +%Y%m%d)/main.cpp.bak
printf '\nint broken_precheck( {\n' >> src/app/main.cpp
./scripts/build-windows-local.sh --compile-only; echo "exit=$?"
cp /tmp/md-editor-claude-$(date +%Y%m%d)/main.cpp.bak src/app/main.cpp
git status --short   # 必须干净
./scripts/build-windows-local.sh --compile-only; echo "exit=$?"   # 还原后再跑，应 exit=0
```

预期：坏代码轮 `exit` 非 0 非 75（MSVC 退出码透传），日志含 MSVC 编译错误（C2xxx）；还原后两查均过、`git status --short` 无输出。

- [ ] **Step 8: Commit**

```bash
git add scripts/build-windows-vm.ps1 scripts/build-windows-local.sh scripts/provision-windows-vm.ps1 docs/windows-build-vm.md
git commit -m "feat: Windows 构建链支持 -CompileOnly 预检模式（跳过图标与打包，环境故障 75 放行）"
```

---

### Task 4: .githooks/pre-push 钩子 + 启用

**Files:**
- Create: `.githooks/pre-push`（需 `chmod +x`）
- Modify: `CLAUDE.md`（「构建命令」节后加「推送前预检」小节）

**Interfaces:**
- Consumes: Task 2 `scripts/verify-clang.sh`（0/1/75）；Task 3 `scripts/build-windows-local.sh --compile-only`（0/透传/75）。
- Produces: 本机 git 钩子生效（`git config core.hooksPath .githooks`，一次性、本机生效）；环境变量逃生门 `MD_EDITOR_SKIP_PRECHECK=1`、自测开关 `MD_EDITOR_PRECHECK_DRY_RUN=1`（只打印触发决策不执行检查）。

**分流语义（执行者需知）:** 钩子 stdin 每行 `<local_ref> <local_sha> <remote_ref> <remote_sha>`。annotated tag 的 local_sha 是 tag 对象，必须 `^{commit}` dereference；remote_sha 全零（新分支/新 tag）走与 `origin/master` 的 merge-base，无基准（如首推无 origin/master）则该引用不计入；local_sha 全零（删除引用）跳过。多引用（master + tag 同推）改动集取并集。检查顺序：clang（快）失败即止，不浪费 VM 启动。

- [ ] **Step 1: 写钩子**

创建 `.githooks/pre-push` 并 `chmod +x`：

```bash
#!/usr/bin/env bash
# git pre-push 钩子：改动含 C++ 时做跨平台编译预检（容器 clang + win11 KVM MSVC）
# 一次性启用：git config core.hooksPath .githooks（本机生效，换机需重跑）
# 逃生门：git push --no-verify 或 MD_EDITOR_SKIP_PRECHECK=1
# 自测：MD_EDITOR_PRECHECK_DRY_RUN=1 只打印触发决策，不执行检查
set -u
REPO=$(git rev-parse --show-toplevel) || exit 0
ZERO=0000000000000000000000000000000000000000
PATTERN='(\.(cpp|c|h|hpp|qrc|cmake)$|(^|/)CMakeLists\.txt$)'

say()  { printf '\033[36m[pre-push] %s\033[0m\n' "$*"; }
warn() { printf '\033[33m[pre-push] %s\033[0m\n' "$*" >&2; }

[ "${MD_EDITOR_SKIP_PRECHECK:-0}" = "1" ] && { warn "MD_EDITOR_SKIP_PRECHECK=1，跳过预检"; exit 0; }

# ---- 1. 汇总所有待推引用的改动集（多引用取并集）----
changed=$(
    while read -r local_ref local_sha remote_ref remote_sha; do
        [ "$local_sha" = "$ZERO" ] && continue   # 删除引用，无新内容
        commit=$(git rev-parse --verify -q "${local_sha}^{commit}" 2>/dev/null) || {
            warn "无法把 $local_ref 解引用到 commit（$local_sha），该引用不计入改动集"
            continue
        }
        if [ "$remote_sha" = "$ZERO" ]; then     # 新分支/新 tag：与 origin/master 比
            base=$(git merge-base "$commit" origin/master 2>/dev/null) || {
                warn "$local_ref 与 origin/master 无共同祖先，不计入改动集"
                continue
            }
        else
            base=$remote_sha
        fi
        git diff --name-only "$base" "$commit"
    done
)

# ---- 2. 改动集不含 C++ 相关 → 秒过 ----
if ! printf '%s\n' "$changed" | grep -Eq "$PATTERN"; then
    say "改动不含 C++ 源/构建文件，跳过预检"
    exit 0
fi
count=$(printf '%s\n' "$changed" | grep -Ec "$PATTERN")
say "改动含 C++ 相关文件（$count 个），执行推送前预检"

if [ "${MD_EDITOR_PRECHECK_DRY_RUN:-0}" = "1" ]; then
    say "[dry-run] 将执行：verify-clang.sh → build-windows-local.sh --compile-only"
    exit 0
fi

# ---- 3. 检查一：容器 clang 语法（快，失败即止不浪费 VM 启动）----
say "检查 1/2：clang 语法检查（容器）"
"$REPO/scripts/verify-clang.sh"
rc=$?
if [ "$rc" -eq 1 ]; then
    warn "clang 语法检查未通过——推送已阻断（git push --no-verify 可强推）"
    exit 1
elif [ "$rc" -ne 0 ]; then
    warn "clang 检查环境不可用（exit $rc），跳过该环节"
fi

# ---- 4. 检查二：win11 KVM MSVC 编译 ----
say "检查 2/2：Windows MSVC 编译（VM）"
"$REPO/scripts/build-windows-local.sh" --compile-only
rc=$?
if [ "$rc" -eq 75 ]; then
    warn "Windows VM 环境不可用，跳过该环节"
elif [ "$rc" -ne 0 ]; then
    warn "MSVC 编译失败（exit $rc）——推送已阻断（git push --no-verify 可强推）"
    exit 1
fi

say "预检通过"
exit 0
```

- [ ] **Step 2: dry-run 分流测试矩阵（五场景，不真跑检查）**

在仓库根执行（master 当前已与 origin/master 同步为前提）：

```bash
# 场景 A：新分支 doc-only 提交 → 应「跳过预检」
git checkout -b prepush-test-doc origin/master
echo test > docs/prepush-test.tmp.md && git add docs/prepush-test.tmp.md && git commit -m "chore: prepush 钩子测试临时提交（doc-only）"
echo "refs/heads/prepush-test-doc $(git rev-parse HEAD) refs/heads/prepush-test-doc 0000000000000000000000000000000000000000" \
  | MD_EDITOR_PRECHECK_DRY_RUN=1 .githooks/pre-push; echo "A exit=$?"
# 预期：输出「跳过预检」，A exit=0

# 场景 B：新分支 C++ 提交 → 应「执行推送前预检」（注意：入口源文件是 src/main.cpp，无 src/app/main.cpp）
echo "// prepush 测试" >> src/main.cpp && git add src/main.cpp && git commit -m "chore: prepush 钩子测试临时提交（cpp）"
echo "refs/heads/prepush-test-doc $(git rev-parse HEAD) refs/heads/prepush-test-doc 0000000000000000000000000000000000000000" \
  | MD_EDITOR_PRECHECK_DRY_RUN=1 .githooks/pre-push; echo "B exit=$?"
# 预期：输出「执行推送前预检」与「[dry-run] 将执行」，B exit=0

# 场景 C：annotated tag 指向 B 的 commit → dereference 后同样触发
git tag -a prepush-test-tag -m "prepush 测试 tag" HEAD
echo "refs/tags/prepush-test-tag $(git rev-parse prepush-test-tag) refs/tags/prepush-test-tag 0000000000000000000000000000000000000000" \
  | MD_EDITOR_PRECHECK_DRY_RUN=1 .githooks/pre-push; echo "C exit=$?"
# 预期：同 B（若不 deref，tag 对象 sha 进 diff 会异常——输出触发即证明 deref 生效）

# 场景 D：删除引用（local_sha 全零）→ 跳过
echo "refs/heads/prepush-test-doc 0000000000000000000000000000000000000000 refs/heads/prepush-test-doc $(git rev-parse HEAD)" \
  | MD_EDITOR_PRECHECK_DRY_RUN=1 .githooks/pre-push; echo "D exit=$?"
# 预期：「跳过预检」，D exit=0

# 场景 E：SKIP 逃生门
echo "refs/heads/prepush-test-doc $(git rev-parse HEAD) refs/heads/prepush-test-doc 0000000000000000000000000000000000000000" \
  | MD_EDITOR_SKIP_PRECHECK=1 .githooks/pre-push; echo "E exit=$?"
# 预期：「MD_EDITOR_SKIP_PRECHECK=1，跳过预检」，E exit=0

# 清理测试痕迹
git checkout master
git branch -D prepush-test-doc
git tag -d prepush-test-tag
git status --short   # 必须干净
```

- [ ] **Step 3: 启用钩子 + CLAUDE.md 文档**

```bash
git config core.hooksPath .githooks
```

CLAUDE.md「构建命令」节之后追加小节：

```markdown
## 推送前预检（pre-push 钩子）

- 一次性启用（本机生效，换机需重跑）：`git config core.hooksPath .githooks`
- 改动含 C++（src/构建文件）时 push 前自动：容器 clang 语法检查（`scripts/verify-clang.sh`，镜像 `mdeditor-clang:trixie`）+ win11 VM MSVC 编译（`scripts/build-windows-local.sh --compile-only`）；无 C++ 改动秒过
- 编译错误硬阻断；环境故障（docker/镜像/VM/ssh）警告放行
- 逃生门：`git push --no-verify` 或 `MD_EDITOR_SKIP_PRECHECK=1`
- clang 镜像构建：`docker build -t mdeditor-clang:trixie -f scripts/docker/Dockerfile.mdeditor-clang scripts/docker/`
```

- [ ] **Step 4: --no-verify 原生逃生验证（不真推 origin，推临时 bare）**

```bash
mkdir -p /tmp/md-editor-claude-$(date +%Y%m%d)/prepush-e2e
git init --bare /tmp/md-editor-claude-$(date +%Y%m%d)/prepush-e2e/remote.git
git remote add prepush-e2e /tmp/md-editor-claude-$(date +%Y%m%d)/prepush-e2e/remote.git
echo "// prepush no-verify 测试" >> src/main.cpp
git add src/main.cpp && git commit -m "chore: prepush --no-verify 验证临时提交"
git push --no-verify prepush-e2e master; echo "exit=$?"
git push prepush-e2e master --force-with-lease=master: 2>&1 | head -3   # 不带 --no-verify 应触发预检输出后进入真实检查（Ctrl+C 可中断——此步只为观察触发，看到「执行推送前预检」+「检查 1/2」即算通过，随后中断）
git reset --hard HEAD~1   # 丢弃测试提交
git remote remove prepush-e2e
git status --short && git log --oneline -1   # 确认回到原 HEAD
```

预期：`--no-verify` 推送成功 exit=0；不带参数的推送先打出 `[pre-push] 执行推送前预检` 与 `检查 1/2`（随后人工 Ctrl+C 中断即可，不要求跑完）。

- [ ] **Step 5: Commit**

```bash
git add .githooks/pre-push CLAUDE.md
git commit -m "feat: .githooks/pre-push 跨平台编译预检钩子（clang + MSVC，退出码 0/1/75 分流）"
```

---

### Task 5: 端到端验证（真实 push 全链路，含 VM 冷启动）

**Files:**
- 无新增/修改文件（纯验证；若发现缺陷，回对应 Task 的文件修复并 `fix:` 提交）

**Interfaces:**
- Consumes: Task 1–4 全部产出。
- Produces: 验证记录（耗时与结论报告给用户）；spec「测试策略」四条全部闭环。

- [ ] **Step 1: VM 冷启动前置**

```bash
virsh -c qemu:///system shutdown win11
# 轮询等关机（最多 60s）
until [ "$(virsh -c qemu:///system domstate win11)" = "shut off" ]; do sleep 5; done
```

- [ ] **Step 2: 造真实 C++ 提交，走完整 push（钩子全链路：clang + VM 冷启动 + MSVC）**

```bash
mkdir -p /tmp/md-editor-claude-$(date +%Y%m%d)/prepush-e2e
git init --bare /tmp/md-editor-claude-$(date +%Y%m%d)/prepush-e2e/remote.git
git remote add prepush-e2e /tmp/md-editor-claude-$(date +%Y%m%d)/prepush-e2e/remote.git
echo "// prepush 端到端验证" >> src/main.cpp
git add src/main.cpp && git commit -m "chore: prepush 端到端验证临时提交"
time git push prepush-e2e master; echo "exit=$?"
```

预期：钩子依次输出「执行推送前预检」→「检查 1/2」（clang 全绿）→「检查 2/2」（VM 自动开机 40s + ssh 等待 + MSVC 增量编译）→「预检通过」→ push 成功 `exit=0`；冷启动总耗时 ~2–4 分钟（记录实际值写进报告）。

- [ ] **Step 3: 清理与还原**

```bash
git reset --hard HEAD~1
git remote remove prepush-e2e
rm -rf /tmp/md-editor-claude-$(date +%Y%m%d)/prepush-e2e
git status --short && git log --oneline -1   # 确认回到原 HEAD、工作区干净
```

- [ ] **Step 4: 汇总报告**

向用户报告：① spec 测试策略四条（钩子分流 / 事故回归 / -CompileOnly 实测 / 端到端）各自的结果与耗时；② 遗留问题（如有）；③ 版本推进建议留待用户收口决定（本计划未动版本号）。

---

## 自审记录

1. **Spec 覆盖**：组件 1 钩子（Task 4）、组件 2 容器 clang（Task 1 镜像 + Task 2 脚本）、组件 3 Windows 侧三处改动（Task 3）、组件 4 启用与文档（Task 4 的 CLAUDE.md + Task 3 的 windows-build-vm.md + Task 1 的 Dockerfile 即镜像文档）；测试策略四条分别落在 Task 4 Step 2/4、Task 2 Step 4 + Task 3 Step 7、Task 3 Step 6/7、Task 5；错误表五种情形分别由钩子分流逻辑（Task 4 Step 1 §2–4）、verify-clang die_env（Task 2）、die_precheck（Task 3）、--no-verify/SKIP（Task 4 Step 2 E、Step 4）、并集逻辑（Task 4 Step 1 §1）覆盖。无缺口。
2. **占位符扫描**：无 TBD/TODO/「适当处理」类占位；所有代码步骤给出完整代码块；文档修改给出逐字文本与插入锚点。
3. **类型/命名一致性**：退出码协议 0/1/75 在 Task 2（die_env/exit 1）、Task 3（die_precheck/透传）、Task 4（分流）三处语义一致；镜像名 `mdeditor-clang:trixie`、Dockerfile 路径 `scripts/docker/Dockerfile.mdeditor-clang`、参数名 `-CompileOnly`（PowerShell switch）/`--compile-only`（bash 长参）、环境变量 `MD_EDITOR_SKIP_PRECHECK` / `MD_EDITOR_PRECHECK_DRY_RUN` 全文一致。
