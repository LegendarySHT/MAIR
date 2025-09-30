#!/bin/bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
ARTEFACTS_DIR="$ROOT_DIR/artefacts"
CORPUS_DIR="$ROOT_DIR/corpus"
REPO_DIR="$ROOT_DIR/repo"

mkdir -p "$CORPUS_DIR"

# 按 artefacts/<name> 生成对应 corpus/<name>
copy_from_tree() {
    local src_root="$1"; shift || true
    local out_dir="$1"; shift || true
    [ -d "$src_root" ] || return 0

    # 选择性收集：证书/密钥/CRL/配置/测试向量等
    # 限制单文件大小，避免超大样本（例如 512KB）
    local -a patterns=(
        "*.pem" "*.der" "*.crt" "*.cer" "*.key" "*.p12" "*.pfx" "*.crl"
        "*.conf" "*.ini" "*.cnf" "*.cfg" "*.txt" "*.json"
    )

    for pat in "${patterns[@]}"; do
        # 复制并保持相对结构的文件名（去目录，仅保留文件名即可）
        while IFS= read -r -d '' f; do
            base="$(basename "$f")"
            # 去重：已存在则跳过
            [ -e "$out_dir/$base" ] && continue
            cp -f "$f" "$out_dir/$base" 2>/dev/null || true
        done < <(find "$src_root" -type f -name "$pat" -size -512k -print0 2>/dev/null)
    done
}

for ARTEFACT_SUBDIR in "$ARTEFACTS_DIR"/*/; do
    [ -d "$ARTEFACT_SUBDIR" ] || continue
    NAME=$(basename "$ARTEFACT_SUBDIR")
    OUT_DIR="$CORPUS_DIR/$NAME"
    mkdir -p "$OUT_DIR"

    # 基础占位样本
    cat >"$OUT_DIR/min.json" <<'JSON'
{}
JSON

    cat >"$OUT_DIR/sample.json" <<'JSON'
{"a":1,"b":[true,false],"s":"x"}
JSON

    printf '\x00\x01\x02\x03' >"$OUT_DIR/min.bin"

    # 从源码树收集更丰富的样本
    copy_from_tree "$REPO_DIR/testing" "$OUT_DIR"
    copy_from_tree "$REPO_DIR/src/libstrongswan/tests" "$OUT_DIR"
    copy_from_tree "$REPO_DIR/src/pki/tests" "$OUT_DIR" || true
    copy_from_tree "$REPO_DIR/src/swanctl/tests" "$OUT_DIR" || true

    echo "语料已生成: $OUT_DIR"
done


