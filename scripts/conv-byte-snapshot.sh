#!/usr/bin/env bash
# 转换字节一致快照：用法 conv-byte-snapshot.sh <输出目录> [mdconv路径]
set -euo pipefail
OUT="$1"; MDCONV="${2:-$(git rev-parse --show-toplevel)/build/tests/mdconv}"
mkdir -p "$OUT"
python3 "$(git rev-parse --show-toplevel)/scripts/conv_corpus_digest.py" "$OUT/digest.txt" "$MDCONV"
