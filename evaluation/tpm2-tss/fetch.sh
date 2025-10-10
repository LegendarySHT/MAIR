#!/bin/bash
# export PROJ=tpm2-tss
URL="https://github.com/tpm2-software/tpm2-tss.git"

git clone "$URL" repo --depth 1

# 将 json_object *jso_content; 更改为 json_object *jso_content = NULL;
# 注释说明：该变量在第三方库 libjson-c.so 中初始化，由于我们不插桩第三方库，因此，为了避免 MSan的误报，这里手动对其进行初始化
# sed -i 's/json_object \*jso_content;/json_object *jso_content = NULL;/g' repo/src/tss2-fapi/ifapi_ima_eventlog.c
sed -i 's/\*jso_sub;/*jso_sub = NULL;/g' repo/src/tss2-fapi/ifapi_json_eventlog_serialize.c
sed -i 's/\*jso_digests/*jso_digests = NULL/g' repo/src/tss2-fapi/ifapi_json_eventlog_serialize.c

# 从仓库中搜寻语料文件
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if [ -f "$SCRIPT_DIR/create_tpm2tss_corpus.sh" ]; then
    "$SCRIPT_DIR/create_tpm2tss_corpus.sh" || true
else
    echo "警告: 未找到可执行的 create_tpm2tss_corpus.sh，已跳过语料生成" >&2
fi  