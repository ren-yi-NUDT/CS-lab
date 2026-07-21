#!/bin/bash
# run.sh —— 一键启动 web 服务器
# 用法：./run.sh [config.ini]
# 默认读取 ./webserver.ini

set -e

cd "$(dirname "$0")"

CFG="${1:-webserver.ini}"

# 1. 编译（已存在且源码未变则跳过）
if [ ! -x webserver ] || [ webserver.c -nt webserver ]; then
    echo "[run] 编译中..."
    make
fi

# 2. 杀掉可能在跑的旧进程
pkill -x webserver 2>/dev/null && sleep 0.3 || true

# 3. 启动
echo "[run] 启动 webserver，配置文件：$CFG"
echo "[run] Ctrl-C 停止"
echo ""

# 4. 优雅退出：捕获 SIGINT/SIGTERM 转发给子进程
./webserver "$CFG" &
SERVER_PID=$!

trap "echo ''; echo '[run] 收到退出信号，停止 webserver'; kill $SERVER_PID 2>/dev/null; wait $SERVER_PID 2>/dev/null; exit 0" INT TERM

# 5. 等服务启动后打印访问信息
sleep 0.3
PORT=$(grep -E '^port=' "$CFG" 2>/dev/null | head -1 | cut -d= -f2 | tr -d ' \r\n')
PORT="${PORT:-8080}"

cat <<EOF

================================================
  Web 服务器已启动，PID=$SERVER_PID
  访问地址：http://localhost:${PORT}/
  日志文件：$(pwd)/webserver.log
  停止方式：Ctrl-C
================================================

EOF

# 6. 实时显示访问日志
wait $SERVER_PID
