#!/usr/bin/env bash
# win11 构建机宿主侧一次性配置（在宿主 Debian 上以本用户运行，需在 libvirt 组）
# 做完这步再去 VM 里跑 scripts/provision-windows-vm.ps1（经仓库 virtiofs 共享直接运行）
# 详见 docs/windows-build-vm.md。幂等，可重复运行。
#
# 2026-10-12 适配：宿主重装系统后重建 win11 VM，复用旧工具链数据盘 win10-data.qcow2。
# 盘符防漂移：数据盘不再假设 D:（provision 按卷标 build-data 定位并钉回 D:），
# 仓库共享钉 W:，Downloads 共享自动盘符——见 docs/windows-build-vm.md。
set -euo pipefail

VM=win11
URI=qemu:///system
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DATA_DISK="$HOME/qemu/win11/win10-data.qcow2"   # 工具链数据盘（guest 数据盘卷，卷标 build-data）
SHARE_TAG=md-editor-src                          # 宿主仓库 → guest（卷标=tag，guest 钉 W:）
DL_SHARE_TAG=daijd-shared                        # 宿主 ~/Downloads → guest（安装器预下渠道）
VM_IP=192.168.122.10
SNAP=before-build-vm

say() { printf '\033[36m==> %s\033[0m\n' "$*"; }

# ---- 0. 前置检查（用 --name 判断，避免 domstate 输出本地化）----
if virsh -c "$URI" list --state-running --state-paused --name 2>/dev/null | grep -qx "$VM"; then
    echo "错误: VM $VM 必须先关机（virsh -c $URI shutdown $VM）再运行本脚本" >&2
    exit 1
fi

# ---- 1. 快照（回滚点，已存在则跳过）----
say "1/8 快照 $SNAP"
if virsh -c "$URI" snapshot-info "$VM" "$SNAP" >/dev/null 2>&1; then
    echo "    已存在，跳过"
else
    virsh -c "$URI" snapshot-create-as "$VM" "$SNAP" "加构建配置前回滚点"
fi

# ---- 2. 资源:6 vCPU / 6G 内存 ----
say "2/8 资源调整 → 6 vCPU / 6G"
virsh -c "$URI" setvcpus "$VM" 6 --maximum --config
virsh -c "$URI" setvcpus "$VM" 6 --config
virsh -c "$URI" setmaxmem "$VM" 6291456 --config
virsh -c "$URI" setmem "$VM" 6291456 --config

# ---- 3. 数据盘检查（用户已建盘挂 vdb；缺了才补挂，不再自动创建）----
say "3/8 数据盘 vdb（$DATA_DISK）"
if virsh -c "$URI" dumpxml "$VM" | grep -q 'target dev=.vdb'; then
    echo "    vdb 已挂载，跳过"
else
    if [ ! -f "$DATA_DISK" ]; then
        echo "错误: $DATA_DISK 不存在且 VM 未挂 vdb——请先创建/迁移数据盘（qemu-img create 或旧盘就位）" >&2
        exit 1
    fi
    virsh -c "$URI" attach-disk --config "$VM" "$DATA_DISK" vdb \
        --driver qemu --subdriver qcow2 --targetbus virtio
fi

# ---- 4. 两个 virtiofs 共享（仓库 + Downloads）----
# virtiofs 前置：domain 需共享内存后端（新建 VM 的默认 XML 没有，缺了 define 共享段时报
# "unsupported configuration: 'virtiofs' requires shared memory"）
ensure_shared_memory() {
    if virsh -c "$URI" dumpxml "$VM" | grep -q "<access mode='shared'/>"; then
        echo "    memoryBacking 已共享，跳过"
        return
    fi
    local tmp
    tmp="$(mktemp)"
    virsh -c "$URI" dumpxml "$VM" > "$tmp"
    local block="  <memoryBacking>
    <source type='memfd'/>
    <access mode='shared'/>
  </memoryBacking>"
    awk -v blk="$block" '/<vcpu/ && !ins {print blk; ins=1} {print}' "$tmp" > "$tmp.new"
    virsh -c "$URI" define "$tmp.new"
    rm -f "$tmp" "$tmp.new"
    echo "    已加 memoryBacking（memfd + shared）"
}
ensure_share() {
    local tag="$1" dir="$2"
    if virsh -c "$URI" dumpxml "$VM" | grep -q "target dir='$tag'"; then
        echo "    $tag 已配置，跳过"
        return
    fi
    mkdir -p "$dir"
    local tmp
    tmp="$(mktemp)"
    virsh -c "$URI" dumpxml "$VM" > "$tmp"
    local block="    <filesystem type='mount' accessmode='passthrough'>
      <driver type='virtiofs'/>
      <source dir='$dir'/>
      <target dir='$tag'/>
    </filesystem>"
    awk -v blk="$block" '/<\/devices>/ && !ins {print blk; ins=1} {print}' "$tmp" > "$tmp.new"
    virsh -c "$URI" define "$tmp.new"
    rm -f "$tmp" "$tmp.new"
}
say "4/8 virtiofs 共享 $SHARE_TAG → $REPO_DIR、$DL_SHARE_TAG → ~/Downloads"
ensure_shared_memory
ensure_share "$SHARE_TAG"  "$REPO_DIR"
ensure_share "$DL_SHARE_TAG" "$HOME/Downloads"

# ---- 5. ACL：让 qemu 进程（libvirt-qemu）能遍历并读写共享/磁盘路径 ----
say "5/8 ACL（libvirt-qemu 访问共享与磁盘）"
if ! command -v setfacl >/dev/null; then
    echo "    警告: 无 setfacl（apt install acl），跳过——目录若本就对 others 可读写则无需此步" >&2
else
    setfacl -m u:libvirt-qemu:rx /home/"$USER" 2>/dev/null || true
    setfacl -m u:libvirt-qemu:rx "$HOME/qemu" 2>/dev/null || true      # qcow2 所在路径（遍历）
    setfacl -m u:libvirt-qemu:rwX "$REPO_DIR"                          # 构建要写 .ico 与 dist/
    setfacl -m u:libvirt-qemu:rX "$HOME/Downloads" 2>/dev/null || true # guest 只读拷安装器
fi

# ---- 6. AppArmor 放行（system 实例 qemu 受 AppArmor 约束；写入需 root，打印命令人工执行）----
say "6/8 AppArmor 放行检查"
uuid="$(virsh -c "$URI" dumpxml "$VM" | sed -n "s/.*<uuid>\([^<]*\)<\/uuid>.*/\1/p" | head -1)"
aa_file="/etc/apparmor.d/libvirt/${uuid}.files"
# libvirt 启动 VM 时生成 per-VM profile，其中带 "#include if exists <libvirt/<uuid>.files>"
# ——把路径放行写进 .files 即持久生效（profile 再生成也会带上），无需手动 apparmor_parser
if grep -q "$REPO_DIR" "$aa_file" 2>/dev/null; then
    echo "    $aa_file 已含仓库路径，跳过"
else
    echo "    需 root 写 $aa_file，请复制执行以下命令（VM 启动时自动生效）："
    printf '\033[33m'
    cat <<EOF
sudo tee "$aa_file" >/dev/null <<'AAEOF'
# md-editor win11 构建机：virtiofs 共享与 qcow2 放行（setup-windows-vm-host.sh 生成）
"$HOME/qemu/**" rwk,
"$REPO_DIR/**" rwk,
"$HOME/Downloads/**" rk,
AAEOF
EOF
    printf '\033[0m'
fi

# ---- 7. DHCP 固定 IP ----
say "7/8 DHCP 绑定 → $VM_IP"
mac="$(virsh -c "$URI" dumpxml "$VM" | sed -n "s/.*<mac address='\([^']*\)'.*/\1/p" | head -1)"
if virsh -c "$URI" net-dumpxml default 2>/dev/null | grep -q "$VM_IP"; then
    echo "    已绑定，跳过"
else
    # 真实错误直接中止（网络不存在、语法错等）；绑定后重启网络生效
    virsh -c "$URI" net-update default add ip-dhcp-host \
        "<host mac='$mac' name='$VM' ip='$VM_IP'/>" --config
    virsh -c "$URI" net-destroy default >/dev/null 2>&1 || true
    virsh -c "$URI" net-start default >/dev/null 2>&1 || true
fi

# ---- 8. ssh 密钥与别名 ----
say "8/8 宿主 ssh key / config"
mkdir -p "$HOME/.ssh" && chmod 700 "$HOME/.ssh"
if [ ! -f "$HOME/.ssh/id_ed25519" ]; then
    ssh-keygen -t ed25519 -N "" -f "$HOME/.ssh/id_ed25519" -C "$USER-win11-build"
fi
if ! grep -q 'win11-build' "$HOME/.ssh/config" 2>/dev/null; then
    touch "$HOME/.ssh/config"
    cat >> "$HOME/.ssh/config" <<EOF

# BEGIN md-editor win11 构建机（scripts/setup-windows-vm-host.sh 生成）
Host win11-build
    HostName $VM_IP
    User builder
# END
EOF
fi

say "完成。下一步："
echo "  1. （如上打印了 AppArmor 命令）执行之"
echo "  2. virsh -c $URI start $VM"
echo "  3. VM 内先装 virtio-win guest tools（ISO 里的 virtio-win-gt-x64.exe，含 virtiofs），再在管理员 PowerShell 里运行："
echo "       <仓库共享盘>:\scripts\provision-windows-vm.ps1 -SshPublicKey '$(cat "$HOME/.ssh/id_ed25519.pub")'"
echo "  4. 回宿主验证: $REPO_DIR/scripts/build-windows-local.sh"
