#!/bin/bash
set -euo pipefail

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
ARTEFACTS_DIR="$ROOT_DIR/artefacts"
CORPUS_ROOT="$ROOT_DIR/corpus"
mkdir -p "$CORPUS_ROOT"

generate_basic_corpus() {
  local target_dir="$1"
  mkdir -p "$target_dir"

  cat >"$target_dir/min.json" <<'JSON'
{}
JSON

  cat >"$target_dir/sample.json" <<'JSON'
{"a":1,"b":[true,false],"s":"x"}
JSON

  # 简单的二进制占位
  printf '\x00\x01\x02\x03' >"$target_dir/min.bin"
}

# 为 fapi-ima-fuzzing 生成有效 IMA 事件日志语料（从 .b64 解码）
generate_fapi_ima_corpus() {
  local target_dir="$1"
  mkdir -p "$target_dir"

  local src_dir="$ROOT_DIR/repo/test/data/fapi/eventlog"
  local files=(
    "sml-ima-ng-sha1.b64"
    "sml-ima-sha1.b64"
    "sml-ima-sig-sha256.b64"
  )

  local any=0
  for f in "${files[@]}"; do
    if [ -f "$src_dir/$f" ]; then
      local out="$target_dir/${f%.b64}"
      base64 -d "$src_dir/$f" >"$out" || { echo "警告: 解码失败 $f" >&2; rm -f "$out" || true; continue; }
      any=1
    fi
  done

  if [ "$any" -eq 0 ]; then
    echo "警告: 未在 $src_dir 找到 IMA 事件日志样例，回退到基础语料。" >&2
    generate_basic_corpus "$target_dir"
  fi
}

# 为 fapi-system-fuzzing 生成固件事件日志语料（从 .b64 解码）
generate_fapi_system_corpus() {
  local target_dir="$1"
  mkdir -p "$target_dir"

  local src_dir="$ROOT_DIR/repo/test/data/fapi/eventlog"
  local files=(
    "binary_measurements_pc_client.b64"
    "binary_measurements_nuc.b64"
    "binary_measurements_hcrtm.b64"
    "event.b64"
    "event-uefivar.b64"
    "event-uefiaction.b64"
    "event-uefiservices.b64"
    "specid-vendordata.b64"
  )

  local any=0
  for f in "${files[@]}"; do
    if [ -f "$src_dir/$f" ]; then
      local out="$target_dir/${f%.b64}"
      base64 -d "$src_dir/$f" >"$out" || { echo "警告: 解码失败 $f" >&2; rm -f "$out" || true; continue; }
      any=1
    fi
  done

  if [ "$any" -eq 0 ]; then
    echo "警告: 未在 $src_dir 找到固件事件日志样例，回退到基础语料。" >&2
    generate_basic_corpus "$target_dir"
  fi
}

# 针对 artefacts 下的每个子目录，在 corpus 下创建同名子目录并放置样例
shopt -s nullglob
created=()
for dir in "$ARTEFACTS_DIR"/*; do
  [ -d "$dir" ] || continue
  name="$(basename "$dir")"
  out_dir="$CORPUS_ROOT/$name"
  case "$name" in
    fapi-ima-fuzzing)
      generate_fapi_ima_corpus "$out_dir"
      ;;
    fapi-system-fuzzing)
      generate_fapi_system_corpus "$out_dir"
      ;;
    *)
      generate_basic_corpus "$out_dir"
      ;;
  esac
  created+=("$out_dir")
done

if [ ${#created[@]} -eq 0 ]; then
  echo "警告: 未在 $ARTEFACTS_DIR 下发现任何子目录，未生成语料。" >&2
else
  printf "已生成语料目录:\n"
  for p in "${created[@]}"; do echo "  - $p"; done
fi



