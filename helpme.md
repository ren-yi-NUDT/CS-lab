# 链接炸弹实验 — 完全教学指南

本文档从零开始，手把手教你理解实验涉及的全部背景知识，然后逐关拆解。
claude --resume dd3dd92a-3a6c-4f30-a3be-db010ebafd5f

---

## 第一部分：前置知识

### 1.1 什么是"链接"

你写的 C 代码经过编译器 (`gcc -c`) 变成 **目标文件 (`.o`)**。但 `.o` 里面的函数调用和变量引用还是"空洞"——比如调用了 `printf`，编译器不知道 `printf` 在内存中的实际地址，只是留了个占位符。

**链接器 (`ld`)** 的工作就是：把多个 `.o` 拼在一起，把这些"空洞"填上真正的地址。

这个过程叫 **重定位 (relocation)**。

### 1.2 静态库 `.a` vs 共享库 `.so`

| | 静态库 `.a` | 共享库 `.so` |
|---|---|---|
| 本质 | 多个 `.o` 的打包（类似 tar） | 已完成链接的可执行代码 |
| 加载时机 | 链接时复制进可执行文件 | 运行时由动态链接器加载 |
| 内存地址 | 链接时确定 | **运行时才知道**加载到哪 |

关键区别：**共享库的加载地址在编译时完全不知道**。系统可能把它加载到 `0x7f0000000000`，也可能加载到 `0x7f5000000000`，取决于运行时内存布局。

这就是问题的根源——如果 `.o` 里的代码用了**绝对地址**，链接成 `.so` 后运行时地址对不上，就炸了。

### 1.3 ELF 文件格式

Linux 上的可执行文件、`.o` 文件、`.so` 文件都遵循 **ELF** (Executable and Linkable Format) 格式。

一个 ELF `.o` 文件的内部结构（简化）：

```
┌──────────────────────────┐
│       ELF Header         │  ← 文件头，标识这是 ELF、64位/32位、节区表在哪
├──────────────────────────┤
│       .text              │  ← 机器代码（函数的指令）
├──────────────────────────┤
│       .data              │  ← 已初始化的全局/静态变量（可读写）
├──────────────────────────┤
│       .rodata            │  ← 只读数据（const 变量、字符串常量）
├──────────────────────────┤
│       .bss               │  ← 未初始化的全局/静态变量
├──────────────────────────┤
│       .rela.text         │  ← .text 节区的重定位表
├──────────────────────────┤
│       .rela.data         │  ← .data 节区的重定位表
├──────────────────────────┤
│       .symtab            │  ← 符号表（函数名、变量名）
├──────────────────────────┤
│       .shstrtab          │  ← 节区名字符串表
├──────────────────────────┤
│    Section Header Table  │  ← 节区头部表（每个节区的元信息）
└──────────────────────────┘
```

用 `readelf -S file.o` 就能看到所有节区和它们的属性。

### 1.4 重定位类型（最核心的知识）

**重定位表** (`.rela.text`, `.rela.data` 等) 记录了"哪些位置需要填地址、怎么填"。

x86-64 上最关键的几种重定位类型：

#### `R_X86_64_PC32` — PC 相对寻址（安全）

```
指令: mov 0x0(%rip), %eax    # 从 "当前指令位置 + 偏移" 读数据
```

编码方式：目标地址 = **当前指令的地址** + 一个 32 位偏移量。

这是"相对地址"——不需要知道变量的绝对地址在哪里，只需要知道"它在我前方多少字节"。**链接器对这种方式非常满意**，因为不管 `.so` 被加载到哪里，相对距离不变。

#### `R_X86_64_32` / `R_X86_64_32S` — 绝对地址（危险）

```
指令: mov $0x0, %eax         # 把一个 32 位立即数放进寄存器
```

编码方式：指令里直接嵌入变量的 **32 位绝对地址**。

问题：共享库可能被加载到 4GB 以上的地址（64 位系统的地址空间非常大）。一个 32 位值**放不下** 64 位地址。所以链接器**直接拒绝**把含这种重定位的 `.o` 链接成 `.so`。

#### `R_X86_64_64` — 64 位绝对地址（安全但有坑）

编码方式：指令里嵌入一个完整的 **64 位地址**。能放得下，所以链接器不会拒绝。但如果这个地址位于**只读节区**（`.rodata`），就产生了 TEXTREL 问题（见下文）。

### 1.5 什么是 PIC（位置无关代码）

**PIC = Position Independent Code**，位置无关代码。

编译时加 `-fPIC` 标志 (`gcc -c -fPIC`)，编译器会生成"不依赖绝对地址"的代码：

| 不加 `-fPIC` | 加了 `-fPIC` |
|---|---|
| `mov $addr, %rax` (绝对地址) | 通过 GOT 表间接访问 |
| `R_X86_64_32` 重定位 | `R_X86_64_GOTPCREL` 重定位 |
| 不能做 `.so` | 可以做 `.so` |

**GOT** (Global Offset Table) 是一小段数据，存放变量/函数的真实地址。PIC 代码不直接访问变量，而是先找到 GOT（相对位置），再从 GOT 里读取变量的真实地址。这多了一次间接跳转，但换来了完全的位置无关性。

### 1.6 什么是 TEXTREL

**TEXTREL = Text Relocation**，意思是"代码段（或只读数据段）里有需要在运行时修改的重定位"。

为什么这是问题？

1. 操作系统把 `.so` 加载到内存后，代码段 (`.text`) 和只读数据段 (`.rodata`) 被映射为**只读页面**
2. 如果动态链接器需要修改这些页面中的地址（因为重定位），但它**写不了**——页面是只读的
3. 要么修改页面权限（安全风险），要么报错

检测方法：
```bash
gcc -shared -o lib.so xxx.o           # 链接时会有警告
readelf -d lib.so | grep TEXTREL      # 如果输出有 TEXTREL 字样，就说明有问题
```

### 1.7 符号可见性：GLOBAL vs LOCAL

在 ELF 符号表中，每个符号（函数名、变量名）都有一个"绑定"属性：

| 绑定 | 含义 | 源码对应 |
|---|---|---|
| `STB_GLOBAL` | 全局可见，可被其他 `.o` 引用 | 普通的全局变量/函数 |
| `STB_LOCAL` | 只在本 `.o` 内可见 | `static` 修饰的变量/函数 |

为什么这很重要？

当链接器看到 `R_X86_64_PC32` 引用到一个 **GLOBAL** 符号时，它会觉得不安全——因为共享库中的全局符号可能被**符号介入**（symbol interposition）覆盖（另一个 `.so` 可能定义了同名符号）。如果符号被覆盖，PC 相对偏移就错了。

但如果符号是 **LOCAL** 的，链接器知道它不可能被覆盖，PC 相对引用就是安全的，允许链接。

用 `readelf -s file.o` 可以查看符号绑定。

---

## 第二部分：工具速查

### 2.1 readelf — 读取 ELF 信息

```bash
readelf -h file.o       # ELF 头部（是32位还是64位、节区表在哪）
readelf -S file.o       # 节区头部表（每个节区的名字、偏移、大小、标志）
readelf -r file.o       # 重定位表（最重要的命令！看有哪些重定位、什么类型）
readelf -s file.o       # 符号表（哪些函数、变量，是GLOBAL还是LOCAL）
readelf -d lib.so       # 动态段（检查 .so 是否有 TEXTREL）
readelf -a file.o       # 以上所有信息一把梭
```

### 2.2 objdump — 反汇编

```bash
objdump -d file.o             # 反汇编 .text 节区，看机器指令
objdump -d -r file.o          # 反汇编 + 在指令旁标注重定位
objdump -s -j .rodata file.o  # 显示 .rodata 节区的原始字节
```

### 2.3 objcopy — 修改 ELF 二进制

```bash
objcopy --localize-symbol=counter in.o out.o    # 把符号 counter 从 GLOBAL 改为 LOCAL
objcopy --globalize-symbol=foo in.o out.o       # 把符号 foo 从 LOCAL 改为 GLOBAL
objcopy --strip-symbol=bar in.o out.o           # 删除符号 bar
```

### 2.4 ar — 静态库操作

```bash
ar -t bomb1.a       # 列出静态库里包含哪些 .o 文件
ar -x bomb1.a       # 解包：把里面的 .o 文件释放到当前目录
```

### 2.5 gcc — 编译与链接

```bash
gcc -c -O1 source.c -o output.o          # 编译为 .o（不加 -fPIC）
gcc -c -O1 -fPIC source.c -o output.o    # 编译为 PIC 的 .o
gcc -shared -o lib.so *.o                # 把 .o 链接为共享库
```

---

## 第三部分：逐关拆解

### Level 1 — 蒸汽时代 (10 分)

#### 问题

最简单的关卡。炸弹里是纯算术函数（`add`, `sub`, `mul`, `mod`），没有全局变量，没有静态数据。

#### 分析过程

```bash
cd level1
ar -x bomb1.a              # 解包，得到 lv1.o
readelf -r lv1.o           # 查看重定位表
```

你会看到输出类似：

```
Relocation section '.rela.eh_frame' ...
  ... R_X86_64_PC32 ...
```

**关键观察**：只有 `.rela.eh_frame` 中的 `R_X86_64_PC32` 重定位——这是异常处理框架的重定位，链接器会自动处理。`.text` 节区**没有任何重定位**，说明代码不引用任何外部变量。

纯算术函数只用寄存器和栈，天生就是位置无关的。

#### 解决方案

```bash
gcc -shared -o libbomb.so lv1.o
python3 ../bomb_tester.py . libbomb.so
```

直接链接即可，没有任何障碍。

#### 检查项

config.json 要求两项：
- `elf_valid`：文件是合法的 ELF 共享库
- `func_test`：`add(17,8)=25`, `sub(17,2)=15`, `mul(20,4)=80`, `mod(13,10)=3`

---

### Level 2 — 全局危机 (20 分)

#### 问题

炸弹中有代码访问全局变量 `gvar` 并取其地址 (`&gvar`)。取地址操作生成了 `R_X86_64_32` / `R_X86_64_32S` 重定位。

#### 分析过程

```bash
cd level2
ar -x bomb2.a
readelf -r lv2.o
```

输出：
```
Relocation section '.rela.text' at offset 0x248 contains 4 entries:
  Offset     Info        Type            Sym. Value  Sym. Name + Addend
  0x9        ...         R_X86_64_32S    0x0         gvar + 0
  0xe        ...         R_X86_64_32     0x0         gvar + 0
  0x19       ...         R_X86_64_PC32   0x0         gvar - 4
  0x28       ...         R_X86_64_32S    0x0         .rodata + 0
```

看 `.rela.text`：有两处 `R_X86_64_32S` 和一处 `R_X86_64_32`——这些都是**绝对地址**重定位。

试一下直接链接：
```bash
gcc -shared -o libbomb.so lv2.o
```

你会看到链接器报错（或者在某些版本只是警告），因为这些 32 位绝对地址在共享库中不安全。

#### 反汇编确认

```bash
objdump -d lv2.o
```

```
get_addr:
   ...
   movq   $0x0,-0x8(%rsp)     # ← 这里嵌入了 gvar 的绝对地址（R_X86_64_32S）
   ...

read_var:
   ...
   mov    0x0(%rip),%eax      # ← 这里用 PC 相对（R_X86_64_PC32），安全
   ...

get_elem:
   ...
   mov    0x0(,%rdi,4),%eax   # ← 用 .rodata 的绝对地址（R_X86_64_32S）
```

`get_addr` 函数取 `gvar` 的地址并返回，编译器用 `movq $addr` 直接把地址塞进指令——这就是 `R_X86_64_32S` 的来源。

#### 解决方案

用 `-fPIC` 重写等价代码。通过反汇编和 config.json 分析原函数行为：

| 函数 | 行为 |
|---|---|
| `get_addr()` | 返回 `gvar` 的地址（转为 int） |
| `read_var()` | 返回 `gvar` 的值 |
| `get_elem(i)` | 返回静态数组 `arr[i]`，其中 `arr = {10,20,30,40,50}` |

从 config.json 得知 `gvar = 108`（看 `desc` 字段和 `read_var` 的 `expected` 值）。
长度
写一个新文件 `lv2_pic.c`：

```c
int gvar = 108;
int get_addr(void) { return (int)(unsigned long)&gvar; }
int read_var(void) { return gvar; }
static int arr[] = {10, 20, 30, 40, 50};
int get_elem(int i) { return arr[i]; }
```

编译、链接、测试：

```bash
gcc -c -fPIC -O1 -o lv2_pic.o lv2_pic.c
gcc -shared -o libbomb.so lv2_pic.o
python3 ../bomb_tester.py . libbomb.so
```

**为什么 `-fPIC` 就能解决？** 因为 PIC 代码访问全局变量时，不把绝对地址嵌入指令，而是通过 GOT（全局偏移表）间接访问。重定位类型变为 `R_X86_64_GOTPCREL`，链接器可以安全处理。

#### 思考题

- 为什么 `get_elem` 访问 `arr[i]` 没有产生 `R_X86_64_32`？
  → 因为 `arr` 是 `static` 的，编译器知道它不会被外部引用。编译器把它放在 `.rodata` 中，用段相对地址访问，生成的是 `R_X86_64_32S` 到 `.rodata` 段本身（而非符号）。实际上在这个案例中 `.rodata` 的引用也产生了 `R_X86_64_32S`，所以也需要 PIC 重写。

- `R_X86_64_PC32` 和 `R_X86_64_32` 在编码上的区别？
  → `PC32` 编码的是"相对于下一条指令的 32 位偏移"，`32/32S` 编码的是"32 位绝对地址"。前者是相对的（不需要知道加载地址），后者是绝对的（必须知道加载地址）。

---

### Level 3 — 数据陷阱 (20 分)

#### 问题

代码访问一个全局变量 `counter`。重定位类型是 `R_X86_64_PC32`（本身是安全的），但链接器**仍然拒绝**。

原因：`counter` 是 **GLOBAL** 符号，存在符号介入的风险。

#### 分析过程

```bash
cd level3
ar -x bomb3.a
readelf -r lv3.o
```

输出：
```
Relocation section '.rela.text' contains 2 entries:
  ... R_X86_64_PC32  ... counter - 4
  ... R_X86_64_PC32  ... counter - 4
```

重定位类型是 `R_X86_64_PC32`——这不是绝对地址问题。

看符号表：
```bash
readelf -s lv3.o
```

```
   3: ... FUNC    GLOBAL DEFAULT  1 next_val
   4: ... OBJECT  GLOBAL DEFAULT  3 counter        ← GLOBAL 绑定！
```

`counter` 是 **GLOBAL** 符号。链接器的逻辑是：
- 共享库中的全局符号可能被另一个 `.so` 的同名符号覆盖（符号介入）
- 如果 `counter` 被覆盖了，原来用 `R_X86_64_PC32` 计算的相对偏移就指向了错误的地址
- 所以链接器拒绝这种组合：`R_X86_64_PC32` + GLOBAL 符号

试一下直接链接：
```bash
gcc -shared -o libbomb.so lv3.o
```

你会看到链接器报错，说不能在共享库中使用 `R_X86_64_PC32` 引用全局符号。

#### 解决方案

用 `objcopy` 把 `counter` 的绑定从 GLOBAL 改为 LOCAL：

```bash
objcopy --localize-symbol=counter lv3.o lv3_fixed.o
```

验证修改：
```bash
readelf -s lv3_fixed.o | grep counter
```

你会看到 `counter` 的绑定变成了 `LOCAL`。

然后链接：
```bash
gcc -shared -o libbomb.so lv3_fixed.o
python3 ../bomb_tester.py . libbomb.so
```

**为什么改成 LOCAL 就可以了？** 因为 LOCAL 符号不可能被外部覆盖，所以 PC 相对引用永远指向正确的位置，链接器认为安全。

**为什么不能直接重写代码？** 可以，但 `objcopy` 是更"二进制层面"的做法——不改源码，直接修改 ELF 符号表中的一个字节。

#### 思考题

- 如果把 `counter` 声明为 `static`，这个问题还会出现吗？
  → 不会。`static` 修饰的全局变量在编译时就绑定为 `LOCAL`，链接器不会拒绝。

- `objcopy` 还有哪些实用功能？
  → `--strip-symbol`（删除符号）、`--globalize-symbol`（LOCAL→GLOBAL）、`--set-section-flags`（修改节区属性）、`--remove-section`（删除整个节区）等。

---

### Level 4 — 二进制手术师 (25 分)

#### 问题

代码定义了一个 `const` 指针指向静态变量：`int * const ptr = &base_val;`。

这个 `const` 指针的值（即 `base_val` 的地址）需要在链接时才能确定，所以它会产生一个**重定位**。但 `const` 指针被放在了 `.rodata`（只读数据节区），于是问题来了：

> 一个重定位在只读节区里 → 动态链接器需要修改它 → 但页面是只读的 → TEXTREL

#### 分析过程

```bash
cd level4
ar -x bomb4.a

# 先看节区结构
readelf -S lv4.o
```

关键输出：
```
  [3] .data      PROGBITS  ... WA ...      ← 可读写
  [5] .rodata    PROGBITS  ...  A ...      ← 只读！只有 A（ALLOC）标志，没有 W（WRITE）
  [6] .rela.rodata RELA    ... I  ... 5    ← .rodata 的重定位表，Info=5 指向 .rodata
```

`.rodata` 的 flags 是 `A`（ALLOC），没有 `W`（WRITE）。而 `.rela.rodata` 有一条重定位记录。

看重定位：
```bash
readelf -r lv4.o
```

```
Relocation section '.rela.rodata' contains 1 entry:
  ... R_X86_64_64 ... .data + 0    ← .rodata 中有一个指向 .data 的 64 位地址重定位
```

试着链接：
```bash
gcc -shared -o libbomb.so lv4.o
```

警告：`relocation in read-only section '.rodata'`、`creating DT_TEXTREL in a shared object`

验证 TEXTREL：
```bash
readelf -d libbomb.so | grep TEXTREL
```

输出有 `TEXTREL` 字样——测试不通过。

#### 解决方案

**核心思路**：把 `.rodata` 的节区标志加上 `SHF_WRITE`（可写），让链接器认为它是可写节区，就不会产生 TEXTREL 了。

实验提供了 `tools/patch_o.py`，但有 TODO 需要完成。你需要实现以下函数：

##### 1. `read_u16` / `read_u32` / `read_u64` — 读取 ELF 二进制数据

```python
import struct

def read_u16(data, offset):
    return struct.unpack_from('<H', data, offset)[0]

def read_u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]

def read_u64(data, offset):
    return struct.unpack_from('<Q', data, offset)[0]
```

`struct.unpack_from` 从 `data` 的 `offset` 位置读取数据。`<` 表示小端序（x86 的字节序），`H`=uint16, `I`=uint32, `Q`=uint64。

##### 2. `write_u64` — 写入数据

```python
def write_u64(data, offset, val):
    struct.pack_into('<Q', data, offset, val)
    return data
```

注意 `data` 必须是 `bytearray`（可修改的），不能是 `bytes`。

##### 3. `get_section_name` — 从字符串表查找节区名

ELF 文件的所有节区名存在 `.shstrtab` 节区中，是一个连续的字符串池，用 `\0` 分隔。每个节区头部的 `sh_name` 字段是**节区名在字符串池中的偏移**。

```python
def get_section_name(data, shdr_offset, shdr_entsize, shstrndx, sh_name_off):
    # 第一步：找到 .shstrtab 节区头部
    shstrtab_hdr_off = shdr_offset + shstrndx * shdr_entsize

    # 第二步：从节区头部读出 .shstrtab 在文件中的偏移和大小
    shstrtab_offset = read_u64(data, shstrtab_hdr_off + 24)   # sh_offset
    shstrtab_size = read_u64(data, shstrtab_hdr_off + 32)      # sh_size

    # 第三步：在字符串池中，从 sh_name_off 开始读到下一个 \0
    start = shstrtab_offset + sh_name_off
    end = data.index(b'\x00', start)
    return data[start:end].decode('ascii')
```

图示：
```
.shstrtab 内容:  \0  .text\0  .data\0  .rodata\0  ...
                  ↑   ↑        ↑         ↑
                 0   1        7        13

如果 sh_name_off = 7，节区名就是 ".data"
```

##### 4. `find_sections_with_textrel` — 检测 TEXTREL 风险

逻辑：

```
遍历所有节区
  ├─ 找到类型为 SHT_RELA 的节区（重定位表）
  │   └─ 通过 sh_info 字段找到"被重定位的目标节区"
  │       └─ 检查目标节区的标志：
  │           ├─ 有 SHF_ALLOC（运行时分配内存）？→ 是
  │           ├─ 没有 SHF_WRITE（只读）？→ 是
  │           ├─ 没有 SHF_EXECINSTR（不是代码段）？→ 是
  │           └─ → 这个节区有 TEXTREL 风险！
  └─ 其他类型的节区 → 跳过
```

需要从 ELF 头部读取的值：

| 字段 | 偏移 | 大小 | 含义 |
|------|------|------|------|
| `e_shoff` | 40 | 8 字节 | 节区头部表在文件中的起始偏移 |
| `e_shentsize` | 58 | 2 字节 | 每个节区头部的字节数（64 位 ELF 固定为 64） |
| `e_shnum` | 60 | 2 字节 | 节区数量 |
| `e_shstrndx` | 62 | 2 字节 | `.shstrtab` 节区的索引 |

每个节区头部 (Elf64_Shdr) 中的关键字段：

| 字段 | 节区头部内偏移 | 大小 | 含义 |
|------|---------------|------|------|
| `sh_name` | 0 | 4 字节 | 节区名在 .shstrtab 中的偏移 |
| `sh_type` | 4 | 4 字节 | 节区类型（4 = SHT_RELA 表示重定位表） |
| `sh_flags` | 8 | 8 字节 | 标志（SHF_WRITE=1, SHF_ALLOC=2, SHF_EXECINSTR=4） |
| `sh_info` | 44 | 4 字节 | 对于 RELA 节区，指向被重定位的目标节区索引 |

核心代码：
```python
# 从 ELF header 读取节区头部表信息
shdr_offset = read_u64(data, 40)
shdr_entsize = read_u16(data, 58)
shdr_count = read_u16(data, 60)
shstrndx = read_u16(data, 62)

for i in range(shdr_count):
    hdr_off = shdr_offset + i * shdr_entsize

    sh_type = read_u32(data, hdr_off + 4)
    if sh_type != SHT_RELA:
        continue

    # sh_info 告诉我们这个重定位表作用于哪个节区
    target_idx = read_u32(data, hdr_off + 44)

    # 读取目标节区的信息
    target_hdr = shdr_offset + target_idx * shdr_entsize
    tgt_name_off = read_u32(data, target_hdr)
    tgt_name = get_section_name(data, shdr_offset, shdr_entsize, shstrndx, tgt_name_off)
    tgt_flags = read_u64(data, target_hdr + 8)

    # 判断：已分配 + 只读 + 非可执行 → TEXTREL 风险
    is_readonly_alloc = (tgt_flags & SHF_ALLOC) and not (tgt_flags & SHF_WRITE)
    is_text = bool(tgt_flags & SHF_EXECINSTR)

    if is_readonly_alloc and not is_text:
        results.append((target_idx, tgt_name, tgt_flags))
```

##### 5. `fix_textrel` — 应用修复

```python
# 对每个有问题的节区，把 sh_flags 加上 SHF_WRITE
for idx, name, flags in problems:
    hdr_off = shdr_offset + idx * shdr_entsize
    new_flags = flags | SHF_WRITE
    write_u64(data, hdr_off + 8, new_flags)   # sh_flags 在节区头部偏移 8
```

#### 应用修复

```bash
python3 ../tools/patch_o.py lv4.o lv4_fixed.o
gcc -shared -o libbomb.so lv4_fixed.o
python3 ../bomb_tester.py . libbomb.so
```

#### 思考题

- 为什么 `.rodata` 中的重定位会导致 TEXTREL，而 `.data` 中的就不会？
  → 因为 `.rodata` 在内存中被映射为只读页面。动态链接器需要修改重定位目标位置的值，但只读页面不让写。`.data` 本身就可写，修改没有问题。

- 从源码角度怎样避免？
  → 把 `int * const ptr = &base_val;` 改为 `int *ptr = &base_val;`（去掉 `const`），编译器就会把它放进 `.data` 而非 `.rodata`。或者加 `-fPIC` 编译。

---

### Level 5 — 终极链接炸弹 (50 分)

#### 问题

Boss 关。`bomb5.a` 包含 **3 个 `.o` 文件**，每个有不同类型的问题。需要综合运用前四关的所有技术。

#### 分析过程

```bash
cd level5
ar -x bomb5.a
ar -t bomb5.a             # 列出: boss_helper.o, boss_state.o, boss_tricky.o
```

**逐个分析**：

##### boss_helper.o

```bash
readelf -r boss_helper.o
```

只有 `.rela.eh_frame` 中的 `R_X86_64_PC32`——安全！这个文件可以直接使用。

##### boss_state.o

```bash
readelf -r boss_state.o
```

输出：
```
Relocation section '.rela.text' contains 5 entries:
  ... R_X86_64_PC32 ... counter - 4     ← PC相对引用 counter（全局符号）
  ... R_X86_64_PC32 ... counter - 4
  ... R_X86_64_PC32 ... .data + 0       ← PC相对引用段内数据，安全
  ... R_X86_64_PC32 ... .data - 4
  ... R_X86_64_PC32 ... .data + 4

Relocation section '.rela.rodata' contains 1 entry:
  ... R_X86_64_64 ... .data + 0         ← .rodata 中的重定位 → TEXTREL 风险！
```

两个问题：
1. `counter` 是全局符号 → 需要用 Level 3 的 `objcopy --localize-symbol` 技术
2. `.rodata` 中有重定位 → 需要用 Level 4 的 `patch_o.py` 技术

但要注意：`gvar` **不能** localize，因为 `boss_tricky.o` 需要跨文件访问它！

```bash
readelf -s boss_state.o | grep gvar
#   9: ... OBJECT  GLOBAL DEFAULT  3 gvar    ← gvar 保持 GLOBAL，因为 boss_tricky 要用
```

##### boss_tricky.o

```bash
readelf -r boss_tricky.o
```

输出：
```
Relocation section '.rela.text' contains 3 entries:
  ... R_X86_64_32S ... gvar + 0     ← 绝对地址！
  ... R_X86_64_32  ... gvar + 0     ← 绝对地址！
  ... R_X86_64_PC32 ... gvar - 4    ← PC相对
```

`R_X86_64_32S` 和 `R_X86_64_32`——绝对地址重定位，和 Level 2 一样的问题。需要用 `-fPIC` 重写。

但注意：`gvar` 是 `extern` 声明的，它的定义在 `boss_state.o` 中。重写时要保留 `extern` 声明。

```bash
readelf -s boss_tricky.o
#   4: ... NOTYPE  GLOBAL DEFAULT  UND gvar    ← 未定义，需要从其他 .o 链接
```

#### 修复步骤

**第 1 步：boss_helper.o — 直接保留，不需要修改**

**第 2 步：boss_state.o — localize counter + patch TEXTREL**

```bash
# 把 counter 从 GLOBAL 改为 LOCAL
objcopy --localize-symbol=counter boss_state.o boss_state_fixed.o

# 用 patch_o.py 修复 .rodata 的 TEXTREL
python3 ../tools/patch_o.py boss_state_fixed.o boss_state_patched.o
```

**第 3 步：boss_tricky.o — 用 PIC 重写**

创建 `boss_tricky_pic.c`：
```c
extern int gvar;
int shared_addr(void) { return (int)(unsigned long)&gvar; }
int read_shared(void) { return gvar; }
```

```bash
gcc -c -fPIC -O1 -o boss_tricky_pic.o boss_tricky_pic.c
```

**第 4 步：链接所有文件**

```bash
gcc -shared -o libbomb.so boss_helper.o boss_state_patched.o boss_tricky_pic.o
python3 ../bomb_tester.py . libbomb.so
```

#### 配置信息解读

从 `config.json` 确认各函数的期望行为：

| 函数 | 行为 | 期望值 |
|---|---|---|
| `read_shared()` | 读取 `gvar` | 501 |
| `next_val()` | 返回 `counter` 并自增 | 43, 44, 45（初始值 43） |
| `sum_data()` | 对 `data[3]` 求和 | 367 |
| `helper_double(7)` | 返回 `x * 2` | 14 |
| `helper_square(7)` | 返回 `x * x` | 49 |
| `shared_addr()` | 返回 `gvar` 的地址 | 不检查具体值 |

---

## 第四部分：知识地图

```
关卡设计思路:

Level 1  ─  纯函数，无重定位问题
   │         教你：ar 解包 + gcc -shared 基本流程
   │
Level 2  ─  R_X86_64_32/32S 绝对地址
   │         教你：认识绝对地址重定位，用 -fPIC 重写
   │
Level 3  ─  R_X86_64_PC32 + GLOBAL 符号
   │         教你：符号绑定影响链接，用 objcopy 修改
   │
Level 4  ─  .rodata 中的重定位 → TEXTREL
   │         教你：理解 ELF 节区结构，手动解析并修改二进制
   │
Level 5  ─  以上三种问题的组合
             教你：综合分析能力，对每个 .o 诊断不同问题并分别修复
```

---

## 第五部分：常见踩坑

### 5.1 "明明用了 objcopy --localize-symbol，还是链接失败"

检查你是否 localiz**e** 了正确的符号。用 `readelf -s` 确认修改后的文件中符号绑定是否真的变了。

### 5.2 "patch_o.py 运行后没效果"

确保 `data` 用的是 `bytearray` 而不是 `bytes`：
```python
data = bytearray(f.read())   # ✓ 可修改
data = f.read()               # ✗ bytes 不可修改，struct.pack_into 会报错
```

### 5.3 "gcc -shared 报错 relocation ... can not be used when making a shared object"

这个错误说明 `.o` 中有无法用于共享库的重定位（通常是 `R_X86_64_32` / `R_X86_64_32S`）。你需要像 Level 2 那样用 `-fPIC` 重写。

### 5.4 "Level 5 中 gvar 应该 localize 吗？"

**不能！** `boss_tricky.o`（或其 PIC 替代品）需要通过全局符号 `gvar` 访问 `boss_state.o` 中定义的变量。如果你 localize 了 `gvar`，`boss_tricky.o` 就找不到它了。

### 5.5 "readelf 输出是中文/乱码"

`readelf` 的某些检查依赖英文输出。设置 `LANG=C`：
```bash
LANG=C readelf -d libbomb.so
```

`bomb_tester.py` 内部已经自动设置了 `LANG=C`。

---

## 第六部分：完整操作速查表

```bash
# ============ Level 1 ============
cd level1
ar -x bomb1.a
gcc -shared -o libbomb.so *.o
python3 ../bomb_tester.py . libbomb.so

# ============ Level 2 ============
cd ../level2
ar -x bomb2.a
# 分析原代码行为，创建 PIC 替代
cat > lv2_pic.c << 'EOF'
int gvar = 108;  # 从 config.json 的 desc 或 read_var 的 expected 得知
int get_addr(void) { return (int)(unsigned long)&gvar; }
int read_var(void) { return gvar; }
static int arr[] = {10, 20, 30, 40, 50};
int get_elem(int i) { return arr[i]; }
EOF
gcc -c -fPIC -O1 -o lv2_pic.o lv2_pic.c
gcc -shared -o libbomb.so lv2_pic.o
python3 ../bomb_tester.py . libbomb.so

# ============ Level 3 ============
cd ../level3
ar -x bomb3.a
objcopy --localize-symbol=counter lv3.o lv3_fixed.o
gcc -shared -o libbomb.so lv3_fixed.o
python3 ../bomb_tester.py . libbomb.so

# ============ Level 4 ============
cd ../level4
ar -x bomb4.a
python3 ../tools/patch_o.py lv4.o lv4_fixed.o
gcc -shared -o libbomb.so lv4_fixed.o
python3 ../bomb_tester.py . libbomb.so

# ============ Level 5 ============
cd ../level5
ar -x bomb5.a
# boss_helper.o — 直接用
# boss_state.o — localize counter + patch TEXTREL
objcopy --localize-symbol=counter boss_state.o boss_state_fixed.o
python3 ../tools/patch_o.py boss_state_fixed.o boss_state_patched.o
# boss_tricky.o — PIC 重写
cat > boss_tricky_pic.c << 'EOF'
extern int gvar;
int shared_addr(void) { return (int)(unsigned long)&gvar; }
int read_shared(void) { return gvar; }
EOF
gcc -c -fPIC -O1 -o boss_tricky_pic.o boss_tricky_pic.c
# 链接全部
gcc -shared -o libbomb.so boss_helper.o boss_state_patched.o boss_tricky_pic.o
python3 ../bomb_tester.py . libbomb.so
```
