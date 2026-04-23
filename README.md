# Y86-64 模拟器

基于 Rust 实现的 Y86-64 指令集模拟器，支持从 Xilinx COE 文件加载机器码并解释执行。

## 设计方案

### 总体架构

模拟器采用**多周期**设计，每条指令依次经历以下阶段：

```
取指(Fetch) → 译码(Decode) → 执行(Execute) → 写回(Writeback)
```

CPU 状态包括：
- **寄存器文件**：15 个 64 位通用寄存器（%rax ~ %r14）+ PC
- **条件码**：ZF（零标志）、SF（符号标志）、OF（溢出标志）
- **状态**：AOK（正常）、HLT（停机）、ADR（地址错误）、INS（非法指令）
- **内存**：64KB 字节寻址空间，小端序存储

### 模块结构

```
src/
├── main.rs          — CLI 入口：解析参数、加载 COE、运行模拟器、输出结果
├── instruction.rs   — 指令定义：Opcode/Register 枚举、条件码逻辑、指令长度
├── memory.rs        — 内存模块：COE 文件解析、字节/8 字节读写、内存转储
└── cpu.rs           — CPU 核心：取指-译码-执行主循环
```

### 指令集 (ISA)

基于 CS:APP 教材中的 Y86-64 指令集，支持以下指令：

| 类别 | 指令 | 编码 | 长度 |
|------|------|------|------|
| 停机 | `halt` | `00` | 1B |
| 空操作 | `nop` | `10` | 1B |
| 寄存器传送 | `rrmovq rA, rB` | `20 rArB` | 2B |
| 条件传送 | `cmovXX rA, rB` | `2X rArB` | 2B |
| 立即数传送 | `irmovq V, rB` | `30 FrB V` | 10B |
| 寄存器→内存 | `rmmovq rA, D(rB)` | `40 rArB D` | 10B |
| 内存→寄存器 | `mrmovq D(rB), rA` | `50 rArB D` | 10B |
| 算术逻辑 | `addq/subq/andq/xorq rA, rB` | `6X rArB` | 2B |
| 跳转 | `jmp/jXX Dest` | `7X Dest` | 9B |
| 函数调用 | `call Dest` | `80 Dest` | 9B |
| 函数返回 | `ret` | `90` | 1B |
| 压栈 | `pushq rA` | `A0 FrA` | 2B |
| 出栈 | `popq rA` | `B0 FrA` | 2B |

条件传送 cmovXX 的 XX 后缀：`le`(1), `l`(2), `e`(3), `ne`(4), `ge`(5), `g`(6)。

跳转 jXX 的 XX 后缀：`mp`(0), `le`(1), `l`(2), `e`(3), `ne`(4), `ge`(5), `g`(6)。

### 寄存器编码

| 编号 | 寄存器 | 编号 | 寄存器 |
|------|--------|------|--------|
| 0 | %rax | 8 | %r8 |
| 1 | %rcx | 9 | %r9 |
| 2 | %rdx | A | %r10 |
| 3 | %rbx | B | %r11 |
| 4 | %rsp | C | %r12 |
| 5 | %rbp | D | %r13 |
| 6 | %rsi | E | %r14 |
| 7 | %rdi | F | 无（NONE） |

### 指令编码格式

指令中寄存器以单字节编码：高 4 位为 rA，低 4 位为 rB。例如 `%rax, %rcx` 编码为 `0x01`。

8 字节立即数和地址采用**小端序**（Little-Endian）存储。

### COE 文件格式

使用 Xilinx COE 文件作为输入格式，每项为 1 字节机器码：

```
memory_initialization_radix=16;
memory_initialization_vector=
30, f0, 64, 00, 00, 00, 00, 00, 00, 00,
...,
00;
```

- 第一行指定数据进制（支持 2/10/16）
- 数据项以逗号分隔，末尾以分号结束
- 每项对应一个字节的机器码，按地址递增排列

### 执行流程

```
main() → Memory::load_from_coe() → Cpu::new() → Cpu::run()
                                                └→ loop { step() }
                                                   ├── 取指：从 PC 读取指令字节
                                                   ├── 译码：解析操作码、寄存器、立即数
                                                   ├── 推进 PC
                                                   └── 执行：根据指令类型操作寄存器/内存
```

条件码在 ALU 运算（addq/subq/andq/xorq）后自动更新，跳转和条件传送指令根据条件码判断是否执行。

## 使用说明

### 编译

```bash
cd y86sim
cargo build --release
```

### 运行

```bash
# 基本运行
cargo run -- test_sum.coe

# 显示所有非零内存内容
cargo run -- test_sum.coe --mem

# 转储指定地址范围的内存（十六进制地址 + 字节数）
cargo run -- test_sum.coe --dump 0x100 16

# 组合使用
cargo run -- test_sum.coe --mem --dump 0x0 64
```

### 输出说明

程序运行后会输出：
1. 执行周期数
2. 所有 15 个寄存器的值（十进制 + 十六进制）
3. PC 值、条件码状态、执行状态
4. 内存转储（如果使用了 `--mem` 或 `--dump` 参数）

## 测试：从 1 加到 100

`test_sum.coe` 实现了 1+2+...+100 并将结果存入内存的功能。

### 汇编源码与对应机器码

```asm
# 地址    机器码                      汇编指令
0x000:  30 f0 64 00 00 00 00 00 00 00  irmovq $100, %rax    # 计数器 = 100（倒计数）
0x00a:  30 f1 00 00 00 00 00 00 00 00  irmovq $0, %rcx     # 累加和 = 0
0x014:  30 f2 01 00 00 00 00 00 00 00  irmovq $1, %rdx     # 减量 = 1
0x01e:  30 f3 00 01 00 00 00 00 00 00  irmovq $0x100, %rbx # 存储地址
# loop:
0x028:  60 01                          addq %rax, %rcx      # sum += counter
0x02a:  61 20                          subq %rdx, %rax      # counter--
0x02c:  76 28 00 00 00 00 00 00 00     jg loop              # counter > 0 则继续循环
0x035:  40 13 00 00 00 00 00 00 00 00  rmmovq %rcx, 0(%rbx) # 将结果存入内存
0x03f:  00                              halt                 # 停机
```

### 运行结果

```
已加载 64 字节机器码
开始执行...
执行完成，共 306 个周期
=== 寄存器状态 ===
   %rax =                    0 (0x0000000000000000)
   %rcx =                 5050 (0x00000000000013ba)
   ...
  状态 : Hlt
```

验证：
- `%rcx = 5050`（1+2+...+100 = 5050）
- 内存地址 `0x100` 处存储 `0x13ba`（5050 的小端序表示）
- 程序正常停机
