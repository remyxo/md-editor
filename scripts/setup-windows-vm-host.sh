#!/usr/bin/env bash
# win11 构建机宿主侧一次性配置（在宿主 Debian 上以本用户运行，需在 libvirt 组）
# 做完这步再去 VM 里跑 scripts/provision-windows-vm.ps1（经 ~/Downloads 共享拷入）
# 详见 docs/windows-build-vm.md。幂等，可重复运行。
set -euo pipefail

VM=win11
URI=qemu:///system
REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
POOL_DIR=/var/lib/libvirt/images
DATA_VOL=win11-data.qcow2
DATA_SIZE=60G
SHARE_TAG=md-editor-src
VM_IP=192.168.122.10
SNAP=before-build-vm

say() { printf '\033[36m==> %s\033[0m\n' "$*"; }

# ---- 0. 前置检查（用 --name 判断，避免 domstate 输出本地化）----
if virsh -c "$URI" list --state-running --state-paused --name 2>/dev/null | grep -qx "$VM"; then
    echo "错误: VM $VM 必须先关机（virsh -c $URI shutdown $VM）再运行本脚本" >&2
    exit 1
fi

# ---- 1. 快照（回滚点，已存在则跳过）----
say "1/7 快照 $SNAP"
if virsh -c "$URI" snapshot-info "$VM" "$SNAP" >/dev/null 2>&1; then
    echo "    已存在，跳过"
else
    virsh -c "$URI" snapshot-create-as "$VM" "$SNAP" "加构建配置前回滚点"
fi

# ---- 2. 资源:6 vCPU / 6G 内存 ----
say "2/7 资源调整 → 6 vCPU / 6G"
virsh -c "$URI" setvcpus "$VM" 6 --maximum --config
virsh -c "$URI" setvcpus "$VM" 6 --config
virsh -c "$URI" setmaxmem "$VM" 6291456 --config
virsh -c "$URI" setmem "$VM" 6291456 --config

# ---- 3. 数据盘（经 default 存储池创建，daemon 侧落盘免 sudo）----
say "3/7 数据盘 $DATA_VOL（${DATA_SIZE}，挂 D:）"
if ! virsh -c "$URI" pool-info default >/dev/null 2>&1; then
    virsh -c "$URI" pool-define-as default dir --target "$POOL_DIR"
fi
virsh -c "$URI" pool-start default >/dev/null 2>&1 || true
virsh -c "$URI" pool-autostart default >/dev/null 2>&1 || true
if ! virsh -c "$URI" vol-info --pool default "$DATA_VOL" >/dev/null 2>&1; then
    virsh -c "$URI" vol-create-as --pool default --name "$DATA_VOL" \
        --capacity "$DATA_SIZE" --format qcow2
fi
if virsh -c "$URI" dumpxml "$VM" | grep -q 'target dev=.vdb'; then
    echo "    vdb 已挂载，跳过"
else
    virsh -c "$URI" attach-disk --config "$VM" "$POOL_DIR/$DATA_VOL" vdb \
        --driver qemu --subdriver qcow2 --targetbus virtio
fi

# ---- 4. 第二个 virtiofs 共享（宿主仓库 → 卷标 md-editor-src）----
say "4/7 virtiofs 共享 $SHARE_TAG → $REPO_DIR"
if virsh -c "$URI" dumpxml "$VM" | grep -q "target dir='$SHARE_TAG'"; then
    echo "    已配置，跳过"
else
    tmp="$(mktemp)"
    virsh -c "$URI" dumpxml "$VM" > "$tmp"
    block="    <filesystem type='mount' accessmode='passthrough'>
      <driver type='virtiofs'/>
      <source dir='$REPO_DIR'/>
      <target dir='$SHARE_TAG'/>
    </filesystem>"
    awk -v blk="$block" '/<\/devices>/ && !ins {print blk; ins=1} {print}' "$tmp" > "$tmp.new"
    virsh -c "$URI" define "$tmp.new"
    rm -f "$tmp" "$tmp.new"
fi

# ---- 5. ACL：让 qemu 进程（libvirt-qemu）能遍历并写入仓库 ----
say "5/7 ACL（libvirt-qemu 访问仓库）"
if ! command -v setfacl >/dev/null; then
    echo "    警告: 无 setfacl（apt install acl），跳过——目录若本就对 others 可读写则无需此步" >&2
else
    setfacl -m u:libvirt-qemu:rx /home/"$USER" 2>/dev/null || true
    setfacl -m u:libvirt-qemu:rx "$(dirname "$REPO_DIR")"
    setfacl -m u:libvirt-qemu:rwX "$REPO_DIR"
fi

# ---- 6. DHCP 固定 IP ----
say "6/7 DHCP 绑定 → $VM_IP"
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

# ---- 7. ssh 密钥与别名 ----
say "7/7 宿主 ssh key / config"
if [ ! -f "$HOME/.ssh/id_ed25519" ]; then
    ssh-keygen -t ed25519 -N "" -f "$HOME/.ssh/id_ed25519" -C "$USER-win11-build"
fi
if ! grep -q 'win11-build' "$HOME/.ssh/config" 2>/dev/null; then
    cat >> "$HOME/.ssh/config" <<EOF

# BEGIN md-editor win11 构建机（scripts/setup-windows-vm-host.sh 生成）
Host win11-build
    HostName $VM_IP
    User builder
# END
EOF
fi

say "完成。下一步："
echo "  1. virsh -c $URI start $VM"
echo "  2. 把 scripts/provision-windows-vm.ps1 拷进 ~/Downloads（现有共享），在 VM 管理员 PowerShell 里运行："
echo "       .\provision-windows-vm.ps1 -SshPublicKey '$(cat "$HOME/.ssh/id_ed25519.pub")'"
echo "  3. 回宿主验证: $REPO_DIR/scripts/build-windows-local.sh"
