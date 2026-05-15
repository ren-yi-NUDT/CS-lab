#!/bin/bash
# 编译 Cache 并并行测试所有 trace，单次 awk 批量解析结果

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

echo -e "${BOLD}[1/2] 编译 ...${NC}"
make -j$(nproc) 2>&1 | grep -E "error" || true
echo -e "${GREEN}编译完成${NC}"

echo -e "${BOLD}[2/2] 并行测试 ...${NC}"

TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# 并行启动所有 trace
for trace in traces/*.trace.zst; do
    name=$(basename "$trace" .trace.zst)
    ./Cache "$trace" > "$TMPDIR/$name.log" 2>&1 &
done

# 等待全部完成
wait
echo -e "${GREEN}全部完成${NC}\n"

# 单次 awk 批量解析所有日志
echo -e "${BOLD}命中率汇总${NC}\n"
printf "${CYAN}%-12s %12s %12s %12s %10s${NC}\n" "Trace" "总操作数" "Data命中率" "Inst命中率" "状态"
echo "──────────────────────────────────────────────────────"

for log in "$TMPDIR"/*.log; do
    nm=$(basename "$log" .log)
    t=$(awk '/Memory Trace数量/{gsub(/[^0-9]/,""); print; exit}' "$log")
    d=$(awk '/Data Cache命中率/{gsub(/[^0-9.]/,""); print; exit}' "$log")
    i=$(awk '/Inst Cache命中率/{gsub(/[^0-9.]/,""); print; exit}' "$log")
    if grep -q "Cache模拟成功完成" "$log"; then
        st="${GREEN}✓${NC}"
    elif grep -q "关键错误" "$log"; then
        st="${RED}✗ 错误${NC}"
    else
        st="${YELLOW}? 异常${NC}"
    fi
    printf "%-12s %12s %11s%% %11s%% %b\n" "$nm" "${t:-0}" "${d:-0}" "${i:-0}" "$st"
done

echo "──────────────────────────────────────────────────────"
echo -e "状态: ${GREEN}✓${NC} = 验证通过   ${RED}✗${NC} = 数据不一致   ${YELLOW}?${NC} = 运行失败"
