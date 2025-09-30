#!/bin/bash
set -e

# 为 libjpeg-turbo@libjpeg_turbo_fuzzer 创建测试语料
# 直接提取测试用例到语料目录，不使用压缩

CORPUS_DIR="corpus/libjpeg-turbo@libjpeg_turbo_fuzzer"
REPO_DIR="repo"

echo "为 libjpeg-turbo@libjpeg_turbo_fuzzer 创建测试语料..."

# 清理并创建语料目录
rm -rf "$CORPUS_DIR" && mkdir -p "$CORPUS_DIR"

# 检查是否已存在语料，避免重复生成
if [ "$(find "$CORPUS_DIR" -type f | wc -l)" -gt 0 ]; then
    echo "语料已存在，跳过生成。"
    echo "语料文件数量: $(find "$CORPUS_DIR" -type f | wc -l)"
    exit 0
fi

# 保存原始工作目录
ORIGINAL_DIR="$PWD"

# 创建临时目录
TEMP_DIR=$(mktemp -d)
cd "$TEMP_DIR"

# 克隆 seed-corpora 仓库
echo "克隆 libjpeg-turbo/seed-corpora 仓库..."
git clone https://github.com/libjpeg-turbo/seed-corpora
git -C seed-corpora checkout 7c9ea5ffaac76ef618657978c9fdfa845d310b93

# 直接复制测试文件到语料目录
echo "提取测试用例到语料目录..."
cd seed-corpora

# # 复制 AFL 测试用例
# if [ -d "afl-testcases" ]; then
#     find afl-testcases -path "*/jpeg*" -type f -exec cp {} "$ORIGINAL_DIR/$CORPUS_DIR/" \;
# fi

# # 复制 bug 报告中的解压缩测试用例
# if [ -d "bugs" ]; then
#     find bugs -path "*/decompress*" -type f -exec cp {} "$ORIGINAL_DIR/$CORPUS_DIR/" \;
# fi

# 复制测试图片
if [ -d "$ORIGINAL_DIR/$REPO_DIR/testimages" ]; then
    find "$ORIGINAL_DIR/$REPO_DIR/testimages" -name "*.jpg" -type f -exec cp {} "$ORIGINAL_DIR/$CORPUS_DIR/" \;
fi

# 清理临时目录
cd "$ORIGINAL_DIR"
rm -rf "$TEMP_DIR"

echo "语料创建完成: $CORPUS_DIR"
echo "语料文件数量: $(find "$CORPUS_DIR" -type f | wc -l)"