#!/bin/bash
export PROG_DIR=$(cd "$(dirname "$0")"; pwd)

# arpd ARP helper daemon 参数设置
# 基于arpd文档可用的参数:
# -l: dump数据库并退出  
# -f FILE: 从文件读取ARP数据库
# -b DATABASE: 数据库文件位置
# -a NUMBER: 主动查询次数  
# -k: 抑制内核广播查询
# -n TIME: 负缓存超时
# -p TIME: 轮询间隔
# -R RATE: 广播速率
# -B NUMBER: 连续广播次数
# -h: 帮助信息
# -V: 版本信息

export PRE_OPT="-V"
export POST_OPT=""
export stdin="false"

$PROG_DIR/../../../run.sh $@
