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

## 最终验证

答案文件 `bomb_202402720028.txt` 内容（一行一个答案）:

```
yxYZRkQzJt
44 45 46 47 48 49
133 E 106
```

运行:

```bash
./bomb_linux 720028 < bomb_202402720028.txt
```

输出:

```
    超级二进制炸弹2024版，欢迎你！
    欢迎你！720028
请输入第1级的密码：牛刀小试~你已经通过了第1级考验！
请输入第2级的密码：不错不错~你已经通过了第2级考验！
请输入第3级的密码：今夜没加班？ 你已经通过了第3级考验！
```

---

## 通用方法论

对付这类随机分发的炸弹关卡，核心流程是:

1. **读汇编** — 在 `bomb_disasm.txt` 中找到对应 phase 的函数，理解整体逻辑
2. **找随机分发点** — 每个 phase 开头都有 `GenerateRandomNumber` + 跳转表，先确定进入哪个子函数
3. **读子函数** — 分析约束条件（比较、循环、switch）
4. **GDB 提取随机值** — 在 `GenerateRandomNumber` 返回后设断点，读 `rand_div` (0x408820)
5. **代入求解** — 把随机值代入约束，算出答案

> **注意**: 不同学号的随机种子不同，会产生不同的随机数序列和子关卡，你需要按上述流程跑自己的学号。
