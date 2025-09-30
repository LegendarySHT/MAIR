#!/bin/bash
export PROJ=7zip
URL="https://github.com/mcmilk/7-Zip.git"


# Use depth 1 to avoid cloning the history
git clone "$URL" repo --depth 1

# 原仓库没提供，由于是压缩/解压程序，我们自己提供相应输入：corpus/7zz/sample1.txt
mkdir -p ./corpus/7zz
cat > ./corpus/7zz/sample1.txt << 'EOF'
This is a small sample text file for 7zip benchmarking.
It is intentionally short to keep smoke tests fast and deterministic.
EOF
