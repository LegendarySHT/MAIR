#!/bin/bash
set -e

# 为 libpng@ossfuzz 创建扁平语料
CORPUS_DIR="corpus/libpng@ossfuzz"

echo "创建 libpng 语料..."
rm -rf "$CORPUS_DIR" && mkdir -p "$CORPUS_DIR"

# 参考 fuzzbench：从 png 测试与样例收集少量 png 作为初始语料
# 简化：优先从 repo/contrib/pngsuite 或测试目录中拷贝 png
COUNT=0
if [ -d "repo/contrib/pngsuite" ]; then
  find repo/contrib/pngsuite -type f -name "*.png" -exec cp {} "$CORPUS_DIR/" \; && COUNT=1
fi
if [ $COUNT -eq 0 ] && [ -d "repo/tests" ]; then
  find repo/tests -type f -name "*.png" -exec cp {} "$CORPUS_DIR/" \;
fi

echo "语料文件数量: $(find "$CORPUS_DIR" -type f | wc -l)"

#!/bin/bash
set -e

# 为libpng创建测试语料

CORPUS_DIR="corpus"
REPO_DIR="repo"

echo "为libpng创建测试语料..."

# 创建语料目录
mkdir -p "$CORPUS_DIR"

# 从源码目录收集测试文件
if [ -d "$REPO_DIR" ]; then
    # 查找各种测试文件
    find "$REPO_DIR" -name "*.zip" -o -name "*.png" -o -name "*.jpg" -o -name "*.jpeg" -o -name "*.xml" -o -name "*.json" -o -name "*.txt" -o -name "*.bin" -o -name "*.dat" | while read file; do
        if [[ "$file" == *"test"* ]] || [[ "$file" == *"sample"* ]] || [[ "$file" == *"seed"* ]] || [[ "$file" == *"corpus"* ]]; then
            echo "复制测试文件: $file"
            cp "$file" "$CORPUS_DIR/"
        fi
    done
    
    # 解压种子语料zip文件
    for zip_file in "$CORPUS_DIR"/*.zip; do
        if [ -f "$zip_file" ]; then
            echo "解压种子语料: $zip_file"
            unzip -o "$zip_file" -d "$CORPUS_DIR/"
        fi
    done
fi

# 如果语料目录为空，创建一些基本的测试文件
if [ ! "$(ls -A "$CORPUS_DIR")" ]; then
    echo "创建基本测试文件..."
    echo "test" > "$CORPUS_DIR/test.txt"
    echo '{"test": "data"}' > "$CORPUS_DIR/test.json"
fi

echo "语料创建完成: $CORPUS_DIR"
echo "语料文件数量: $(find "$CORPUS_DIR" -type f | wc -l)"
