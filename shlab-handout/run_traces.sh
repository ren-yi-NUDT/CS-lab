#!/bin/bash
#
# run_traces.sh - 并行运行全部 16 个 trace，对比 tsh 与 tshref
#
# Usage:
#   ./run_traces.sh              # 跑全部 16 个
#   ./run_traces.sh 01 05 10     # 只跑指定 trace
#

set -u

cd "$(dirname "$0")"

if [ "$#" -gt 0 ]; then
    TRACES=("$@")
else
    TRACES=(01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16)
fi

TMPDIR=$(mktemp -d)
trap 'rm -rf "$TMPDIR"' EXIT

normalize() {
    sed -E 's/\([0-9]+\)/(PID)/g' | \
    awk '
        /^ *[Pp][Ii][Dd] / {                    # ps 表头
            print "PID TTY STAT TIME COMMAND"
            next
        }
        /^(tsh|tshref|\/bin\/ps)/ { next }       # 测试框架自己，跳过
        /\.\/(mysplit|myspin|mystop|myint)\b/ {  # 测试程序（匹配 ./程序名，避免匹配到 awk 脚本中同名串）
            sub(/^ +[0-9]+ +/, "")
            sub(/[0-9]+:[0-9]+/, "TIME")
            print
            next
        }
        # 其他行（系统进程）一律丢弃
        { if (!/^[[:space:]]*[0-9]/) print }     # 非 ps 行原样输出
    '
}

run_one() {
    local i="$1"
    local out_file="$TMPDIR/trace_${i}.out"
    local ref_file="$TMPDIR/trace_${i}.ref"
    local status_file="$TMPDIR/trace_${i}.status"

    perl sdriver.pl -t "trace${i}.txt" -s ./tsh    -a "-p" 2>&1 | normalize > "$out_file"
    perl sdriver.pl -t "trace${i}.txt" -s ./tshref -a "-p" 2>&1 | normalize > "$ref_file"

    if diff -q "$out_file" "$ref_file" > /dev/null; then
        echo "PASS" > "$status_file"
    else
        echo "FAIL" > "$status_file"
    fi
}

echo "=== 并行运行 ${#TRACES[@]} 个 trace (tsh vs tshref) ==="
echo

printf "%-10s %-8s %s\n" "Trace" "Result" "Description"
printf "%-10s %-8s %s\n" "-----" "------" "-----------"

# 并行启动所有 trace
for i in "${TRACES[@]}"; do
    run_one "$i" &
done

# 轮询结果：谁先跑完就先展示，不用等全部结束
PASS_COUNT=0
FAIL_COUNT=0
total=${#TRACES[@]}
done=0

while [ $done -lt $total ]; do
    for i in "${TRACES[@]}"; do
        if [ -f "$TMPDIR/trace_${i}.status" ] && [ ! -f "$TMPDIR/trace_${i}.printed" ]; then
            touch "$TMPDIR/trace_${i}.printed"
            status=$(cat "$TMPDIR/trace_${i}.status")
            desc=$(head -2 "trace${i}.txt" | tail -1 | sed 's/^# *//')
            printf "%-10s %-8s %s\n" "trace$i" "$status" "$desc"
            if [ "$status" = "PASS" ]; then
                PASS_COUNT=$((PASS_COUNT + 1))
            else
                FAIL_COUNT=$((FAIL_COUNT + 1))
            fi
            done=$((done + 1))
        fi
    done
    if [ $done -lt $total ]; then
        sleep 0.1
    fi
done

echo
echo "=== 总结: $PASS_COUNT passed, $FAIL_COUNT failed (共 $total) ==="

if [ "$FAIL_COUNT" -gt 0 ]; then
    echo
    echo "=== 失败 trace 的 diff（< tshref, > tsh）==="
    for i in "${TRACES[@]}"; do
        status=$(cat "$TMPDIR/trace_${i}.status")
        if [ "$status" != "PASS" ]; then
            echo
            echo "--- trace$i ---"
            diff "$TMPDIR/trace_${i}.ref" "$TMPDIR/trace_${i}.out" | head -20
        fi
    done
fi
