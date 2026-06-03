# 链接炸弹实验报告

**学号**: 720028

## Level 1: 蒸汽时代 (Steam Age) — 10 分

### readelf -r 输出

```
Relocation section '.rela.eh_frame' at offset 0x1e0 contains 4 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000020  000200000002 R_X86_64_PC32     0000000000000000 .text + 0
000000000034  000200000002 R_X86_64_PC32     0000000000000000 .text + 8
000000000048  000200000002 R_X86_64_PC32     0000000000000000 .text + 11
00000000005c  000200000002 R_X86_64_PC32     0000000000000000 .text + 1b
```

### 问题分析

`lv1.o` 的重定位表中只有 `.rela.eh_frame` 节区的 `R_X86_64_PC32` 条目，`.text` 节区没有任何重定位。这说明炸弹中的四个函数（`add`、`sub`、`mul`、`mod`）是纯算术运算，不引用任何全局变量或外部符号。`R_X86_64_PC32` 是 PC 相对寻址，天然适合共享库，链接器不会拒绝。

### 修复思路

无需任何修复。直接解包并链接即可：

```bash
ar -x bomb1.a
gcc -shared -o libbomb.so lv1.o
```

---

## Level 2: 全局危机 (Global Crisis) — 20 分

### readelf -r 输出

```
Relocation section '.rela.text' at offset 0x248 contains 4 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000009  00060000000b R_X86_64_32S      0000000000000000 gvar + 0
00000000000e  00060000000a R_X86_64_32       0000000000000000 gvar + 0
000000000019  000600000002 R_X86_64_PC32     0000000000000000 gvar - 4
000000000028  00040000000b R_X86_64_32S      0000000000000000 .rodata + 0

Relocation section '.rela.eh_frame' at offset 0x2a8 contains 3 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000020  000200000002 R_X86_64_PC32     0000000000000000 .text + 0
000000000034  000200000002 R_X86_64_PC32     0000000000000000 .text + 13
000000000048  000200000002 R_X86_64_PC32     0000000000000000 .text + 1e
```

### 问题分析

`get_addr` 函数中存在两处问题重定位：

1. 偏移 0x9 处的 `R_X86_64_32S`：`movq $0x0,-0x8(%rsp)` 指令中嵌入了 `gvar` 的 32 位绝对地址（带符号扩展）
2. 偏移 0xe 处的 `R_X86_64_32`：`mov $0x0,%eax` 指令中嵌入了 `gvar` 的 32 位绝对地址（零扩展）
3. 偏移 0x28 处的 `R_X86_64_32S`：`get_elem` 中 `arr` 数组的首地址使用了 32 位绝对地址

`R_X86_64_32` 和 `R_X86_64_32S` 在指令中嵌入 32 位绝对地址。共享库可能被加载到 4GB 以上的地址空间，32 位地址无法表示这个范围，因此链接器拒绝将这些重定位用于共享库。

### 修复思路

通过反汇编分析原始函数逻辑，用 `-fPIC` 重新编写等价的 C 代码并编译：

```c
int gvar = 108;
int get_addr(void) { return (int)(unsigned long)&gvar; }
int read_var(void) { return gvar; }
static int arr[] = {10, 20, 30, 40, 50};
int get_elem(int i) { return arr[i]; }
```

PIC 代码会使用 `R_X86_64_GOTPCREL` 通过 GOT 间接访问全局变量，不再嵌入绝对地址：

```bash
gcc -fPIC -c lv2_pic.c -o lv2_pic.o
gcc -shared -o libbomb.so lv2_pic.o
```

### 思考题

**1. 为什么 `get_elem` 访问 `arr[i]` 没有产生 `R_X86_64_32` 重定位？**

`arr` 在原始代码中定义为 `static int arr[]`（静态局部数组）。编译器将其放入 `.rodata` 节区，`get_elem` 通过 `R_X86_64_32S` 引用的是 `.rodata` 节区本身的地址，而非 `arr` 这个符号。由于 `arr` 是 `static` 的，编译器知道它的地址在编译单元内固定，所以直接计算数组元素的偏移量。真正产生 `R_X86_64_32` / `R_X86_64_32S` 的是对全局符号 `gvar` 取地址的操作（`get_addr` 中），因为这需要在运行时解析符号的绝对地址。

**2. `R_X86_64_PC32` 和 `R_X86_64_32` 在指令编码上的根本区别是什么？**

- `R_X86_64_PC32`：重定位值是 **目标地址减去当前指令地址**（PC 相对偏移），编码为一个 32 位有符号差值添加到指令中的立即数字段。因为 x86-64 的 `mov` 等指令使用 RIP 相对寻址，该偏移能被动态链接器在运行时计算，不需要知道绝对地址。
- `R_X86_64_32` / `R_X86_64_32S`：重定位值是目标的 **32 位绝对地址**，直接嵌入指令的立即数字段。在 64 位系统中，共享库可能加载到超过 4GB 的地址，32 位无法容纳，因此链接器拒绝。

---

## Level 3: 数据陷阱 (Data Trap) — 20 分

### readelf -r 输出

```
Relocation section '.rela.text' at offset 0x168 contains 2 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000006  000400000002 R_X86_64_PC32     0000000000000000 counter - 4
00000000000f  000400000002 R_X86_64_PC32     0000000000000000 counter - 4

Relocation section '.rela.eh_frame' at offset 0x198 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Add1end
000000000020  000200000002 R_X86_64_PC32     0000000000000000 .text + 0
```

### readelf -s 符号表

```
   Num:    Value          Size Type    Bind   Vis      Ndx Name
     3: 0000000000000000    20 FUNC    GLOBAL DEFAULT    1 next_val
     4: 0000000000000000     4 OBJECT  GLOBAL DEFAULT    3 counter
```

### 问题分析

`next_val` 函数通过 `R_X86_64_PC32` 访问全局变量 `counter`。虽然 PC 相对寻址本身是安全的，但 `counter` 的绑定属性是 `GLOBAL`。在共享库中，全局符号可能被其他模块的同名符号覆盖（符号介入/symbol interposition）。如果发生符号介入，PC 相对引用将指向错误的地址，因此链接器拒绝。

### 修复思路

使用 `objcopy --localize-symbol` 将 `counter` 从 `GLOBAL` 改为 `LOCAL`。局部符号不会被外部介入，PC 相对引用就安全了：

```bash
objcopy --localize-symbol=counter lv3.o lv3_fixed.o
gcc -shared -o libbomb.so lv3_fixed.o
```

### 思考题

**1. 如果把 `counter` 声明为 `static`，这个问题还会出现吗？为什么？**

不会。`static` 变量在编译时绑定为 `STB_LOCAL`，链接器不会将其暴露给动态链接器。局部符号只在当前编译单元内可见，不会被外部同名符号覆盖（符号介入），因此 PC 相对引用始终指向正确的地址，链接器会允许链接为共享库。

**2. `objcopy` 除了改变符号绑定，还有哪些实用的 ELF 修改功能？**

- `--strip-symbol` / `--strip-all`：删除指定或全部符号
- `--globalize-symbol`：将局部符号变为全局
- `--redefine-sym`：重命名符号
- `--set-section-flags`：修改节区属性标志
- `--rename-section`：重命名节区
- `--add-section` / `--remove-section`：添加或删除节区
- `--only-section` / `--keep-section`：只保留或保留指定节区
- `--compress-debug-sections`：压缩调试信息

---

## Level 4: 二进制手术师 (Binary Surgeon) — 25 分

### readelf -r 输出

```
Relocation section '.rela.text' at offset 0x1a8 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000008  000300000002 R_X86_64_PC32     0000000000000000 .data - 4

Relocation section '.rela.rodata' at offset 0x1c0 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000000  000300000001 R_X86_64_64       0000000000000000 .data + 0

Relocation section '.rela.eh_frame' at offset 0x1d8 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000020  000200000002 R_X86_64_PC32     0000000000000000 .text + 0
```

### readelf -S 节区头部（修复前）

```
  [5] .rodata  PROGBITS  0000000000000000  00000058  0000000000000008  WA  0  0  8
```

`.rodata` 节区标志为 `A`（ALLOC）但无 `W`（WRITE），同时 `.rela.rodata` 中存在一条 `R_X86_64_64` 重定位（指向 `.data`）。

### 问题分析

炸弹中定义了一个 `const` 指针，指向静态变量。该指针被放入 `.rodata`（只读数据节区），其值需要在运行时通过重定位填入 `.data` 中变量的实际地址。但 `.rodata` 标记为只读（`SHF_ALLOC` 但无 `SHF_WRITE`），动态链接器无法在运行时修改它，产生 TEXTREL 错误。

具体来说：`.rela.rodata` 中偏移 0x0 处的 `R_X86_64_64` 重定位要求动态链接器在加载时将 `.data` 的 64 位绝对地址写入 `.rodata` 的起始位置。但 `.rodata` 所在的内存页面是只读的，无法写入。

### 修复思路

实现 `patch_o.py` 中的关键函数，检测 `.rodata` 中的重定位并添加 `SHF_WRITE` 标志：

1. `read_u16` / `read_u32` / `read_u64`：从 ELF 二进制的指定偏移读取整数
2. `get_section_name`：从 `.shstrtab` 字符串表中按索引查找节区名
3. `find_sections_with_textrel`：遍历所有 `SHT_RELA` 节区，通过 `sh_info` 找到被重定位的目标节区，检查是否"已分配但不可写"
4. `fix_textrel`：对检测到的只读节区在节区头部的 `sh_flags` 字段添加 `SHF_WRITE`

修复后 `.rodata` 的标志从 `A` 变为 `WA`，链接器认为该节区可写，不再报 TEXTREL：

```bash
python3 ../tools/patch_o.py lv4.o lv4_fixed.o
gcc -shared -o libbomb.so lv4_fixed.o
```

### 思考题

**1. 为什么 `.rodata` 中的重定位会导致 TEXTREL，而 `.data` 中的就不会？**

因为 `.data` 节区本身就标记为可写（`SHF_WRITE`），动态链接器可以正常修改其中的数据。而 `.rodata` 标记为只读（有 `SHF_ALLOC` 但无 `SHF_WRITE`），操作系统会将它映射到只读内存页面。动态链接器需要修改重定位目标位置，但无法写入只读页面，因此产生 TEXTREL 冲突。

**2. 从源码角度看，怎样改写才能避免这个问题？**

将 `const` 指针改为非 `const`。`const` 指针会被编译器放入 `.rodata`，而普通变量放入 `.data`（可写）。或者，不使用指针存储地址，改为在函数内部通过 PC 相对寻址直接访问目标变量。

---

## Level 5: 终极链接炸弹 (Ultimate Link Bomb) — 50 分

### 解包结果

`bomb5.a` 包含三个 `.o` 文件：

1. `boss_state.o` — 包含 `next_val`、`sum_data` 函数和多个全局变量
2. `boss_tricky.o` — 包含 `shared_addr`、`read_shared` 函数，引用外部 `gvar`
3. `boss_helper.o` — 包含 `helper_double`、`helper_square` 函数

### readelf -r 输出

**boss_state.o:**

```
Relocation section '.rela.text' at offset 0x248 contains 5 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000006  000600000002 R_X86_64_PC32     000000000000000c counter - 4
00000000000f  000600000002 R_X86_64_PC32     000000000000000c counter - 4
00000000001a  000300000002 R_X86_64_PC32     0000000000000000 .data + 0
000000000020  000300000002 R_X86_64_PC32     0000000000000000 .data - 4
000000000026  000300000002 R_X86_64_PC32     0000000000000000 .data + 4

Relocation section '.rela.rodata' at offset 0x2c0 contains 1 entry:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000000  000300000001 R_X86_64_64       0000000000000000 .data + 0

Relocation section '.rela.eh_frame' at offset 0x2d8 contains 2 entries:
```

**boss_tricky.o:**

```
Relocation section '.rela.text' at offset 0x1b0 contains 3 entries:
  Offset          Info           Type           Sym. Value    Sym. Name + Addend
000000000009  00040000000b R_X86_64_32S      0000000000000000 gvar + 0
00000000000e  00040000000a R_X86_64_32       0000000000000000 gvar + 0
000000000019  000400000002 R_X86_64_PC32     0000000000000000 gvar - 4

Relocation section '.rela.eh_frame' at offset 0x1f8 contains 2 entries:
```

**boss_helper.o:**

```
Relocation section '.rela.eh_frame' at offset 0x188 contains 2 entries:
```

### 问题分析

**boss_state.o** 存在两个问题：
1. **符号绑定问题**：`counter` 和 `sum_data` 引用全局符号使用 `R_X86_64_PC32`，但 `counter` 绑定为 `GLOBAL`（符号介入风险），与 Level 3 相同
2. **TEXTREL 问题**：`.rela.rodata` 中有 `R_X86_64_64` 重定位，`.rodata` 标记为只读，与 Level 4 相同

**boss_tricky.o** 存在一个问题：
- 包含 `R_X86_64_32S` 和 `R_X86_64_32` 绝对地址重定位（取 `gvar` 的地址），与 Level 2 相同。同时 `gvar` 是外部未定义符号（`UND`），需要 `boss_state.o` 提供

**boss_helper.o** 无问题：
- 只有 `.rela.eh_frame` 中的 PC 相对重定位，纯算术函数，可直接使用

### 修复思路

对三个文件分别使用前四关的技术：

1. **boss_helper.o**：安全文件，直接保留
2. **boss_state.o**：
   - 使用 `objcopy --localize-symbol=counter` 将 `counter` 从 `GLOBAL` 改为 `LOCAL`（Level 3 技术）
   - 使用 `patch_o.py` 对 `.rodata` 添加 `SHF_WRITE` 标志（Level 4 技术）
3. **boss_tricky.o**：
   - 分析 `gvar` 由 `boss_state.o` 定义（值为 501），用 `-fPIC` 重写 `shared_addr` 和 `read_shared`（Level 2 技术）

```bash
# boss_tricky.o: 用 PIC 代码替代
gcc -fPIC -c boss_tricky_pic.c -o boss_tricky_pic.o

# boss_state.o: 本地化符号 + 修补 TEXTREL
objcopy --localize-symbol=counter boss_state.o boss_state_fixed.o
python3 ../tools/patch_o.py boss_state_fixed.o boss_state_patched.o

# 链接所有修复后的 .o
gcc -shared -o libbomb.so boss_state_patched.o boss_tricky_pic.o boss_helper.o
```

---

## 总结

| 关卡 | 核心技术 | 分数 |
|------|---------|------|
| Level 1 | 直接链接（无问题） | 10 |
| Level 2 | PIC 重写替代绝对地址重定位 | 20 |
| Level 3 | `objcopy --localize-symbol` 修复符号绑定 | 20 |
| Level 4 | `patch_o.py` 为 `.rodata` 添加可写标志消除 TEXTREL | 25 |
| Level 5 | 综合应用 Level 2/3/4 技术 | 50 |
| **总分** | | **125** |
