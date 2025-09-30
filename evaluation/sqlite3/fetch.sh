#!/bin/bash
set -e

# 修改自来自 fuzzbench
mkdir -p repo corpus/sqlite3@ossfuzz


if [ ! -f sqlite3.tar.gz ]; then
    curl 'https://sqlite.org/src/tarball/sqlite.tar.gz?r=c78cbf2e86850cc6' -o sqlite3.tar.gz
fi

if ! file sqlite3.tar.gz | grep -q 'gzip compressed data'; then
    echo "错误：sqlite3.tar.gz 不是有效的 gzip 压缩包，本下载请求被 sqlite.org 网站拦截（如出现了验证码或 HTML 页面）。"
    echo "请手动通过浏览器访问以下链接下载 sqlite3.tar.gz，并放到 repo 目录下："
    echo "    https://sqlite.org/src/tarball/sqlite.tar.gz?r=c78cbf2e86850cc6"
    exit 1
fi

pushd repo
tar xzf ../sqlite3.tar.gz --strip-components 1
popd


find repo -name "*.test" -exec cp {} corpus/sqlite3@ossfuzz/ \;
# Although sqlite3 owns a corpus set with 1000+ inputs,
# the most inputs fed to sqlite3@ossfuzz are too simple that the PUT execution time ~1ms, 
# which are not useful for our evaluation (the relevant biases are inevitable).
# Therefore, we only select seeds with PUT execution time >5ms or ~5ms.
WHITELIEST_SEEDS=(
    "crash01.test"
    "multiwrite01.test"
    "randexpr1.test"
    "window3.test"
    "fts1porter.test"
    "fts5porter.test"
)

# 先将白名单内的种子移动到tmp目录，删除其余所有种子，再将tmp目录下的种子移回，最后删除tmp目录
mkdir -p tmp_sqlite3_seeds
for w in "${WHITELIEST_SEEDS[@]}"; do
    if [ -f "corpus/sqlite3@ossfuzz/$w" ]; then
        mv "corpus/sqlite3@ossfuzz/$w" tmp_sqlite3_seeds/
    fi
done
rm -f corpus/sqlite3@ossfuzz/*.test
mv tmp_sqlite3_seeds/* corpus/sqlite3@ossfuzz/ 2>/dev/null || true
rmdir tmp_sqlite3_seeds
