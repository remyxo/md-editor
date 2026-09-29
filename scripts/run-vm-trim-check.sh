#!/usr/bin/env bash
# VM 侧打包瘦身验证：安全砍后的体积对账 + dumpbin 依赖链 + 激进砍 A/B
# 用法: run-vm-trim-check.sh   （先跑 build-windows-local.sh 完成构建打包）
set -euo pipefail
SSH_TARGET="${MD_EDITOR_WIN_SSH:-builder@192.168.122.10}"
ssh -o BatchMode=yes -o ConnectTimeout=10 "$SSH_TARGET" 'powershell -NoProfile -ExecutionPolicy Bypass -File W:\scripts\verify-package-trim.ps1'
