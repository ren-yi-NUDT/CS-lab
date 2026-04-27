#!/usr/bin/env bash
set -euo pipefail

# ---- config ----
JOBS="${JOBS:-$(nproc)}"
TRACE_DIR="traces"
BINARY="./predictor_perc"
LOG_DIR="logs_perceptron"
SRC="perceptron.c"
# ----------------

build() {
    echo "[*] Building Perceptron predictor from $SRC ..."
    make clean 2>/dev/null || true
    # 编译: 用 perceptron.c 替代 predictor.c
    gcc -O3 -Wall -Wextra -Winline -Winit-self -Wno-sequence-point \
        -Wno-unused-function -Wno-inline -fPIC -W -Wcast-qual -Wpointer-arith \
        -c -o bt9.o bt9.c
    gcc -O3 -Wall -Wextra -Winline -Winit-self -Wno-sequence-point \
        -Wno-unused-function -Wno-inline -fPIC -W -Wcast-qual -Wpointer-arith \
        -c -o main.o main.c
    gcc -O3 -Wall -Wextra -Winline -Winit-self -Wno-sequence-point \
        -Wno-unused-function -Wno-inline -fPIC -W -Wcast-qual -Wpointer-arith \
        -c -o predictor.o "$SRC"
    gcc -O3 -o "$BINARY" bt9.o main.o predictor.o -lz
    rm -f bt9.o main.o predictor.o
    if [ ! -x "$BINARY" ]; then
        echo "[!] Build failed" >&2
        exit 1
    fi
    echo "[+] Build succeeded."
    echo ""
}

run_one() {
    local trace="$1"
    local name
    name=$(basename "$trace" .bt9.trace.gz)
    local log="$LOG_DIR/${name}.log"
    "$BINARY" "$trace" > "$log" 2>&1
}

main() {
    mkdir -p "$LOG_DIR"
    build

    local traces=()
    for t in "$TRACE_DIR"/*.bt9.trace.gz; do
        [ -f "$t" ] && traces+=("$t")
    done

    if [ ${#traces[@]} -eq 0 ]; then
        echo "[!] No trace files found in $TRACE_DIR/" >&2
        exit 1
    fi

    echo "[*] Running ${#traces[@]} traces (Perceptron, HIST_LEN=20, TABLE=1024) with $JOBS parallel jobs ..."
    SECONDS=0

    export -f run_one
    export BINARY LOG_DIR
    printf '%s\n' "${traces[@]}" | xargs -P "$JOBS" -I{} bash -c 'run_one "$@"' _ {}

    echo ""
    echo "========== All Outputs =========="
    echo ""

    local total=0 ok=0
    for t in "${traces[@]}"; do
        local name
        name=$(basename "$t" .bt9.trace.gz)
        local log="$LOG_DIR/${name}.log"
        total=$((total + 1))
        echo "-------- $name --------"
        if [ -f "$log" ]; then
            cat "$log"
            ok=$((ok + 1))
        else
            echo "  [!] Log file not found"
        fi
        echo ""
    done

    echo "========== MISPRED_PER_1K_INST =========="
    printf "%-25s %s\n" "Trace" "Mispred/1K"
    printf "%-25s %s\n" "-------------------------" "----------"
    for t in "${traces[@]}"; do
        local name
        name=$(basename "$t" .bt9.trace.gz)
        local log="$LOG_DIR/${name}.log"
        if [ -f "$log" ]; then
            local val
            val=$(grep 'MISPRED_PER_1K_INST' "$log" | awk '{print $NF}')
            printf "%-25s %s\n" "$name" "$val"
        else
            printf "%-25s %s\n" "$name" "N/A"
        fi
    done
    echo ""
    echo "Total: $total  OK: $ok  Failed: $((total - ok))"
    echo "Time:  ${SECONDS}s"
    echo "Logs in $LOG_DIR/"
}

main "$@"
