#!/usr/bin/env bash
# ============================================================
# Binary Bomb Lab — 分阶段演示脚本 (动画版)
# 用法: ./demo.sh [学号]
# 默认学号: 720028
# ============================================================

set -e

SID="${1:-720028}"
BOMB="./bomb_linux"
ANSWER="bomb_${SID}.txt"
DECOMPILED="./bomb_decompiled"

# ── 颜色定义 ──
RST='\033[0m'
BOLD='\033[1m'
DIM='\033[2m'
BLINK='\033[5m'
RED='\033[31m'
GREEN='\033[32m'
YELLOW='\033[33m'
BLUE='\033[34m'
MAGENTA='\033[35m'
CYAN='\033[36m'
WHITE='\033[37m'

# ── 动画速度 (秒) ──
T_CHAR=0.018       # 打字速度: 每字符
T_LINE=0.3        # 逐行弹出: 行间延迟
T_BLOCK=0.6        # 区块间延迟
T_DRAMATIC=0.6     # 重要信息前的戏剧性停顿

# ── 工具函数 ──

# 逐字符打印 (支持 ANSI 颜色前缀)
typewrite() {
    local color_prefix="$1"
    local text="$2"
    local color_suffix="${RST}"
    echo -ne "  ${color_prefix}"
    for ((i=0; i<${#text}; i++)); do
        printf "%s" "${text:$i:1}"
        sleep "$T_CHAR"
    done
    echo -e "${color_suffix}"
}

# 逐字符打印 (无缩进)
typewrite_raw() {
    local color_prefix="$1"
    local text="$2"
    echo -ne "${color_prefix}"
    for ((i=0; i<${#text}; i++)); do
        printf "%s" "${text:$i:1}"
        sleep "$T_CHAR"
    done
    echo -e "${RST}"
}

# 弹出一行描述文字 (带延迟)
pop_line() {
    echo -e "  ${WHITE}$1${RST}"
    sleep "$T_LINE"
}

# 弹出多行描述
pop_desc() {
    while IFS= read -r line; do
        echo -e "  ${WHITE}${line}${RST}"
        sleep "$T_LINE"
    done <<< "$1"
}

# 动画画线 (上下边框)
draw_line() {
    local char="${1:-━}"
    local color="${2:-$YELLOW}"
    echo -ne "  ${color}"
    for ((i=0; i<61; i++)); do
        printf "%s" "$char"
        sleep 0.005
    done
    echo -e "${RST}"
}

# 动画 Phase 标题
phase_header() {
    local num="$1"
    local title="$2"
    echo ""
    draw_line "━" "$YELLOW"
    echo -ne "  ${BOLD}${GREEN}"
    local text="Phase ${num}: ${title}"
    for ((i=0; i<${#text}; i++)); do
        printf "%s" "${text:$i:1}"
        sleep 0.022
    done
    echo -e "${RST}"
    draw_line "━" "$YELLOW"
    echo ""
    sleep "$T_BLOCK"
}

# 代码块 (逐行弹出)
code_block() {
    echo -e "  ${DIM}  ┌─────────────────────────────────────────────────┐${RST}"
    sleep 0.1
    while IFS= read -r line; do
        echo -ne "  ${DIM}  │${RST} ${CYAN}"
        for ((i=0; i<${#line}; i++)); do
            printf "%s" "${line:$i:1}"
            sleep 0.012
        done
        echo -e "${RST}"
        sleep 0.15
    done <<< "$1"
    echo -e "  ${DIM}  └─────────────────────────────────────────────────┘${RST}"
    echo ""
    sleep "$T_BLOCK"
}

# 步骤标签
pop_step() {
    local label="$1"
    local text="$2"
    echo -ne "  ${BLUE}[${label}]${RST} "
    for ((i=0; i<${#text}; i++)); do
        printf "%s" "${text:$i:1}"
        sleep 0.02
    done
    echo ""
    sleep "$T_LINE"
}

# 关键洞察 (紫色 + 闪烁感)
pop_insight() {
    sleep "$T_DRAMATIC"
    echo -ne "  ${BOLD}${MAGENTA}>>> "
    for ((i=0; i<${#1}; i++)); do
        printf "%s" "${1:$i:1}"
        sleep 0.025
    done
    echo -e "${RST}"
    echo ""
    sleep "$T_BLOCK"
}

# 答案揭晓 (戏剧性)
pop_answer() {
    sleep "$T_DRAMATIC"
    echo -ne "  ${BOLD}${GREEN}>>> 答案: "
    sleep 0.3
    echo -ne "${YELLOW}${BOLD}"
    for ((i=0; i<${#1}; i++)); do
        printf "%s" "${1:$i:1}"
        sleep 0.04
    done
    echo -e "${RST}"
    echo ""
    sleep "$T_BLOCK"
}

# 分隔线
sep() {
    echo ""
    sleep 0.15
}

# 按 Enter 继续
press_enter() {
    echo -e "  ${DIM}─────────────────────────────────────────${RST}"
    echo -e "  ${DIM}[按 Enter 继续下一关]${RST}"
    read -r
    echo ""
}

# ══════════════════════════════════════════════════════════════
#  主流程
# ══════════════════════════════════════════════════════════════

clear

# ── 开场动画 ──

echo ""
echo -e "${BOLD}${RED}"
echo "  ██████╗ ██╗███╗   ██╗ ██████╗ ███████╗███████╗"
echo "  ██╔══██╗██║████╗  ██║██╔════╝ ██╔════╝██╔════╝"
echo "  ██████╔╝██║██╔██╗ ██║██║  ███╗█████╗  █████╗  "
echo "  ██╔══██╗██║██║╚██╗██║██║   ██║██╔══╝  ██╔══╝  "
echo "  ██████╔╝██║██║ ╚████║╚██████╔╝███████╗███████╗"
echo "  ╚═════╝ ╚═╝╚═╝  ╚═══╝ ╚═════╝ ╚══════╝╚══════╝"
echo -e "${RST}"
sleep 0.3
typewrite_raw "${BOLD}${YELLOW}" "         B I N A R Y   B O M B   L A B"
typewrite_raw "${DIM}"  "                  Bomb Defusal Demo"
echo ""
sleep 0.3
pop_line "学号: ${BOLD}${CYAN}${SID}${RST}"
pop_line "工具: ${BOLD}GDB${RST} + ${BOLD}objdump${RST} + ${BOLD}逆向分析${RST}"
echo ""
sleep 0.5

# ════════════════════════════════════════════════════════════
#  Phase 1
# ════════════════════════════════════════════════════════════

phase_header "1" "字符串比较 — 读取秘密字符串"

pop_desc "程序会调用 GenerateRandomString() 生成一个随机字符串，
然后要求你输入一模一样的字符串。
答不对就直接 explode_bomb() 爆炸退出。
没有任何提示，你根本不知道它要什么字符串。"
sep

pop_step "思路" "找到比较逻辑，在比较之前把秘密字符串偷出来"
pop_line ""
pop_desc "反汇编 phase_1 函数，发现核心逻辑非常简单：
  调用 GenerateRandomString 生成字符串
  调用 strcmp(input, secret) 比较输入和秘密字符串
  不相等就爆炸"
sep

pop_step "GDB" "在 strcmp 调用之前设断点"
code_block "break *0x401b7d
run 720028
(断点命中时随便输入 test 回车)"

pop_desc "x86-64 调用约定: 第 1 个参数放 %rdi，第 2 个参数放 %rsi。
所以 strcmp(input, secret) 中：
  %rdi = input    (输入的字符串)
  %rsi = secret   (程序生成的秘密字符串)"
sep

pop_step "GDB" "断点命中！直接读取 %rsi 拿到秘密字符串"
code_block "(gdb) x/s \$rsi
0x7fffffffd695: \"yxYZRkQzJt\""

pop_insight "x/s 命令把寄存器指向的内存当字符串打印 — 一步秒杀！"

pop_answer "yxYZRkQzJt"

pop_line "${DIM}写入 bomb_720028.txt 第 1 行: yxYZRkQzJt${RST}"

press_enter

# ════════════════════════════════════════════════════════════
#  Phase 2
# ════════════════════════════════════════════════════════════

phase_header "2" "六个递增整数 — 随机子关卡分发"

pop_desc "这关要输入 6 个空格分隔的整数，比如: 1 2 3 4 5 6
程序内部根据学号作为随机种子，分发到 16 个不同子关卡之一。
每个子关卡有不同的约束条件。
不同学号的人看到的题目不一样！"
sep

pop_step "分析" "先找到分发到了哪个子关卡"
pop_desc "在 phase_2 的分发跳转处设断点，
运行后查看全局变量 rand_div (地址 0x408820) 的值。
这个变量是整个实验中查得最多的"
sep

pop_step "GDB" "查看 rand_div，确定进入的子关卡"
code_block "(gdb) break *0x401bb3
(gdb) run 720028 < bomb_720028.txt
(gdb) p *(long long*)0x408820
\$1 = 13

rand_div = 13  →  进入 phase_2_13"

pop_desc "进入 phase_2_13 后，反汇编 (disas phase_2_13) 阅读约束逻辑：
逐段分析汇编，识别出三个关键循环。"
sep

pop_step "约束1" "首元素约束"
pop_desc "代码调用了 GenerateRandomNumber(50)，
然后把 rand_div + 16 作为 a[0] 的期望值。"
code_block "(gdb) break *0x4026b0
(gdb) run 720028 < bomb_720028.txt
(gdb) p *(long long*)0x408820
\$2 = 28

a[0] = 28 + 16 = 44"

pop_step "约束2" "非负约束 — 循环检查 a[i] >= 0"
pop_desc "反汇编中有一个循环，逐个比较 a[i] 和 0，
如果 a[i] < 0 就跳到 explode_bomb()。"
sep

pop_step "约束3" "严格递增 — 循环检查 a[i] > a[i-1]"
pop_desc "另一个循环，比较相邻元素：
如果 a[i] <= a[i-1]，爆炸！"
sep

pop_desc "综合三个约束：
  1. a[0] == 44
  2. 所有 a[i] >= 0
  3. a[i] > a[i-1] (严格递增)

最简单的解：从 44 开始连续递增 6 个数"

pop_insight "最简解: 44 45 46 47 48 49"

pop_answer "44 45 46 47 48 49"

pop_line "${DIM}写入 bomb_720028.txt 第 2 行: 44 45 46 47 48 49${RST}"

press_enter

# ════════════════════════════════════════════════════════════
#  Phase 3
# ════════════════════════════════════════════════════════════

phase_header "3" "数字+字符+数字 — switch-case 分支"

pop_desc "这关要求输入格式: 数字 字符 数字 (如 133 E 106)
程序用 sscanf 解析出三个字段，然后分别校验。
同样有随机子关卡分发 (14 个子关卡之一)。"
sep

pop_step "分析" "先找到分发结果"
code_block "(gdb) break *0x4028b4
(gdb) run 720028 < bomb_720028.txt
(gdb) p *(long long*)0x408820
\$3 = 13

rand_div = 13  →  进入 phase_3_13"

pop_desc "反汇编 phase_3_13，发现它先调 GenerateRandomNumber(8)，
然后把结果 +130 作为第一个数字 val1 的期望值。
接着用 switch(val1 - 130) 分支来决定字符 ch 和第二个数字 val2。"
sep

pop_step "提取" "第一个数字 val1"
code_block "(gdb) break *0x40421f
(gdb) run 720028 < bomb_720028.txt
(gdb) p *(long long*)0x408820
\$4 = 3

val1 = 3 + 130 = 133"

pop_desc "val1 = 133，所以 switch 进入了 case 3 的分支。
在 case 3 的代码中，程序又调了两次随机数：
一次生成期望字符，一次生成期望数字。"
sep

pop_step "提取" "期望字符 ch"
pop_desc "在 case 分支中设置期望字符的指令处设断点：
程序把字符放到了 %al (rax 的低 8 位) 寄存器中。"
code_block "(gdb) break *0x404383
(gdb) run 720028 < bomb_720028.txt
(gdb) printf \"%c\\n\", \$al
E"

pop_step "提取" "第二个数字 val2"
code_block "(gdb) break *0x404390
(gdb) continue
(gdb) p *(long long*)0x408820
\$5 = 106

val2 = 106"

pop_desc "每个 switch-case 分支的代码路径不同，
生成了不同的随机字符和数字。
必须在正确的分支处设断点才能提取到对应的值。"

pop_insight "三个值藏在不同的代码路径中，需要逐步断点精准提取！"

pop_answer "133 E 106"

pop_line "${DIM}写入 bomb_720028.txt 第 3 行: 133 E 106${RST}"

press_enter

# ════════════════════════════════════════════════════════════
#  Phase 4
# ════════════════════════════════════════════════════════════

phase_header "4" "阶乘查表 — 魔数除法 + 跳转表陷阱"

pop_desc "这关输入一个整数 x，程序会：
  1. 检查 x > 499
  2. 计算 quotient = x / 500
  3. 计算 quotient 的阶乘
  4. 把阶乘结果和预计算的表做比较"
sep

pop_step "警告" "跳转表陷阱：rand_div 的值 ≠ 子关卡号！"
pop_desc "这关的跳转表不是简单的 rand_div → phase_4_{rand_div} 映射！
必须看实际跳转目标才能确定进入了哪个子关卡。"
code_block "(gdb) break *0x404604
(gdb) run 720028 < bomb_720028.txt
(gdb) x/i \$rax
→ 0x4046ec <phase_4+305>: mov -0x8(%rbp),%rax

(gdb) x/3i 0x4046ec
→ 0x4046f3: call 0x40548f <phase_4_24>

rand_div = 14，但实际跳到了 phase_4_24！"

pop_insight "不看实际跳转目标就猜子关卡号，必炸！"
sep

pop_step "分析" "phase_4_24 的算法"
pop_desc "反汇编 phase_4_24，发现核心逻辑：
  1. sscanf 读取一个整数 x
  2. 检查 x > 0 且 x > 499 (cmp \$0x1f3; jg)
  3. 计算 quotient = x / 500

但是！编译器把 x/500 优化成了「魔数乘法」：
  imul \$0x10624dd3, %rax, %rax
  shr  \$0x24, %rax

这不是什么神秘运算，就是 x / 500 的编译器优化版本。"
sep

pop_step "分析" "阶乘函数 func4_2"
pop_desc "反汇编 func4_2，识别出递归结构：
  if n <= 1: return 1
  else: return n * func4_2(n-1)

这就是经典的阶乘函数！"
sep

pop_step "分析" "预计算阶乘表"
pop_desc "从反汇编中直接读出内存中的表数据："
echo -e "  ${DIM}  ┌────────────────────────────────────────┐${RST}"
echo -e "  ${DIM}  │${RST} ${CYAN}索引  值        含义${RST}"
echo -e "  ${DIM}  │${RST} ${WHITE} 0    24         4!${RST}"
echo -e "  ${DIM}  │${RST} ${WHITE} 1    120        5!${RST}"
echo -e "  ${DIM}  │${RST} ${WHITE} 2    720        6!${RST}"
echo -e "  ${DIM}  │${RST} ${WHITE} 3    5040       7!${RST}"
echo -e "  ${DIM}  │${RST} ${WHITE} 4    40320      8!${RST}"
echo -e "  ${DIM}  │${RST} ${WHITE} 5    362880     9!${RST}"
echo -e "  ${DIM}  │${RST} ${WHITE} 6    3628800    10!${RST}"
echo -e "  ${DIM}  └────────────────────────────────────────┘${RST}"
sleep "$T_BLOCK"
sep

pop_step "GDB" "查表索引，确定目标阶乘值"
code_block "(gdb) break *0x40554a
(gdb) run 720028 < bomb_720028.txt
(gdb) p *(long long*)0x408820
\$1 = 5

rand_div = 5  →  table[5] = 362880 = 9!"

pop_step "反推" "从阶乘结果反推 x 的值"
pop_desc "目标: factorial(quotient) = 362880 = 9!
  所以 quotient = 9
  又 quotient = x / 500
  所以 x = 9 × 500 = 4500
  验证: 4500 > 499 ✓"

pop_insight "魔数乘法 0x10624dd3 其实就是编译器对 x/500 的优化！"

pop_answer "4500"

pop_line "${DIM}写入 bomb_720028.txt 第 4 行: 4500${RST}"

press_enter

# ════════════════════════════════════════════════════════════
#  Phase 5
# ════════════════════════════════════════════════════════════

phase_header "5" "不可能任务 — Shellcode 注入"

pop_desc "前四关输入的是「数据」，程序拿数据做判断。
第五关不同：程序把输入的 hex 字符串翻译成字节，
然后让 CPU 直接执行这些字节当作指令！"
sep

echo -e "  ${BOLD}${YELLOW}  hex 输入 → tohex() 翻译 → CPU 直接执行${RST}"
echo -e "  ${DIM}  \"bf00040000...\"  →  buf=[0xbf,0x00,...]  →  mov edi, 0x400${RST}"
echo ""
sleep "$T_BLOCK"

pop_desc "指令和数据在内存中没有本质区别，CPU 只是根据 PC 指针
去取指执行，不管那个位置存的是什么。"
sep

pop_step "检查1" "长度检查"
pop_desc "hex 字符串长度必须在 10~768 之间，
对应 5~384 字节的机器码。太短或太长都会爆炸。"
sep

pop_step "检查2" "XOR 校验"
pop_desc "buf 的 256 字节逐个异或，结果必须等于 rand_div & 0xFF。
这是一个简单的校验和机制，防止随便输入。"
code_block "(gdb) break *0x401808
(gdb) run 720028 < bomb_720028.txt
(gdb) p *(long long*)0x408820
\$1 = 540

540 & 0xFF = 0x1c  →  XOR 目标 = 0x1c"

pop_step "检查3" "超时检查 — 执行时间 ≤ 1000ms"
sep

pop_step "设计" "Shellcode 需要做什么？"
pop_desc "CPU 跳到 buf 后，我们需要它做这些事：
  1. 调用 GenerateRandomNumber(0x400) — 更新随机数
  2. 把 rand_div 存入全局变量 result (地址 0x4087f8)
  3. 跳回主程序 0x4018a0，让后续检查通过
然后主程序会再次调 GenerateRandomNumber(0x400)，
比较 result 是否等于新的 rand_div。"
sep

pop_step "编码" "手动翻译汇编 → 机器码"
echo ""
echo -e "  ${DIM}  ┌──────────────────────────────────────────────────────────────┐${RST}"
sleep 0.1
echo -e "  ${DIM}  │${RST} ${CYAN}汇编指令                        机器码                    含义${RST}"
echo -e "  ${DIM}  ├──────────────────────────────────────────────────────────────┤${RST}"
sleep 0.15

asm_table=(
    "mov edi, 0x400                |bf 00 04 00 00             |参数=1024"
    "movabs rax, 0x4016ad          |48 b8 ad 16 40 ..          |GenerateRandomNumber"
    "call rax                      |ff d0                      |调用函数"
    "movabs rax, 0x408820          |48 b8 20 88 40 ..          |&rand_div"
    "mov rax, [rax]                |48 8b 00                   |读取 rand_div"
    "movabs rcx, 0x4087f8          |48 b9 f8 87 40 ..          |&result"
    "mov [rcx], eax                |89 01                      |result = rand_div"
    "push 0x4018a0                 |68 a0 18 40 00             |目标地址压栈"
    "ret                           |c3                         |跳转到 0x4018a0"
)

for entry in "${asm_table[@]}"; do
    IFS='|' read -r col1 col2 col3 <<< "$entry"
    printf "  ${DIM}  │${RST} ${WHITE}%-30s %-26s %-10s${RST}\n" "$col1" "$col2" "$col3"
    sleep 0.2
done

echo -e "  ${DIM}  └──────────────────────────────────────────────────────────────┘${RST}"
echo ""
sleep "$T_BLOCK"

pop_step "修正" "解决 XOR 校验"
pop_desc "Shellcode 只有几十字节，buf 有 256 字节。
多出来的位置填 0，然后用一个「修正字节」让整体 XOR = 0x1c。

原理：假设当前 256 字节的 XOR 值为 X，
在 shellcode 末尾放一个字节 = X XOR 0x1c，
那么总体 XOR = X XOR (X XOR 0x1c) = 0x1c ✓"
sep

pop_step "彩蛋" "触发隐藏函数 phase_secret"
pop_desc "程序里有个死代码函数 phase_secret (地址 0x401a8b)，
正常流程永远不会调用它。
在 shellcode 中加入两条指令就可以触发："
code_block "mov edi, 0                # bf 00 00 00 00
movabs rax, 0x401a8b       # 48 b8 8b 1a 40 ..
call rax                   # ff d0

输出: 不可能的...指令和数据的世界已经混乱...SOS..."

pop_answer "(38 字节 shellcode + 修正字节 + 填充 = 256 字节 hex)"

pop_line "${DIM}写入 bomb_720028.txt 第 5 行: Y${RST}"
pop_line "${DIM}写入 bomb_720028.txt 第 6 行: bf0004000048b8ad...${RST}"

press_enter

# ════════════════════════════════════════════════════════════
#  实弹演示
# ════════════════════════════════════════════════════════════

clear
echo ""
echo -ne "  ${CYAN}"
for ((i=0; i<61; i++)); do printf "═"; done
echo -e "${RST}"
echo -ne "  ${BOLD}${CYAN}"
title_t="实弹演示 — 运行真实炸弹"
for ((i=0; i<${#title_t}; i++)); do
    printf "%s" "${title_t:$i:1}"
    sleep 0.025
done
echo -e "${RST}"
echo -ne "  ${CYAN}"
for ((i=0; i<61; i++)); do printf "═"; done
echo -e "${RST}"
echo ""

sleep "$T_BLOCK"

pop_line "${BOLD}现在用答案文件运行真实炸弹，验证全部通关:${RST}"
echo ""
echo -ne "  ${DIM}命令: ${YELLOW}"
sleep 0.3
echo -ne "${BOMB} ${SID} < ${ANSWER}"
sleep 0.5
echo -e "${RST}"
echo ""
sleep 0.5

if [ -x "$BOMB" ] && [ -f "$ANSWER" ]; then
    echo -e "  ${GREEN}--- 炸弹输出开始 ---${RST}"
    echo ""
    "$BOMB" "$SID" < "$ANSWER" 2>&1 | while IFS= read -r line; do
        if [[ "$line" == *"爆了"* ]]; then
            echo -e "  ${BOLD}${RED}${line}${RST}"
        elif [[ "$line" == *"通过"* ]]; then
            echo -e "  ${BOLD}${GREEN}${line}${RST}"
        elif [[ "$line" == *"不可能"* ]] || [[ "$line" == *"SOS"* ]]; then
            echo -e "  ${BOLD}${MAGENTA}${line}${RST}"
        elif [[ "$line" == *"彩蛋"* ]]; then
            echo -e "  ${BOLD}${YELLOW}${line}${RST}"
        elif [[ "$line" == *"超级"* ]] || [[ "$line" == *"欢迎"* ]] || [[ "$line" == *"=="* ]]; then
            echo -e "  ${CYAN}${line}${RST}"
        else
            echo -e "  ${WHITE}${line}${RST}"
        fi
        sleep 0.2
    done
    echo ""
    echo -e "  ${GREEN}--- 炸弹输出结束 ---${RST}"
else
    echo -e "  ${RED}未找到 ${BOMB} 或 ${ANSWER}，跳过实弹演示。${RST}"
fi

echo ""
press_enter

# ════════════════════════════════════════════════════════════
#  反编译版本
# ════════════════════════════════════════════════════════════

clear
echo ""
echo -ne "  ${CYAN}"
for ((i=0; i<61; i++)); do printf "═"; done
echo -e "${RST}"
echo -ne "  ${BOLD}${CYAN}"
title_t2="加分项 — 反编译 C++ 可运行版本"
for ((i=0; i<${#title_t2}; i++)); do
    printf "%s" "${title_t2:$i:1}"
    sleep 0.025
done
echo -e "${RST}"
echo -ne "  ${CYAN}"
for ((i=0; i<61; i++)); do printf "═"; done
echo -e "${RST}"
echo ""

sleep "$T_BLOCK"

pop_desc "不只是「抄答案」，而是把炸弹完整反编译成可编译的 C++ 代码。
通过阅读反汇编，理解每一关的算法逻辑，
用高级语言重新实现，编译后行为一致。
这证明了对每一关逻辑的完全理解。"
sep

if [ -f "$DECOMPILED" ] && [ -x "$DECOMPILED" ]; then
    echo -e "  ${DIM}编译: ${YELLOW}g++ -o bomb_decompiled bomb_decompiled.cpp -no-pie -z execstack${RST}"
    echo -e "  ${DIM}运行: ${YELLOW}${DECOMPILED} ${SID} < ${ANSWER}${RST}"
    echo ""
    "$DECOMPILED" "$SID" < "$ANSWER" 2>&1 | while IFS= read -r line; do
        if [[ "$line" == *"爆了"* ]]; then
            echo -e "  ${BOLD}${RED}${line}${RST}"
        elif [[ "$line" == *"通过"* ]]; then
            echo -e "  ${BOLD}${GREEN}${line}${RST}"
        elif [[ "$line" == *"不可能"* ]] || [[ "$line" == *"SOS"* ]]; then
            echo -e "  ${BOLD}${MAGENTA}${line}${RST}"
        else
            echo -e "  ${WHITE}${line}${RST}"
        fi
        sleep 0.12
    done
else
    pop_line "源码: ${CYAN}bomb_decompiled.cpp${RST}"
    pop_line "编译: ${YELLOW}g++ -o bomb_decompiled bomb_decompiled.cpp -no-pie -z execstack${RST}"
fi

echo ""
press_enter

# ════════════════════════════════════════════════════════════
#  总结
# ════════════════════════════════════════════════════════════

clear
echo ""
echo -ne "  ${CYAN}"
for ((i=0; i<61; i++)); do printf "═"; done
echo -e "${RST}"
echo -ne "  ${BOLD}${CYAN}"
title_t3="通关总结"
for ((i=0; i<${#title_t3}; i++)); do
    printf "%s" "${title_t3:$i:1}"
    sleep 0.04
done
echo -e "${RST}"
echo -ne "  ${CYAN}"
for ((i=0; i<61; i++)); do printf "═"; done
echo -e "${RST}"
echo ""

sleep "$T_BLOCK"

echo -e "  ${GREEN}Phase 1${RST}  ${BOLD}字符串比较${RST}"
echo -e "           → ${CYAN}x/s \$rsi${RST} 一步搞定"
sleep 0.4
echo -e "  ${GREEN}Phase 2${RST}  ${BOLD}随机子关卡分发${RST}"
echo -e "           → ${CYAN}rand_div${RST} 查表 + 约束分析"
sleep 0.4
echo -e "  ${GREEN}Phase 3${RST}  ${BOLD}switch-case 分支${RST}"
echo -e "           → ${CYAN}逐步断点${RST} 提取三个值"
sleep 0.4
echo -e "  ${GREEN}Phase 4${RST}  ${BOLD}阶乘 + 魔数除法${RST}"
echo -e "           → ${CYAN}反推算法${RST} 绕过跳转表陷阱"
sleep 0.4
echo -e "  ${GREEN}Phase 5${RST}  ${BOLD}Shellcode 注入${RST}"
echo -e "           → ${CYAN}手写机器码${RST} + XOR 修正 + 隐藏彩蛋"
echo ""

sleep 0.5
echo -ne "  ${BOLD}${YELLOW}"
msg="核心方法论: 设断点 → 运行 → 查 rand_div → 读反汇编 → 算答案"
for ((i=0; i<${#msg}; i++)); do
    printf "%s" "${msg:$i:1}"
    sleep 0.02
done
echo -e "${RST}"
echo ""
echo ""
echo -e "  ${DIM}感谢观看！${RST}"
echo ""
