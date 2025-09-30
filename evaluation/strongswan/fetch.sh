#!/bin/bash
set -euo pipefail
export PROJ=strongswan
URL="https://github.com/strongswan/strongswan.git"
if [ ! -d repo/.git ]; then git clone "$URL" repo --depth 1; else echo "已存在 repo，跳过克隆"; fi

cd repo
patch -p1 < ../repo.patch
cd ..

