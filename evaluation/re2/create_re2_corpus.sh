#!/bin/bash
set -e

CORPUS_DIR="corpus/re2@re2_fuzzer"

echo "为 re2@ossfuzz 创建测试语料..."

# 清理并创建语料目录
rm -rf "$CORPUS_DIR" && mkdir -p "$CORPUS_DIR"

# 从 oss-fuzz git 拉取 fuzzer 种子
echo "从 oss-fuzz git 拉取 fuzzer 种子..."
if [ ! -d "oss-fuzz-temp" ]; then
    git clone https://github.com/google/oss-fuzz.git oss-fuzz-temp --depth 1
else
    pushd oss-fuzz-temp
    git pull origin master
    popd
fi

# 复制种子文件到 corpus 目录
if [ -d "oss-fuzz-temp/projects/re2/corpus" ]; then
    cp -r "oss-fuzz-temp/projects/re2/corpus"/* "$CORPUS_DIR/"
    echo "已复制种子文件到 $CORPUS_DIR"
elif [ -d "fuzzbench-temp/benchmarks/re2_fuzzer/seeds" ]; then
    cp -r "fuzzbench-temp/benchmarks/re2_fuzzer/seeds"/* "$CORPUS_DIR/"
    echo "已复制 fuzzbench 种子文件到 $CORPUS_DIR"
else
    echo "从 fuzzing 字典获取正则表达式种子..."
    # 从 Google fuzzing 字典获取正则表达式种子
    curl -s https://raw.githubusercontent.com/google/fuzzing/master/dictionaries/regexp.dict > "$CORPUS_DIR/regexp_dict.txt"
    echo "已下载正则表达式字典到 $CORPUS_DIR/regexp_dict.txt"
    
    echo "从源码仓库提取测试文件..."
    # 从 RE2 源码仓库中提取测试文件
    if [ -d "repo" ]; then
        echo "从 repo 目录提取测试文件..."
        find repo -name "*.txt" -exec cp {} "$CORPUS_DIR/" \;
        find repo -name "*.test" -exec cp {} "$CORPUS_DIR/" \;
        find repo -name "*test*" -type f -name "*.cc" -exec head -20 {} \; > "$CORPUS_DIR/test_patterns.txt" || true
        find repo -name "*example*" -type f -name "*.cc" -exec head -20 {} \; > "$CORPUS_DIR/example_patterns.txt" || true
    fi
    
    # 如果还是没有种子，创建一些基础种子
    if [ "$(find "$CORPUS_DIR" -type f | wc -l)" -eq 0 ]; then
        echo "创建基础正则表达式种子..."
        cat > "$CORPUS_DIR/simple.txt" <<EOF
abc
EOF
        cat > "$CORPUS_DIR/alternation.txt" <<EOF
(a|b|c)+
EOF
        cat > "$CORPUS_DIR/quantifiers.txt" <<EOF
^\d{3,5}[a-z]*$
EOF
        cat > "$CORPUS_DIR/character_classes.txt" <<EOF
[0-9]+[a-zA-Z]*
EOF
        cat > "$CORPUS_DIR/anchors.txt" <<EOF
^start.*end$
EOF
        cat > "$CORPUS_DIR/complex.txt" <<EOF
^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$
EOF
    fi
fi

# 清理临时目录
rm -rf oss-fuzz-temp

echo "语料创建完成: $CORPUS_DIR"
echo "语料文件数量: $(find "$CORPUS_DIR" -type f | wc -l)"
