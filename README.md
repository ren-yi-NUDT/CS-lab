# Binary Bomb Lab - GDB 调试通关指南

## 前置知识

### x86-64 调用约定

| 参数顺序 | 寄存器   | 返回值 |
|----------|----------|--------|
| 第 1 个  | `%rdi`   | `%rax` |
| 第 2 个  | `%rsi`   |        |
| 第 3 个  | `%rdx`   |        |
| 第 4 个  | `%rcx`   |        |
| 第 5 个  | `%r8`    |        |
| 第 6 个  | `%r9`    |        |

### 栈帧布局

```
高地址
┌────────────┐
│  参数 7+   │
├────────────┤
│  返回地址   │  ← call 自动压入
├────────────┤
│  旧 %rbp   │  ← push %rbp
├────────────┤
│  局部变量   │  ← %rbp - N 访问
│  ...       │
└────────────┘  ← %rsp
低地址
```

### GDB 基础命令

```gdb
break *0x401b7d        # 在指定地址设断点
break phase_1           # 在函数入口设断点
run 720028 < input.txt  # 带参数和输入文件运行
continue                # 继续执行到下一个断点
x/s $rsi                # 以字符串形式查看 rsi 指向的内存
p/x *(long long*)0x408820  # 以十六进制查看全局变量 rand_div
p *(int*)($rbp-0xc)    # 查看栈上的局部变量
x/6d $rbp-0x20         # 以十进制查看栈上连续6个int
info registers          # 查看所有寄存器
disas phase_1           # 反汇编指定函数
```

---

## 炸弹总体结构

运行方式: `./bomb_linux <学号后6位>`，程序从 `bomb_<学号>.txt` 文件逐行读取答案。

**关键机制**: 随机种子由 `atoi(argv[1])` 决定，所以同一个学号每次运行产生的随机数序列是**固定的**。

核心全局变量:

| 地址      | 变量      | 含义                |
|-----------|-----------|---------------------|
| `0x408810`| `rand1_h` | 随机种子高半部      |
| `0x408818`| `rand1_l` | 随机种子低半部      |
| `0x408820`| `rand_div`| 最近一次 `rand1_h % n` 的结果 |

`GenerateRandomNumber(n)` 的效果: 更新种子，然后 `rand_div = rand1_h % n`。

---

## Phase 1: 字符串比较

### 分析思路

阅读 `bomb_disasm.txt` 中 `<phase_1>` 的反汇编:

```
401b53 <phase_1>:
  401b5f:  mov    %rdi,-0x18(%rbp)      # 保存 input
  401b63:  lea    -0xb(%rbp),%rax        # 局部缓冲区(11字节)
  401b6a:  call   GenerateRandomString   # 生成随机字符串到缓冲区
  401b7d:  call   strcmp@plt             # strcmp(input, 随机字符串)
  401b82:  test   %eax,%eax             # 返回值为0则相等
  401b84:  je     401b8b                 # 相等则跳过炸弹
  401b86:  call   explode_bomb
```

**结论**: `phase_1` 生成一个10字符随机字符串，与用户输入做 `strcmp`，不相等则爆炸。

### GDB 操作

在 `strcmp` 调用前设断点，此时 `%rdi` = 用户输入，`%rsi` = 随机字符串:

```gdb
$ gdb ./bomb_linux
(gdb) break *0x401b7d
(gdb) run 720028
```

随便输入一行文字（比如 `test`），断点命中后:

```gdb
(gdb) x/s $rsi
0x7fffffffd695: "yxYZRkQzJt"
```

这就是答案。把它写到答案文件的第一行。

---

## Phase 2: 六个整数 (随机子关卡)

### 分析思路

`phase_2` 先调用 `GenerateRandomNumber(16)`，根据 `rand_div` 的值(0~15)跳转到16个不同的子函数 `phase_2_0` ~ `phase_2_15`。

```
401b8e <phase_2>:
  401ba3:  call   GenerateRandomNumber   # 参数 0x10 = 16
  401bb3:  cmp    $0xf,%rax             # rand_div > 15?
  401bd7:  notrack jmp *%rax            # 跳转表分发到子函数
```

### 第一步: 确定子关卡

把 phase 1 的答案写入答案文件，再加一行占位（比如 `1 2 3 4 5 6`），然后在 `0x401bb3` 设断点:

```gdb
(gdb) break *0x401bb3
(gdb) run 720028 < bomb_202402720028.txt
```

断点命中后查看 `rand_div`:

```gdb
(gdb) p *(long long*)0x408820
$1 = 13
```

`rand_div = 13`，说明进入 `phase_2_13`。

### 第二步: 分析 phase_2_13 的约束

阅读 `<phase_2_13>` 的反汇编，逐步理解约束:

**约束 1 — 首元素** (0x4026a6 ~ 0x4026c3):
```
4026a6:  mov    $0x32,%edi              # GenerateRandomNumber(50)
4026ab:  call   GenerateRandomNumber
4026b0:  mov    -0x20(%rbp),%eax       # eax = a[0]
4026b5:  mov    rand_div,%rdx          # rdx = rand_div
4026bc:  add    $0x10,%rdx             # rdx = rand_div + 16
4026c0:  cmp    %rdx,%rax              # a[0] == rand_div + 16 ?
4026c3:  je     4026ca
4026c5:  call   explode_bomb
```
→ `a[0] == rand_div + 16`

**约束 2 — 非负** (0x4026ca ~ 0x4026ed):
```
循环 i=0..5: a[i] >= 0 (test + jns)
```

**约束 3 — 严格递增** (0x4026ef ~ 0x40272b):
```
循环 i=1..5:
  if a[i] < a[i-1]: explode   # jl 跳到爆炸
  else if a[i] <= 0: explode  # test + jg，不大于0则爆炸
```
→ `a[i] > a[i-1]` 且 `a[i] > 0`，即**严格递增的正整数**

### 第三步: 提取随机数并求解

在 `0x4026b0` 设断点（`GenerateRandomNumber(50)` 返回后）:

```gdb
(gdb) break *0x4026b0
(gdb) run 720028 < bomb_202402720028.txt
```

命中后:

```gdb
(gdb) p *(long long*)0x408820
$2 = 28
```

所以 `a[0] = 28 + 16 = 44`。需要严格递增的6个正整数，首元素为44:

```
44 45 46 47 48 49
```

---

## Phase 3: 整数+字符+整数 (随机子关卡)

### 分析思路

和 Phase 2 类似，`phase_3` 调用 `GenerateRandomNumber(14)`，根据结果分发到 `phase_3_0` ~ `phase_3_13`。

### 第一步: 确定子关卡

更新答案文件（加入 phase 2 的答案），加占位行，在 `0x4028b4` 设断点:

```gdb
(gdb) break *0x4028b4
(gdb) run 720028 < bomb_202402720028.txt
```

```gdb
(gdb) p *(long long*)0x408820
$3 = 13
```

`rand_div = 13`，进入 `phase_3_13`。

### 第二步: 分析 phase_3_13 的约束

阅读 `<phase_3_13>` 的反汇编:

**输入格式** (0x4041e0 ~ 0x404202):
```
4041e0:  lea    -0x10(%rbp),%rsi    # &val2 → 第5个参数 (r8)
4041e4:  lea    -0x11(%rbp),%rcx    # &char → 第4个参数 (rcx)
4041e8:  lea    -0xc(%rbp),%rdx     # &val1 → 第3个参数 (rdx)
4041f3:  lea    "%d %c %d",%rsi     # 格式串
404202:  call   sscanf
```
→ `sscanf(input, "%d %c %d", &val1, &char, &val2)`

**约束 1 — val1** (0x404215 ~ 0x404235):
```
404215:  mov    $0x8,%edi           # GenerateRandomNumber(8)
40421a:  call   GenerateRandomNumber
40421f:  mov    -0xc(%rbp),%eax     # val1
404224:  mov    rand_div,%rdx
40422b:  add    $0x82,%rdx          # rand_div + 130
404232:  cmp    %rdx,%rax           # val1 == rand_div + 130 ?
```
→ `val1 = rand_div(8) + 0x82`

**约束 2 — switch 计算 char 和 val2**:

根据 `(val1 - 0x82)` 做 switch (0~7)。每个 case:
1. 调若干次 `GenerateRandomNumber`（消耗随机数来推进序列）
2. 调 `GenerateRandomNumber(26)`，`char = rand_div + 0x41`（即 'A'~'Z'）
3. 调 `GenerateRandomNumber(300)`，`val2 == rand_div`

**约束 3 — 最终 char 比较**:
```
期望字符存在 -0x1(%rbp)，实际输入字符在 -0x11(%rbp)
如果两者相等则通过
```

### 第三步: 用 GDB 逐步提取

**步骤 A**: 先断点拿到 val1:

```gdb
(gdb) break *0x40421f
(gdb) run 720028 < bomb_202402720028.txt
(gdb) p *(long long*)0x408820
$4 = 3
```

`val1 = 3 + 130 = 133`，`switch_index = 133 - 130 = 3`。

**步骤 B**: 把 val1=133 写入输入，断点在 switch 后的 case 处提取 char 和 val2:

```gdb
(gdb) break *0x404383    # case 3 设置 char 的指令
(gdb) break *0x404390    # case 3 检查 val2 的指令
(gdb) continue
```

命中 `0x404383` 时:
```gdb
(gdb) printf "char = %c\n", $al
char = E
```

命中 `0x404390` 时:
```gdb
(gdb) p *(long long*)0x408820
$5 = 106
(gdb) p *(int*)($rbp-0x10)
$6 = 0           # 我们输入的 val2
```

所以 `val2 = 106`。

**答案**: `133 E 106`

---

## Phase 4: 阶乘校验 (随机子关卡)

### 分析思路

和 Phase 2/3 一样，`phase_4` 调用 `GenerateRandomNumber(20)`，根据 `rand_div` 通过跳转表分发到不同子函数。

**⚠️ 跳转表陷阱**: `rand_div` 的值并不直接对应子函数编号！比如 `rand_div=14` 跳转到的不是 `phase_4_14`，而是跳转表第 14 项指向的 `phase_4_24`。必须用 GDB 确认实际跳转目标。

### 第一步: 确定子关卡

更新答案文件（加一行占位），在跳转表的间接跳转指令处设断点:

```gdb
(gdb) break *0x404604
(gdb) run 720028 < bomb_720028.txt
```

断点命中后查看实际跳转目标:

```gdb
(gdb) x/i $rax
   0x4046ec <phase_4+305>:  mov    -0x8(%rbp),%rax
```

跳转到 `0x4046ec`，查看该地址的代码:

```
4046ec:  mov    -0x8(%rbp),%rax
4046f0:  mov    %rax,%rdi
4046f3:  call   40548f <phase_4_24>
```

所以实际进入的是 `phase_4_24`。

### 第二步: 分析 phase_4_24 的算法

阅读 `<phase_4_24>` 的反汇编:

**约束 1 — 输入范围** (0x4054ea ~ 0x40550e):
```
4054ea:  call   sscanf          # 格式 "%d"，读一个整数 x
4054f2:  cmp    $0x1,-0x4(%rbp) # sscanf 返回值 == 1?
4054f8:  mov    -0x34(%rbp),%eax
4054fb:  test   %eax,%eax       # x > 0?
4054fd:  jg     405504          # 是则跳过爆炸
405504:  mov    -0x34(%rbp),%eax
405507:  cmp    $0x1f3,%eax     # x > 499?
40550c:  jg     405513          # 是则跳过爆炸
40550e:  call   explode_bomb
```
→ `x > 0` 且 `x > 499`，即 **x ≥ 500**

**约束 2 — 除法取商** (0x405513 ~ 0x405530):
```
405519:  imul   $0x10624dd3,%rdx,%rdx  # 编译器优化：乘以魔数代替除法
405520:  shr    $0x20,%rdx
405524:  sar    $0x5,%edx
40552e:  sub    %ecx,%eax
405530:  mov    %eax,-0x34(%rbp)       # quotient = x / 500
```
→ 这段魔数除法等价于 `quotient = x / 500`

**约束 3 — 阶乘函数** (0x405538):
```
405538:  call   func4_2       # result = func4_2(quotient)
```

`func4_2` 的逻辑:
```
func4_2(n):
  if n <= 1: return 1
  else: return n * func4_2(n-1)
```
→ `func4_2(n) = n!`（阶乘）

**约束 4 — 查表比较** (0x405540 ~ 0x405558):
```
405540:  mov    $0x7,%edi
405545:  call   GenerateRandomNumber    # GenerateRandomNumber(7)
40554a:  mov    rand_div,%rax           # rand_div ∈ {0..6}
405551:  mov    -0x30(%rbp,%rax,4),%eax # table[rand_div]
405555:  cmp    %eax,-0x8(%rbp)         # func4_2(quotient) == table[rand_div]?
```

预置的查表值（从反汇编直接读出）:

| 索引 | 值 | 含义 |
|------|------|------|
| 0 | 0x18 = 24 | 4! |
| 1 | 0x78 = 120 | 5! |
| 2 | 0x2d0 = 720 | 6! |
| 3 | 0x13b0 = 5040 | 7! |
| 4 | 0x9d80 = 40320 | 8! |
| 5 | 0x58980 = 362880 | 9! |
| 6 | 0x375f00 = 3628800 | 10! |

### 第三步: GDB 提取并求解

**步骤 A**: 断点在 `GenerateRandomNumber(7)` 返回后 (0x40554a):

```gdb
(gdb) break *0x40554a
(gdb) run 720028 < bomb_720028.txt
(gdb) p *(long long*)0x408820
$1 = 5
```

`rand_div = 5`，查表 `table[5] = 362880 = 9!`

**步骤 B**: 反推输入:

```
需要: func4_2(quotient) = 9! = 362880
即:   quotient! = 9!
所以: quotient = 9
又:   quotient = x / 500
所以: x = 9 × 500 = 4500
验证: 4500 > 499 ✓
```

**答案**: `4500`

---

## Phase 5: 不可能任务 — 注入 Shellcode

### 核心原理: 为什么 txt 里写的十六进制会被当成代码执行？

Phase 1~4 都是"输入数据，程序拿你的数据做判断"。Phase 5 完全不同：**程序把你输入的十六进制字符串翻译成原始字节，然后直接跳过去让 CPU 执行**。

整个链条如下:

```
你的答案文件第 6 行（一行十六进制字符串）:
"bf0004000048b8ad1640..."
        │
        │  ① read_line() 从文件读入这一行
        ▼
        │  ② tohex() 把每两个十六进制字符翻译成一个字节
        │     "bf" → 0xbf,  "00" → 0x00,  "04" → 0x04, ...
        │     结果存入 buf[256]，buf 在栈上
        ▼
  buf = [0xbf, 0x00, 0x04, 0x00, 0x00, 0x48, 0xb8, ...]
        │
        │  ③ check_buf_valid(): 把 buf 的 256 字节逐个 XOR，
        │     结果必须等于 rand_div & 0xFF（一次校验）
        ▼
        │  ④ goto_buf_X(): 把 CPU 的指令指针直接跳到 buf 的地址
        │     CPU 从 buf[0] 开始，把每个字节当作指令来执行！
        ▼
  CPU 执行: 0xbf 0x00 0x04 0x00 0x00 → "mov edi, 0x400"
  CPU 执行: 0x48 0xb8 ...            → "movabs rax, ..."
  ...
```

**这就是"数据即代码"**——在 CPU 眼里，内存中的字节既可以被解读为数据，也可以被解读为指令，区别仅在于 CPU 是否被引导到那个地址去执行。

### 第一步: 进入 Phase 5

主函数在 Phase 4 之后会问你是否继续:

```gdb
(gdb) x/s 0x406358
"二进制炸弹之不可能任务，你的选择是继续前行（Y），或者放弃（N）："
```

输入 `Y`（不是 N/n）即进入 `phase_impossible`。所以答案文件第 5 行是 `Y`。

### 第二步: 输入约束

```
4017a5:  call   strlen          # 计算输入长度
4017aa:  cmp    $0x9,%rax       # 长度 > 9?
4017ae:  jbe    explode         # 不大于就炸
4017ba:  call   strlen
4017bf:  cmp    $0x300,%rax     # 长度 <= 768?
4017c5:  jbe    ok              # 不超过就通过
```
→ 十六进制字符串长度必须在 **10 ~ 768** 个字符之间（即 5 ~ 384 字节）

### 第三步: XOR 校验

程序在跳转执行 buf 之前，会先做一个校验。`check_buf_valid(buf, rand_div)` 的逻辑等价于:

```c
unsigned char xor_result = 0;
for (int i = 0; i < 256; i++) {
    xor_result ^= buf[i];  // 逐字节异或
}
return (xor_result == (rand_div & 0xFF));  // 必须等于 rand_div 的低 8 位
```

用 GDB 确定 `rand_div`:

```gdb
(gdb) break *0x401808    # GenerateRandomNumber(0x400) 返回后
(gdb) run 720028 < bomb_720028.txt
(gdb) p *(long long*)0x408820
$1 = 540
```

`540 & 0xFF = 28 = 0x1c`

→ **buf 的 256 字节 XOR 结果必须等于 0x1c**

这个校验的意义: 确保你不是随便输入一串十六进制，而是精心构造过整个 buf。后面我们会用一个小技巧来满足它。

### 第四步: goto_buf 跳转 — 数据变成代码

XOR 校验通过后，`GenerateRandomNumber(3)` 决定用哪种方式跳转到 buf:

| 函数 | 指令 | 效果 |
|------|------|------|
| `goto_buf_0` | `jmp *%rax` | 直接跳到 buf |
| `goto_buf_1` | `push %rax; ret` | 把 buf 地址压栈再 ret |
| `goto_buf_2` | `mov %rax,(%rsp); ret` | 把栈顶替换为 buf 地址再 ret |

三种方式效果相同: **CPU 的指令指针跳到 buf 的地址，开始从 buf[0] 逐字节当作 x86-64 指令执行**。

此时 buf 里放什么字节，CPU 就执行什么指令——这就是我们需要写 shellcode 的原因。

### 第五步: 构造 Shellcode

CPU 跳到 buf 后，需要完成以下任务:

1. 调用 `GenerateRandomNumber(0x400)` 获取 rand_div
2. 把 rand_div 存入全局变量 `result`（地址 `0x4087f8`）
3. **★ 触发隐藏彩蛋**: 调用 `phase_secret`（地址 `0x401a8b`）
4. 跳回主程序的比较逻辑（地址 `0x4018a0`），**跳过 explode_bomb**

对应的 x86-64 汇编和机器码逐行对照:

| 汇编指令 | 机器码 (hex) | 含义 |
|----------|-------------|------|
| `mov edi, 0x400` | `bf 00 04 00 00` | 第一个参数 = 0x400 |
| `movabs rax, 0x4016ad` | `48 b8 ad 16 40 00 00 00 00 00` | rax = GenerateRandomNumber 的地址 |
| `call rax` | `ff d0` | 调用 GenerateRandomNumber(0x400) |
| `movabs rax, 0x408820` | `48 b8 20 88 40 00 00 00 00 00` | rax = &rand_div |
| `mov rax, [rax]` | `48 8b 00` | rax = rand_div 的值 |
| `movabs rcx, 0x4087f8` | `48 b9 f8 87 40 00 00 00 00 00` | rcx = &result |
| `mov [rcx], eax` | `89 01` | result = rand_div |
| `mov edi, 0` | `bf 00 00 00 00` | 参数随意（phase_secret 不用） |
| `movabs rax, 0x401a8b` | `48 b8 8b 1a 40 00 00 00 00 00` | rax = phase_secret 的地址 |
| `call rax` | `ff d0` | ★ 调用 phase_secret（触发彩蛋） |
| `push 0x4018a0` | `68 a0 18 40 00` | 把目标地址压栈 |
| `ret` | `c3` | ret 弹出栈顶 → 跳到 0x4018a0 |

> **为什么用 push+ret 而不是 jmp?** `jmp` 的相对跳转只能到 ±2GB 范围，buf 在栈上（高地址），代码段在低地址，距离可能超限。`push 目标地址; ret` 是一种通用的绝对跳转技巧。

### 第六步: 拼 buf 并修正 XOR

Shellcode 只有 65 字节，buf 有 256 字节。填充方法:

1. buf[0..64] = shellcode（上面表格里的机器码）
2. buf[65] = 一个**修正字节**（使得整个 256 字节的 XOR = 0x1c）
3. buf[66..255] = 0x00（全零，不影响 XOR）

```python
import struct

# 构造 shellcode（把上表的机器码拼接）
shellcode  = b"\xbf\x00\x04\x00\x00"                              # mov edi, 0x400
shellcode += b"\x48\xb8" + struct.pack("<Q", 0x4016ad)             # movabs rax, GenerateRandomNumber
shellcode += b"\xff\xd0"                                            # call rax
shellcode += b"\x48\xb8" + struct.pack("<Q", 0x408820)             # movabs rax, &rand_div
shellcode += b"\x48\x8b\x00"                                        # mov rax, [rax]
shellcode += b"\x48\xb9" + struct.pack("<Q", 0x4087f8)             # movabs rcx, &result
shellcode += b"\x89\x01"                                            # mov [rcx], eax
shellcode += b"\xbf\x00\x00\x00\x00"                               # mov edi, 0
shellcode += b"\x48\xb8" + struct.pack("<Q", 0x401a8b)             # movabs rax, phase_secret
shellcode += b"\xff\xd0"                                            # call rax (★ 彩蛋)
shellcode += b"\x68" + struct.pack("<I", 0x4018a0)                 # push 0x4018a0
shellcode += b"\xc3"                                                # ret

# 放入 256 字节 buf
buf = bytearray(256)
buf[:len(shellcode)] = shellcode

# 计算 XOR，在空闲位置放一个修正字节
xor_val = 0
for b in buf:
    xor_val ^= b
target = 0x1c  # rand_div=540, 540 & 0xFF
buf[len(shellcode)] = xor_val ^ target  # 修正字节使整体 XOR = 0x1c

# 输出十六进制字符串 → 这就是答案文件第 6 行
print(buf.hex())
```

原理: 假设 shellcode 的 XOR 值为 `X`，全零部分 XOR 为 0，所以目前总 XOR = `X`。在 buf[65] 处放 `X ^ 0x1c`，那么总 XOR = `X ^ (X ^ 0x1c) = 0x1c`。一个字节就能修正。

### 第七步: 验证

```
请输入第4级的密码：完美了~你已经通过了第4级考验！
... 你的选择是继续前行（Y），或者放弃（N）：Y
友情提示：后面的代码，涉及到反调试、动态生成指令、执行超时检测等...
不可能任务，请输入突防指令：不可能的...不可能的...指令和数据的世界已经混乱...SOS...    ← ★ 隐藏彩蛋！
你已经通过了第5级考验，完成了不可能完成任务（终极考验）！
温馨提示：炸弹还有隐藏彩蛋哦...
```

---

## 最终验证

答案文件 `bomb_720028.txt` 内容（一行一个答案）:

```
yxYZRkQzJt
44 45 46 47 48 49
133 E 106
4500
Y
bf0004000048b8ad16400000000000ffd048b82088400000000000488b0048b9f8874000000000008901bf0000000048b88b1a400000000000ffd068a0184000c3fc00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000
```

运行:

```bash
./bomb_linux 720028 < bomb_720028.txt
```

输出:

```
    超级二进制炸弹2024版，欢迎你！
    欢迎你！720028
请输入第1级的密码：牛刀小试~你已经通过了第1级考验！
请输入第2级的密码：不错不错~你已经通过了第2级考验！
请输入第3级的密码：今夜没加班？ 你已经通过了第3级考验！
请输入第4级的密码：完美了~你已经通过了第4级考验！
... 你的选择是继续前行（Y），或者放弃（N）：不可能的...不可能的...指令和数据的世界已经混乱...SOS...
你已经通过了第5级考验，完成了不可能完成任务（终极考验）！
温馨提示：炸弹还有隐藏彩蛋哦...
```

---

## 隐藏彩蛋解析

程序中有一个 `phase_secret` 函数（地址 `0x401a8b`），功能很简单:

```c
void phase_secret(char *input) {
    puts("不可能的...不可能的...指令和数据的世界已经混乱...SOS...");
}
```

**关键发现**: 搜索整个反汇编文件，这个函数**从未被任何代码调用过**。它是程序作者故意留的一个"死代码"，只能通过以下方式触发:

1. 在反汇编中发现这个函数的存在（`grep "phase_secret" bomb_disasm.txt`）
2. 在 Phase 5 的 shellcode 中，通过 `call` 指令直接跳转到它的地址
3. 它执行完毕后正常返回，shellcode 继续执行后续逻辑

这个彩蛋的设计思路和真实安全场景中的**函数指针劫持**、**ROP 链**（Return-Oriented Programming）是同一个原理——通过控制程序跳转来执行原本不会被触发的代码。

---

## 通用方法论

对付这类随机分发的炸弹关卡，核心流程是:

1. **读汇编** — 在 `bomb_disasm.txt` 中找到对应 phase 的函数，理解整体逻辑
2. **找随机分发点** — 每个 phase 开头都有 `GenerateRandomNumber` + 跳转表，先确定进入哪个子函数
3. **读子函数** — 分析约束条件（比较、循环、switch）
4. **GDB 提取随机值** — 在 `GenerateRandomNumber` 返回后设断点，读 `rand_div` (0x408820)
5. **代入求解** — 把随机值代入约束，算出答案

Phase 5 的特殊之处:

- 需要编写 **shellcode**（x86-64 机器码），理解调用约定和指令编码
- 理解 **XOR 校验**：通过调整空闲字节使整个 buf 的异或值等于目标
- 理解 **控制流劫持**：`goto_buf_X` 把 buf 地址作为跳转目标，CPU 从 buf 开始执行你的代码
- 用 **push + ret** 实现绝对地址跳转

寻找隐藏彩蛋的方法:

- 用 `grep` 搜索反汇编中的函数名，找出**定义了但未被调用**的函数
- 在 shellcode 中加入 `call` 指令跳转到该函数地址
- 验证彩蛋触发后 shellcode 的后续逻辑仍然正常（比较、超时检测等）

> **注意**: 不同学号的随机种子不同，会产生不同的随机数序列和子关卡，你需要按上述流程跑自己的学号。Phase 5 的 shellcode 结构通用，但 XOR 校验目标值因学号而异。
