# 💣 链接炸弹 (Link Bomb)

## 🎬 故事背景

> 2077 年，全球基础设施依赖一套名为 **Glibc** 的古老系统运行。某天，一位离职程序员在系统中留下了一批**未使用 `-fPIC` 编译的静态库**——它们在某些条件下会触发致命错误，像定时炸弹一样逐个引爆。
>
> 更糟的是，这批库没有源码，只有二进制的 `.a` 文件。系统随时可能崩溃。
>
> 你，就是被召回的 **二进制拆弹专家**。你的任务：在纯二进制层面分析这些文件，将它们安全地转为共享库，恢复系统运行。每通过一关，就拆除一颗炸弹。拆弹失败……💥

欢迎来到 **链接拆弹组**。

## 实验简介

你收到了一组神秘的静态库文件（`.a`），它们本应作为共享库（`.so`）使用。然而，由于编译时缺少 `-fPIC` 等种种原因，直接转换会触发各种错误——**就像定时炸弹一样**。你的任务就是化险为夷，在 **二进制代码级别** 上分析并修复这些文件，将它们成功转换为可运行的共享库。

本实验是《计算机系统》课程第 7 章（链接）的配套实验，帮助你深入理解：

- ELF 文件格式与重定位表的结构
- 各类重定位类型（`R_X86_64_PC32`、`R_X86_64_32`、`R_X86_64_64` 等）
- 为什么共享库需要位置无关代码（PIC）
- TEXTREL 的含义及其危害
- 如何使用 `objcopy`、`readelf`、`objdump` 等工具进行二进制分析

---

## 快速开始

```bash
# 第一步：运行初始化，输入你的6位学号
python3 init_bomb.py

# 或者直接指定学号：
python3 init_bomb.py 202401

# 第二步：开始拆解 Level 1
cd level1
ar -x bomb1.a          # 解包炸弹
gcc -shared -o libbomb.so lv1.o   # 尝试直接转换
python3 ../bomb_tester.py . libbomb.so    # 测试是否通过
```

---

## 你的工作目录结构

运行 `init_bomb.py` 后，你会得到以下目录：

```
你的工作目录/
├── bomb_tester.py          # 炸弹检测器——验证你的 .so 是否通过
├── tools/
│   └── patch_o.py          # ELF 文件修补工具（Level 4 & 5 使用，含 TODO）
├── level1/                 # 第 1 关
│   ├── bomb1.a             # 炸弹文件（静态库）
│   └── config.json         # 测试配置（含期望值）
├── level2/                 # 第 2 关
│   ├── bomb2.a
│   └── config.json
├── level3/                 # 第 3 关
│   ├── bomb3.a
│   └── config.json
├── level4/                 # 第 4 关
│   ├── bomb4.a
│   └── config.json
└── level5/                 # 第 5 关（Boss 关）
    ├── bomb5.a
    └── config.json
```

每个关卡目录下的 `config.json` 会告诉你本关的测试内容，包括：
- 函数名和参数
- 期望的返回值
- 需要满足的检查条件（如"无 TEXTREL"）

---

## 测试框架：炸弹检测器

每当你生成一个 `.so` 文件，可以用 `bomb_tester.py` 来检测是否通过：

```bash
# 在当前关卡目录中运行
python3 ../bomb_tester.py . libbomb.so
```

测试框架会执行 `config.json` 中定义的检查项。如果所有检查通过，你会看到：

```
🎉   LEVEL DEFUSED!   🎉
  + 10 points
```

如果任何一项检查失败，炸弹会爆炸 💥：

```
   💥     BOOOOOOOM!     💥
  The bomb exploded!
```

---

## 五关详解

### Level 1: 蒸汽时代 (Steam Age) — 10 分

**炸弹描述**

最简单的关卡。炸弹中的函数是纯算术运算，没有全局变量、没有静态数据、没有函数指针。所有重定位都是 `R_X86_64_PC32`（PC 相对寻址），在共享库中天然安全。

**拆弹方法**

```bash
ar -x bomb1.a              # 解包
readelf -r lv1.o           # 查看重定位（只有 PC-relative）
gcc -shared -o libbomb.so lv1.o   # 直接转换
python3 ../bomb_tester.py . libbomb.so    # 验证
```

**关键观察**

`readelf -r` 看到的只有 `.eh_frame` 节区的 PC-relative 重定位，`.text` 节区中没有重定位——纯函数不引用任何外部符号。

---

### Level 2: 全局危机 (Global Crisis) — 20 分

**炸弹描述**

炸弹中有代码访问全局变量并取其地址。未使用 `-fPIC` 编译时，取地址操作生成 **`R_X86_64_32`** 或 **`R_X86_64_32S`** 重定位——它们在指令中嵌入了一个 **32 位绝对地址**。

共享库可能被加载到 4GB 以上的地址空间，32 位绝对地址无法覆盖这个范围。因此链接器**拒绝**将这类 `.o` 链接为共享库。

**线索**

- 用 `readelf -r lv2.o` 查看重定位表。关注类型为 `R_X86_64_32` 或 `R_X86_64_32S` 的条目——它们嵌入了 32 位绝对地址
- 用 `objdump -d lv2.o` 反汇编，对照重定位表找到对应指令
- 链接器拒绝的原因是：共享库加载地址可能超过 4GB，32 位地址放不下
- **解决办法**：用 `-fPIC` 重新编译。PIC 代码会改用 `R_X86_64_GOTPCREL` 通过 GOT 间接访问，不再嵌入绝对地址
- 如何知道原函数的行为？`objdump -d` 可以看到每条指令——对照理解逻辑即可
- 全局变量的初始值在 `config.json` 的 `desc` 字段中有提示

**拆弹思路**

1. 解包炸弹，确认链接器拒绝
2. 用 `readelf -r` 定位问题重定位
3. 分析原函数逻辑（反汇编或看 test 期望值推理）
4. 新建 `.c` 文件，用 `-fPIC` 编译，实现相同行为
5. 重新链接为 `.so`，用 `bomb_tester.py` 验证

**思考题**

- 为什么 `get_elem` 访问 `arr[i]` 没有产生 `R_X86_64_32` 重定位？（提示：`arr` 是怎样定义的？）
- `R_X86_64_PC32` 和 `R_X86_64_32` 在指令编码上的根本区别是什么？

---

### Level 3: 数据陷阱 (Data Trap) — 20 分

**炸弹描述**

炸弹中的代码访问一个 **全局变量**。即使编译器使用了 PC 相对寻址（`R_X86_64_PC32`），链接器仍然拒绝将其链接为共享库。

问题在于 **符号介入（symbol interposition）**：共享库中的全局符号可能被其他同名符号覆盖，PC 相对引用无法应对这种情况。

**线索**

- 用 `readelf -s lv3.o` 查看符号表。注意符号的绑定（`GLOBAL` vs `LOCAL`）
- 链接器拒绝的原因是**符号介入（symbol interposition）**：共享库中的全局符号可能被其他同名符号覆盖。PC 相对引用无法处理这种情况
- 关键发现：如果是 `LOCAL` 符号（等价于源码中的 `static`），链接器就不会拒绝
- 有没有工具可以**在二进制层面**将 `GLOBAL` 符号改为 `LOCAL`，而不修改源码？
- 用 `readelf -r lv3.o` 确认：重定位类型是 `R_X86_64_PC32`——它本身是安全的，不安全的是**目标符号的可见性**

**拆弹思路**

1. 解包，确认链接器拒绝
2. 用 `readelf -s` 查看符号绑定，`readelf -r` 查看重定位类型和符号
3. 搜索 `objcopy` 的手册，找到能改变符号绑定的选项
4. 用该选项将问题符号从 `GLOBAL` 改为 `LOCAL`
5. 链接为 `.so`，用 `bomb_tester.py` 验证

> 💡 提示：`objcopy` 的完整选项列表可以通过 `objcopy --help` 查看

**思考题**

- 如果把 `counter` 声明为 `static`，这个问题还会出现吗？为什么？
- `objcopy` 除了改变符号绑定，还有哪些实用的 ELF 修改功能？

---

### Level 4: 二进制手术师 (Binary Surgeon) — 25 分

**炸弹描述**

炸弹中的代码定义了一个 `const` 指针指向静态变量。`const` 指针被放入了 **`.rodata`**（只读数据节区），但初始化需要在运行时确定地址——这就产生了位于**只读节区**的重定位。

链接为共享库时，动态链接器需要修改这个值，但 `.rodata` 是只读的，冲突产生了 **TEXTREL**（代码段重定位）。

**拆弹步骤**

```bash
# 第一步：解包，确认问题
ar -x bomb4.a
gcc -shared -o libbomb.so lv4.o    # 会有 TEXTREL 警告
readelf -d libbomb.so | grep TEXTREL

# 第二步：分析节区结构
readelf -S lv4.o                    # 查看节区标志
readelf -r lv4.o                    # 找到 .rodata 中的重定位

# 第三步：完成 tools/patch_o.py 中的 TODO
# 你需要实现：
#   1. read_u16 / read_u32 / read_u64 — 从 ELF 二进制读取数据
#   2. get_section_name — 从字符串表查找节区名
#   3. find_sections_with_textrel — 检测 TEXTREL 风险
#   4. fix_textrel — 对只读数据节区添加 SHF_WRITE 标志

# 第四步：用完成的修补工具修复
python3 ../tools/patch_o.py lv4.o lv4_fixed.o
gcc -shared -o libbomb.so lv4_fixed.o
python3 ../bomb_tester.py . libbomb.so
```

> ⚠️ 注意：不要给 `.text` 节区添加可写标志，这会造成 W+X 安全风险！

**思考题**

- 为什么 `.rodata` 中的重定位会导致 TEXTREL，而 `.data` 中的就不会？
- 从源码角度看，怎样改写才能避免这个问题？

---

### Level 5: 终极链接炸弹 (Ultimate Link Bomb) — 50 分

**炸弹描述**

Boss 关。`bomb5.a` 包含 **3 个 `.o` 文件**，每个有不同类型的问题。

**线索**

- 用 `ar -x bomb5.a` 解包，得到三个 `.o` 文件
- 对每个文件运行 `readelf -r`，判断它们各自的问题类型：
  - 重定位数量为 0 或只有 `.eh_frame` → ✅ 安全，可直接使用
  - 包含 `R_X86_64_PC32` 到全局符号 → 需要 Level 3 的技术
  - 包含 `R_X86_64_32/32S` → 需要 Level 2 的技术
  - `.rodata` 节区包含重定位 → 需要 Level 4 的技术
- 同一个文件可能同时有多个问题，需要组合之前学过的技术

**拆弹思路**

1. 解包，分析每个文件的重定位和符号表
2. 对安全文件——直接保留
3. 对有符号绑定问题的文件——用 `objcopy` 在二进制层面修复
4. 对有 TEXTREL 风险的文件——用 `patch_o.py` 修补节区标志
5. 对包含绝对地址的文件——重写为 PIC 代码重新编译
6. 将修复后的所有 `.o` 链接为 `.so`，用 `bomb_tester.py` 验证

---

## 常用工具速查

### readelf — 读取 ELF 信息

```bash
readelf -a file.o       # 全部信息
readelf -h file.o       # ELF 头部
readelf -S file.o       # 节区头部表
readelf -r file.o       # 重定位表（最重要的命令！）
readelf -s file.o       # 符号表
readelf -d lib.so       # 动态段（检查 TEXTREL）
```

### objdump — 反汇编

```bash
objdump -d file.o              # 反汇编
objdump -d -r file.o           # 反汇编 + 显示重定位
objdump -s -j .rodata file.o   # 显示节区原始字节
```

### objcopy — 修改 ELF 文件

```bash
objcopy --localize-symbol=<name> in.o out.o     # 全局→局部
objcopy --globalize-symbol=<name> in.o out.o    # 局部→全局
objcopy --strip-symbol=<name> in.o out.o        # 删除符号
```

### ar — 静态库操作

```bash
ar -t lib.a             # 列出包含的文件
ar -x lib.a             # 解包
```

### 链接共享库

```bash
gcc -shared -o lib.so *.o             # 基本链接
gcc -shared -fPIC -o lib.so *.o       # 所有 PIC
```

---

## 评分标准

| 关卡 | 名称 | 分数 | 最低通过要求 |
|------|------|------|-------------|
| 1 | 蒸汽时代 | 10 | `.so` 合法且函数正确 |
| 2 | 全局危机 | 20 | `.so` 合法、无 TEXTREL、函数正确 |
| 3 | 数据陷阱 | 20 | `.so` 合法、无 TEXTREL、函数正确 |
| 4 | 二进制手术师 | 25 | `.so` 合法、无 TEXTREL、`patch_o.py` 正确实现、函数正确 |
| 5 | 终极链接炸弹 | 50 | 综合前四关要求，全部函数正确 |
| **总分** | | **125** | |

---

## 常见问题

**Q: 我能不能直接修改源码然后重新编译？**

A: 可以的。虽然实验设计是让你在二进制层面工作，但最终目标是生成一个可工作的 `.so`。如果你能通过分析二进制理解问题所在，然后写出正确的 `-fPIC` 代码，同样达到了学习目的。

**Q: 为什么 Level 2 不能用 `gcc -shared -Wl,-z,notext` 强制通过？**

A: 现代 Linux 链接器（binutils 2.40+）已经拒绝 `R_X86_64_32` 类型的重定位用于共享库，即使指定 `-z notext` 也不行。这是出于安全考虑——32 位绝对地址在共享库加载到 4GB 以上时会产生溢出。

**Q: `objcopy --localize-symbol` 具体做了什么？**

A: 它将符号表中的 `STB_GLOBAL` 绑定改为 `STB_LOCAL`。链接器对局部符号和全局符号有不同的处理策略：局部符号不会被外部介入，因此 PC 相对引用是安全的。

**Q: Level 4 中，为什么修改节区标志就能消除 TEXTREL？**

A: 链接器的逻辑是：如果一个节区已分配（`SHF_ALLOC`）但不可写（无 `SHF_WRITE`），且其中包含重定位，就必须产生 TEXTREL。通过添加 `SHF_WRITE`，链接器认为该节区是"可写的"，从而将重定位交由动态链接器正常处理。

**Q: 我的 `patch_o.py` 无法通过测试怎么办？**

A: 按以下步骤调试：
1. 先用 `python3 tools/patch_o.py --info lv4.o` 检查你能否正确读取 ELF 头部
2. 用 `python3 tools/patch_o.py lv4.o lv4_fixed.o` 运行修补
3. 用 `readelf -S lv4_fixed.o` 检查 `.rodata` 的 flags 是否已包含 `W`
4. 如果节区标志没有变化，检查 `write_u64` 是否正确工作

**Q: 我不小心删除了 `lv1.o` 怎么办？**

A: 重新解包即可：`ar -x bomb1.a`。如果整个目录搞乱了，重新运行 `python3 init_bomb.py` 会重新生成所有炸弹。

---

## 提交要求

1. 每关提交最终生成的 `libbomb.so`
2. 提交你编写的所有修复代码（替代 `.c` 文件、完成的 `patch_o.py` 等）
3. 提交一份分析报告（`report.md`），包含：
   - 每关的 `readelf -r` 输出
   - 问题分析和修复思路
   - 回答对应关卡末尾的思考题

---

祝你拆弹顺利！🧨 → 🎉
