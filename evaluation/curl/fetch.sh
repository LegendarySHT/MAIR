#!/bin/bash
set -e

export PROJ="curl"
URL="https://github.com/curl/curl.git"

echo "克隆 curl 源码..."
if [ ! -d "repo" ]; then
    git clone $URL repo --depth 1
else
    pushd repo
    git pull origin master
    popd
fi

echo "从 curl-fuzzer 拉取 fuzzer 代码和种子..."
if [ ! -d "curl-fuzzer-temp" ]; then
    git clone https://github.com/curl/curl-fuzzer.git curl-fuzzer-temp --depth 1
else
    pushd curl-fuzzer-temp
    git pull origin master
    popd
fi

# 复制 curl_fuzzer.cc 到 repo/tests/fuzzer/
mkdir -p repo/tests/fuzzer
cp "curl-fuzzer-temp/curl_fuzzer.cc" repo/tests/fuzzer/
cp "curl-fuzzer-temp/curl_fuzzer.h" repo/tests/fuzzer/
cp "curl-fuzzer-temp/curl_fuzzer_callback.cc" repo/tests/fuzzer/
cp "curl-fuzzer-temp/curl_fuzzer_tlv.cc" repo/tests/fuzzer/
cp "curl-fuzzer-temp/testinput.h" repo/tests/fuzzer/

# 复制仓库语料到 corpus/curl@curl_fuzzer
SRC_ROOT="curl-fuzzer-temp/corpora"
echo "选择 corpora 中按大小最大的 30 个种子..."
mkdir -p corpus/curl@curl_fuzzer
if [ -d "$SRC_ROOT" ]; then
  TOTAL=$(find "$SRC_ROOT" -type f 2>/dev/null | wc -l)
  echo "源种子文件总数: $TOTAL"
  SEL=0
  TMP_LIST=$(mktemp)
  # 生成 (size \t path) 列表
  find "$SRC_ROOT" -type f -printf '%s\t%p\n' 2>/dev/null > "$TMP_LIST" || true
  # 选出前 30 大并逐个复制（避免管道子进程作用域问题）
  while IFS= read -r __path; do
    [ -f "$__path" ] || continue
    cp -f -- "$__path" corpus/curl@curl_fuzzer/ 2>/dev/null || true
    SEL=$((SEL+1))
  done < <(LC_ALL=C sort -nr "$TMP_LIST" | head -n 30 | cut -f2-)
  rm -f "$TMP_LIST" || true
  echo "已复制: $SEL 个文件到 corpus/curl@curl_fuzzer"
else
  echo "警告: 未找到 $SRC_ROOT 目录，跳过语料复制。"
fi

echo "fetch 完成。语料文件数量: $(find corpus/curl@curl_fuzzer -type f 2>/dev/null | wc -l)"

# 保留 curl-fuzzer-temp 以便复查/重跑（如需清理手动删除）；仅清理无关目录
rm -rf oss-fuzz-temp curl-fuzzer-temp
