#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
INPUT="$SCRIPT_DIR/microgpt/input.txt"

# Check input file
if [ ! -f "$INPUT" ]; then
    echo "input.txt not found, downloading..."
    curl -sL 'https://raw.githubusercontent.com/karpathy/makemore/988aa59/names.txt' -o "$INPUT"
fi

show_help() {
    cat <<EOF
Usage: ./run.sh [python|rust|bench|help]

  python   Run the Python microGPT (microgpt/microgpt.py)
  rust     Run the Rust microGPT (microgpt-rs, auto-build if needed)
  bench    Run both and compare time
  help     Show this message

Default (no args): interactive menu
EOF
}

run_python() {
    echo "==> Running Python microGPT..."
    cd "$SCRIPT_DIR/microgpt"
    python3 microgpt.py
}

run_rust() {
    echo "==> Running Rust microGPT..."
    cd "$SCRIPT_DIR/microgpt-rs"
    if [ ! -f target/release/microgpt-rs ]; then
        echo "    Building (first time, may take a minute)..."
        cargo build --release 2>&1 | tail -1
    fi
    # Copy input.txt so the Rust binary can find it
    cp "$INPUT" ./input.txt
    ./target/release/microgpt-rs
}

run_bench() {
    echo "==> Benchmarking Python vs Rust..."
    echo ""

    echo "--- Python ---"
    time_py=$( { time run_python 2>&1; } 2>&1 )
    echo "$time_py"
    echo ""

    echo "--- Rust ---"
    time_rs=$( { time run_rust 2>&1; } 2>&1 )
    echo "$time_rs"
}

# --- Main ---
case "${1:-}" in
    python|py|p) run_python ;;
    rust|rs|r)   run_rust   ;;
    bench|b)     run_bench  ;;
    help|h|-h|--help) show_help ;;
    *)
        echo "microGPT Launcher"
        echo "================="
        echo "  1) Python (microgpt.py)"
        echo "  2) Rust   (microgpt-rs)"
        echo "  3) Benchmark both"
        echo "  q) Quit"
        echo ""
        read -rp "Choose [1/2/3/q]: " choice
        case "$choice" in
            1) run_python ;;
            2) run_rust   ;;
            3) run_bench  ;;
            q|Q) echo "Bye." ;;
            *)   echo "Invalid choice." ;;
        esac
        ;;
esac
