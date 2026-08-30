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
    # .c（vendored miniz）是 C 源，必须用 C 驱动 clang——C++ 驱动会把合法 C 的
    # 暂定定义（tentative definition）误报为 const 未初始化/redefinition
    driver = "clang" if f.endswith(".c") else "clang++"
    out, skip = [driver, "-fsyntax-only"], False
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
# 并行上限 8：多核低内存主机防止单 TU 被 OOM 杀掉后空日志硬阻断
open(os.path.join(work, "run.sh"), "w").write(
    "#!/bin/bash\n"
    "xargs -d '\\n' -P 8 -n1 sh -c 'eval \"$0\"' < " + shlex.quote(os.path.join(work, "commands.txt")) + "\n"
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
