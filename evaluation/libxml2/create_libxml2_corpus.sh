#!/bin/bash
set -e

CORPUS_DIR="corpus/libxml2@xml_fuzzer"

echo "为 libxml2@ossfuzz 创建测试语料..."

# 清理并创建语料目录
rm -rf "$CORPUS_DIR" && mkdir -p "$CORPUS_DIR"

# 从 fuzzbench 复制种子文件
if [ -d "fuzzbench-temp/benchmarks/libxml2_xml_read_memory_fuzzer/seeds" ]; then
    cp -r fuzzbench-temp/benchmarks/libxml2_xml_read_memory_fuzzer/seeds/* "$CORPUS_DIR/" 2>/dev/null || true
    echo "已复制 fuzzbench 种子文件到 $CORPUS_DIR"
fi

# 从 oss-fuzz 复制种子文件
if [ -d "oss-fuzz-temp/projects/libxml2/corpus" ]; then
    cp -r oss-fuzz-temp/projects/libxml2/corpus/* "$CORPUS_DIR/" 2>/dev/null || true
    echo "已复制 oss-fuzz 种子文件到 $CORPUS_DIR"
fi

# 从 repo 中抽取 XML 测试文件
if [ -d "repo" ]; then
    # 查找 XML 测试文件
    find repo -type f \( -name "*.xml" -o -name "*.xhtml" -o -name "*.xsd" -o -name "*.xsl" \) -exec cp {} "$CORPUS_DIR/" \; 2>/dev/null || true
    echo "已复制 repo 中的 XML 测试文件到 $CORPUS_DIR"
    
    # 查找测试目录中的文件
    if [ -d "repo/test" ]; then
        find repo/test -type f -name "*.xml" -exec cp {} "$CORPUS_DIR/" \; 2>/dev/null || true
        echo "已复制 repo/test 中的 XML 文件到 $CORPUS_DIR"
    fi
fi

# 如果还没有种子，创建一些基础的 XML 文件
if [ "$(find "$CORPUS_DIR" -type f | wc -l)" -eq 0 ]; then
    echo "创建基础 XML 种子文件..."
    
    # 创建一个最小的 XML 文件
    cat > "$CORPUS_DIR/minimal.xml" << 'EOF'
<?xml version="1.0"?>
<root>
    <item>test</item>
</root>
EOF

    # 创建一个包含特殊字符的 XML 文件
    cat > "$CORPUS_DIR/special_chars.xml" << 'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<root>
    <item attr="&lt;&gt;&amp;&quot;&apos;">&lt;&gt;&amp;&quot;&apos;</item>
</root>
EOF

    # 创建一个包含 CDATA 的 XML 文件
    cat > "$CORPUS_DIR/cdata.xml" << 'EOF'
<?xml version="1.0"?>
<root>
    <![CDATA[This is CDATA content with <tags> and & symbols]]>
</root>
EOF

    echo "已创建基础种子文件"
fi

# 仅保留尺寸最大的前 30 个种子
TOTAL_FILES=$(find "$CORPUS_DIR" -type f | wc -l | tr -d ' ')
if [ "$TOTAL_FILES" -gt 30 ]; then
    echo "当前种子总数: $TOTAL_FILES，选择尺寸最大的前 30 个..."
    TMP_DIR=$(mktemp -d)
    # 收集按大小降序排序的前 30 个文件路径
    mapfile -t TOP30 < <(find "$CORPUS_DIR" -type f -printf '%s\t%p\n' | sort -nr | head -n 30 | cut -f2-)
    # 复制到临时目录再回填，确保仅保留这 30 个
    for f in "${TOP30[@]}"; do
        cp -p "$f" "$TMP_DIR/"
    done
    rm -rf "$CORPUS_DIR"
    mkdir -p "$CORPUS_DIR"
    cp -p "$TMP_DIR"/* "$CORPUS_DIR/" 2>/dev/null || true
    rm -rf "$TMP_DIR"
    echo "已保留的前 30 个文件:"
    find "$CORPUS_DIR" -maxdepth 1 -type f -printf ' - %p (%s bytes)\n' | sort -nr -k2 | head -n 30
fi

echo "语料创建完成: $CORPUS_DIR"
echo "语料文件数量: $(find "$CORPUS_DIR" -type f | wc -l)"