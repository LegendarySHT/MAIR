#!/bin/bash
set -e

CORPUS_DIR="corpus/lcms@cms_transform_fuzzer"

echo "为 lcms@ossfuzz 创建测试语料..."

rm -rf "$CORPUS_DIR" && mkdir -p "$CORPUS_DIR"

# 优先使用 fuzzbench 的 seeds
if [ -d "fuzzbench-temp/benchmarks/lcms_cms_transform_fuzzer/seeds" ]; then
    cp -r fuzzbench-temp/benchmarks/lcms_cms_transform_fuzzer/seeds/* "$CORPUS_DIR/" || true
    echo "已复制 fuzzbench 种子文件到 $CORPUS_DIR"
fi

# 从 oss-fuzz 复制字典文件
if [ -f "oss-fuzz-temp/projects/lcms/icc.dict" ]; then
    cp oss-fuzz-temp/projects/lcms/icc.dict "$CORPUS_DIR/"
    echo "已复制 ICC 字典文件到 $CORPUS_DIR"
fi

# 从 repo 中抽取更多 ICC 测试文件
if [ -d "repo" ]; then
    # ICC/ICM 配置文件、二进制样例
    find repo -type f \( -iname "*.icc" -o -iname "*.icm" -o -iname "*.bin" \) -exec cp {} "$CORPUS_DIR/" \; || true
    echo "已复制 repo 中的 ICC 测试文件到 $CORPUS_DIR"
fi

# 兜底：如果还是没有种子，创建一些基础的 ICC 文件
if [ "$(find "$CORPUS_DIR" -type f | wc -l)" -eq 0 ]; then
    echo "创建基础 ICC 种子文件..."
    # 创建一个最小的 ICC 文件头
    cat > "$CORPUS_DIR/minimal.icc" << 'EOF'
ICC_PROFILE
EOF
    echo "已创建基础种子文件"
fi

echo "语料创建完成: $CORPUS_DIR"
echo "语料文件数量: $(find "$CORPUS_DIR" -type f | wc -l)"


