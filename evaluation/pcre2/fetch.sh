# #!/bin/bash
URL="https://github.com/PCRE2Project/pcre2.git"


# 如果 repo/.git 不存在，则执行克隆和子模块初始化
if [ ! -d "repo/.git" ]; then
    # 使用 depth 1 避免克隆历史
    git clone "$URL" repo --depth 1

    pushd repo
    git submodule update --init
    popd
fi

mkdir -p corpus/pcre2test
mkdir -p corpus/pcre2grep


# 复制仓库语料到 corpus/pcre
cp repo/testdata/testinput* corpus/pcre2test
dd if=/dev/zero bs=1 count=1M | tr '\0' '\x61' > corpus/pcre2grep/test.txt

