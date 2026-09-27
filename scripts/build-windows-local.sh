#!/usr/bin/env bash
# 一条命令在本地 win11 KVM 虚拟机里构建 Windows 包（NSIS 安装器，2026-09-27 起不再产 zip）
# 产物落在 dist/（virtiofs 共享即宿主仓库 dist/）。前置与搭建见 docs/windows-build-vm.md
#
# 用法: ./scripts/build-windows-local.sh [--compile-only]
#   --compile-only  预检模式：只编译不打包；virsh/ssh 等环境故障 exit 75 放行（pre-push 钩子用）
# 环境变量:
#   MD_EDITOR_WIN_SSH   ssh 目标（默认 builder@192.168.122.10，setup 脚本绑定的固定 IP）
#   MD_EDITOR_WIN_WAIT  等 ssh 就绪的超时秒数（默认 120）
VM=win11
URI=qemu:///system
SSH_TARGET="${MD_EDITOR_WIN_SSH:-builder@192.168.122.10}"
WAIT_SECS="${MD_EDITOR_WIN_WAIT:-120}"
LAUNCH='C:\md-editor-build-launch.ps1'

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

# 脚本内 dist/ 等为仓库根相对路径，不依赖调用方 cwd
cd "$(dirname "$0")/.." || exit 1

say()  { printf '\033[36m==> %s\033[0m\n' "$*"; }
die()  { printf '\033[31m错误: %s\033[0m\n' "$*" >&2; exit 1; }

# ---- 1. VM 关机则启动（--name 判断，避免 domstate 输出本地化）----
virsh -c "$URI" list --all --name >/dev/null 2>&1 || die_precheck "virsh 连不上 $URI（不在 libvirt 组？）"
if virsh -c "$URI" list --state-running --name 2>/dev/null | grep -qx "$VM"; then
    say "VM $VM 已在运行"
else
    say "启动 VM $VM"
    virsh -c "$URI" start "$VM" || die_precheck "VM 启动失败"
fi

# ---- 2. 等 ssh 就绪 ----
say "等待 ssh 就绪（最多 ${WAIT_SECS}s）"
deadline=$(( SECONDS + WAIT_SECS ))
while ! ssh -o BatchMode=yes -o ConnectTimeout=5 -o StrictHostKeyChecking=accept-new "$SSH_TARGET" "exit 0" 2>/dev/null; do
    if [ "$SECONDS" -ge "$deadline" ]; then
        cat >&2 <<EOF
ssh 到 $SSH_TARGET 超时。排查：
  1. virsh -c $URI domaddr $VM        # 确认实际 IP（setup 绑定的是 192.168.122.10）
  2. VM 里 services.msc 查看 OpenSSH SSH Server (sshd) 是否运行
  3. VM 里 C:\\ProgramData\\ssh\\administrators_authorized_keys 是否含宿主公钥
     （provision 漏传 -SshPublicKey 时需手动补，见 docs/windows-build-vm.md）
EOF
        die_precheck "ssh 到 $SSH_TARGET 超时"
    fi
    sleep 5
done
say "ssh 就绪"

# ---- 3. 触发构建（透传输出与退出码）----
say "触发远程构建（首次冷构建约 10-15 分钟）"
EXTRA_ARGS=""
[ "$COMPILE_ONLY" -eq 1 ] && EXTRA_ARGS=" -CompileOnly"
ssh -o BatchMode=yes "$SSH_TARGET" "powershell -NoProfile -ExecutionPolicy Bypass -File $LAUNCH$EXTRA_ARGS"
rc=$?

# ---- 4. 列出产物 ----
if [ "$rc" -eq 0 ]; then
    if [ "$COMPILE_ONLY" -eq 1 ]; then
        say "编译通过（--compile-only，跳过打包与产物列举）"
        exit 0
    fi
    say "构建完成，产物（dist/）"
    # guest 经 virtiofs 写宿主盘，产物在宿主侧可见可能滞后数秒，带重试
    n=0
    until ls -lh dist/md-editor-*-win64.exe 2>/dev/null; do
        n=$((n + 1))
        [ "$n" -ge 10 ] && { echo "（未找到产物文件——检查上方构建日志）"; break; }
        sleep 1
    done
else
    # rc 90 = VM 内构建环境故障（build-windows-vm.ps1 与 launch 脚本的 virtiofs 共享缺失等）、rc 255 = ssh 会话层故障：非编译错误，预检模式放行
    if [ "$rc" -eq 90 ]; then
        die_precheck "VM 内构建环境故障（exit $rc，virtiofs 共享/launch 环境）"
    elif [ "$rc" -eq 255 ]; then
        die_precheck "ssh 会话层故障（exit $rc）"
    fi
    die "构建失败（exit $rc），见上方日志"
fi
exit "$rc"
