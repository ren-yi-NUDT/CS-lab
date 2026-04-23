# CS 实验四 — Y86-64 模拟器

使用 Rust 和 Python 分别实现 Y86-64 指令集模拟器，从 Xilinx COE 文件加载机器码并解释执行。

## 项目结构

```
lab4/
├── README.md              ← 本文件
├── toy.py                 — Toy Machine 数值编码模拟器（参考）
├── toy2.py                — Toy Machine 助记符编码模拟器（参考）
├── test.toy / test.toy2   — Toy 测试程序
├── y86sim-rust/           — Y86-64 模拟器 Rust 实现
│   ├── README.md          — Rust 版架构说明
│   ├── Cargo.toml
│   ├── src/
│   │   ├── main.rs
│   │   ├── instruction.rs
│   │   ├── memory.rs
│   │   └── cpu.rs
│   └── test_sum.coe
└── y86sim-python/         — Y86-64 模拟器 Python 实现
    ├── README.md          — Python 版架构说明
    ├── main.py
    ├── instruction.py
    ├── memory.py
    ├── cpu.py
    └── test_sum.coe
```

## 设计方案

### 设计目标

实现一个能从 COE 文件读取 Y86-64 机器码、解释执行指令、并输出存储器/寄存器值的模拟器。采用多周期设计（取指 → 译码 → 执行 → 写回），分别用 Rust 和 Python 实现以对比两种语言在系统级模拟中的差异。

### Y86-64 指令集

基于 CS:APP 教材定义的 Y86-64 ISA，是 x86-64 的简化子集：

| 指令 | 功能 | 编码 | 长度 |
|------|------|------|------|
| `halt` | 停机 | `00` | 1B |
| `nop` | 空操作 | `10` | 1B |
| `rrmovq rA, rB` | 寄存器→寄存器 | `20 rArB` | 2B |
| `cmovXX rA, rB` | 条件传送 | `2X rArB` | 2B |
| `irmovq V, rB` | 立即数→寄存器 | `30 FrB V` | 10B |
| `rmmovq rA, D(rB)` | 寄存器→内存 | `40 rArB D` | 10B |
| `mrmovq D(rB), rA` | 内存→寄存器 | `50 rArB D` | 10B |
| `addq rA, rB` | 加法 | `60 rArB` | 2B |
| `subq rA, rB` | 减法 | `61 rArB` | 2B |
| `andq rA, rB` | 按位与 | `62 rArB` | 2B |
| `xorq rA, rB` | 按位异或 | `63 rArB` | 2B |
| `jmp Dest` | 无条件跳转 | `70 Dest` | 9B |
| `jXX Dest` | 条件跳转 | `7X Dest` | 9B |
| `call Dest` | 函数调用 | `80 Dest` | 9B |
| `ret` | 函数返回 | `90` | 1B |
| `pushq rA` | 压栈 | `A0 FrA` | 2B |
| `popq rA` | 出栈 | `B0 FrA` | 2B |

寄存器编号：`%rax`=0, `%rcx`=1, `%rdx`=2, `%rbx`=3, `%rsp`=4, `%rbp`=5, `%rsi`=6, `%rdi`=7, `%r8`=8 ~ `%r14`=14, `NONE`=15。编码格式：高 4 位 rA，低 4 位 rB。8 字节立即数/地址按小端序存储。

### 系统架构

模拟器分为三层：

```
┌─────────────────────────────────┐
│           main（入口）           │  解析参数、加载 COE、运行、输出
├──────────────┬──────────────────┤
│   CPU 核心    │    指令定义层     │  取指-译码-执行循环
│              │  Opcode/Register │  条件码逻辑
│              │  ConditionCodes  │  指令长度
├──────────────┴──────────────────┤
│           内存模块               │  COE 解析、字节/字读写、转储
└─────────────────────────────────┘
```

**CPU 状态**：
- 15 个 64 位通用寄存器 + PC（程序计数器）
- 条件码：ZF（零）、SF（符号）、OF（溢出）
- 运行状态：AOK → HLT
- 64KB 字节寻址内存，`%rsp` 初始化为 `0x1000`

**执行流程**（多周期）：
```
每条指令：
  1. 取指  — 从 PC 读操作码字节，确定指令长度，读取完整指令
  2. 译码  — 解析寄存器编号、立即数/偏移量/目标地址
  3. 推进 PC — PC += 指令长度
  4. 执行  — 根据指令类型操作寄存器/内存/PC（跳转指令覆写 PC）
```

条件码在 ALU 运算（addq/subq/andq/xorq）后自动更新，跳转和条件传送指令据此判断。

### COE 文件格式

Xilinx COE 格式，每项 1 字节机器码：

```
memory_initialization_radix=16;
memory_initialization_vector=
30, f0, 64, 00, ...,
00;
```

- `radix` 支持二进制(2)、十进制(10)、十六进制(16)
- 数据项逗号分隔，末尾分号结束
- 按地址递增排列

## 使用说明

### Rust 版

```bash
cd y86sim-rust
cargo build --release
cargo run -- test_sum.coe --mem
cargo run -- test_sum.coe --dump 0x100 16
```

### Python 版

```bash
cd y86sim-python
python3 main.py test_sum.coe --mem
python3 main.py test_sum.coe --dump 0x100 16
```

### 命令行参数

| 参数 | 说明 |
|------|------|
| `<coe_file>` | COE 机器码文件路径（必需） |
| `--mem` | 显示所有非零内存内容 |
| `--dump <addr> <len>` | 转储从 addr 开始 len 字节的内存（十六进制地址） |

## 测试：从 1 加到 100

`test_sum.coe` 实现了 `1+2+...+100` 并将结果存入内存地址 `0x100`。

### 汇编与机器码对照

```asm
0x000:  30 f0 64 00 00 00 00 00 00 00  irmovq $100, %rax     # 计数器=100
0x00a:  30 f1 00 00 00 00 00 00 00 00  irmovq $0, %rcx      # 累加和=0
0x014:  30 f2 01 00 00 00 00 00 00 00  irmovq $1, %rdx      # 减量=1
0x01e:  30 f3 00 01 00 00 00 00 00 00  irmovq $0x100, %rbx  # 存储地址
# loop (0x028):
0x028:  60 01                          addq %rax, %rcx       # sum += counter
0x02a:  61 20                          subq %rdx, %rax       # counter--
0x02c:  76 28 00 00 00 00 00 00 00     jg 0x028              # counter>0 继续
0x035:  40 13 00 00 00 00 00 00 00 00  rmmovq %rcx, 0(%rbx) # 存结果到内存
0x03f:  00                              halt
```

### 运行结果

```
已加载 64 字节机器码
执行完成，共 306 个周期
=== 寄存器状态 ===
   %rcx =                 5050 (0x00000000000013ba)
  状态 : HLT
```

验证通过：`%rcx = 5050`（1+2+...+100），内存 `0x100` 处存储 `0x13ba`（5050 小端序）。

## 两种实现对比

| 特性 | Rust 版 | Python 版 |
|------|---------|-----------|
| 编译 | `cargo build` 编译为原生二进制 | 解释执行，无需编译 |
| 类型安全 | 枚举 + match 穷举检查 | 运行时字典查找 |
| 内存操作 | `from_le_bytes`/`to_le_bytes` | `struct` 模块 |
| 溢出处理 | `wrapping_add`/`wrapping_sub` | `& MASK64` 位掩码 |
| 错误处理 | `Result<T, String>` | 异常（`ValueError`/`RuntimeError`） |
| 性能 | 原生执行，适合大规模模拟 | 解释执行，适合快速原型验证 |
