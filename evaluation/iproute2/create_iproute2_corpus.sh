#!/bin/bash
set -euo pipefail

# iproute2 语料生成脚本
# 从源码仓库收集测试样例和配置文件

ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_DIR="$ROOT_DIR/repo"
CORPUS_BASE="$ROOT_DIR/corpus"

echo "开始生成 iproute2 语料..."

# 获取所有产物目录
ARTEFACTS_DIR="$ROOT_DIR/artefacts"
if [ ! -d "$ARTEFACTS_DIR" ]; then
    echo "错误: 未找到 artefacts 目录" >&2
    exit 1
fi

# 为每个产物生成语料
for tool_dir in "$ARTEFACTS_DIR"/*; do
    if [ ! -d "$tool_dir" ]; then
        continue
    fi
    
    tool_name=$(basename "$tool_dir")
    corpus_dir="$CORPUS_BASE/$tool_name"
    
    echo "为 $tool_name 生成语料..."
    mkdir -p "$corpus_dir"
    
    # 收集测试文件
    found_files=0
    
    # 1. 从 testsuite 收集测试文件
    if [ -d "$REPO_DIR/testsuite/tests" ]; then
        # 查找与工具相关的测试文件
        find "$REPO_DIR/testsuite/tests" -type f \( -name "*.t" -o -name "*.dump" -o -name "*.conf" \) | while read -r test_file; do
            if [[ "$test_file" == *"/$tool_name/"* ]] || [[ "$test_file" == *"/$tool_name"* ]]; then
                cp "$test_file" "$corpus_dir/"
                found_files=$((found_files + 1))
            fi
        done
    fi
    
    # 2. 从 examples 收集示例文件
    if [ -d "$REPO_DIR/examples" ]; then
        find "$REPO_DIR/examples" -type f \( -name "*.c" -o -name "*.h" -o -name "*.sh" -o -name "*.conf" \) | while read -r example_file; do
            cp "$example_file" "$corpus_dir/"
            found_files=$((found_files + 1))
        done
    fi
    
    # 3. 从 man 页面收集示例命令
    if [ -d "$REPO_DIR/man" ]; then
        find "$REPO_DIR/man" -name "*.8" -o -name "*.7" | while read -r man_file; do
            if [[ "$man_file" == *"$tool_name"* ]]; then
                # 提取 man 页面中的示例命令
                grep -E "^[[:space:]]*[a-zA-Z0-9_-]+[[:space:]]+[a-zA-Z0-9_-]+" "$man_file" > "$corpus_dir/$(basename "$man_file").examples" 2>/dev/null || true
            fi
        done
    fi
    
    # 4. 生成基本测试命令
    case "$tool_name" in
        "ip")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
link show
addr show
route show
neigh show
rule show
tunnel show
xfrm state show
netns list
EOF
            ;;
        "tc")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
qdisc show
class show
filter show
action show
monitor
EOF
            ;;
        "ss")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
-tuln
-tulnp
-4
-6
-s
EOF
            ;;
        "bridge")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
link show
fdb show
vlan show
mdb show
EOF
            ;;
        "devlink")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
dev show
port show
resource show
param show
EOF
            ;;
        "genl")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
ctrl list
ctrl show
EOF
            ;;
        "rdma")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
dev show
link show
res show
stat show
EOF
            ;;
        "tipc")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
node list
link list
socket list
EOF
            ;;
        "vdpa")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
dev show
dev config show
EOF
            ;;
        "nstat"|"ifstat"|"lnstat"|"rtacct")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
-h
-V
EOF
            ;;
        "arpd")
            cat > "$corpus_dir/basic_commands.txt" << 'EOF'
-h
-V
EOF
            ;;
    esac
    
    # 5. 生成网络配置文件示例
    cat > "$corpus_dir/network_configs.txt" << 'EOF'
# 基本网络配置示例
# 接口配置
ip addr add 192.168.1.1/24 dev eth0
ip link set eth0 up

# 路由配置
ip route add default via 192.168.1.1
ip route add 10.0.0.0/8 via 192.168.1.1

# 规则配置
ip rule add from 192.168.1.0/24 table 100
ip rule add fwmark 1 table 200

# 隧道配置
ip tunnel add gre0 mode gre remote 203.0.113.1 local 198.51.100.1
ip link set gre0 up

# 网络命名空间
ip netns add testns
ip link set veth0 netns testns
EOF
    
    # 6. 生成 TC 配置示例
    if [ "$tool_name" = "tc" ]; then
        cat > "$corpus_dir/tc_configs.txt" << 'EOF'
# TC 配置示例
# 添加 qdisc
tc qdisc add dev eth0 root handle 1: htb default 30
tc class add dev eth0 parent 1: classid 1:1 htb rate 100mbit
tc class add dev eth0 parent 1:1 classid 1:10 htb rate 50mbit ceil 100mbit
tc class add dev eth0 parent 1:1 classid 1:20 htb rate 30mbit ceil 100mbit

# 添加 filter
tc filter add dev eth0 parent 1: protocol ip prio 1 u32 match ip dport 80 0xffff flowid 1:10
tc filter add dev eth0 parent 1: protocol ip prio 2 u32 match ip dport 443 0xffff flowid 1:20

# 添加 action
tc filter add dev eth0 parent 1: protocol ip prio 3 u32 match ip src 192.168.1.0/24 action drop
EOF
    fi
    
    # 7. 生成最小占位文件（如果没找到其他文件）
    if [ ! -f "$corpus_dir"/*.txt ] && [ ! -f "$corpus_dir"/*.conf ] && [ ! -f "$corpus_dir"/*.t ] && [ ! -f "$corpus_dir"/*.dump ]; then
        echo "生成最小占位文件..."
        echo "help" > "$corpus_dir/placeholder.txt"
        echo "--version" >> "$corpus_dir/placeholder.txt"
        echo "-h" >> "$corpus_dir/placeholder.txt"
    fi
    
    # 限制文件大小（避免过大样本）
    find "$corpus_dir" -type f -size +512k -delete 2>/dev/null || true
    
    echo "  - 为 $tool_name 生成了语料目录: $corpus_dir"
done

echo "完成: iproute2 语料生成完成。"
echo "语料目录: $CORPUS_BASE"
