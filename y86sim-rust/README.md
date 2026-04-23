# Y86-64 模拟器（Rust 版）

基于 Rust 实现的 Y86-64 指令集模拟器，从 Xilinx COE 文件加载机器码并解释执行。

## 代码架构

```
y86sim-rust/
├── Cargo.toml
├── test_sum.coe           — 测试用 COE 文件（1加到100）
└── src/
    ├── main.rs            — 程序入口
    ├── instruction.rs     — 指令与寄存器定义
    ├── memory.rs          — 内存与 COE 解析
    └── cpu.rs             — CPU 执行核心
```

### 模块职责

#### `instruction.rs` — 指令定义层

定义了 Y86-64 的三类核心数据结构：

- **`Opcode` 枚举**：27 种指令操作码，通过 `from_byte(u8) -> Option<Opcode>` 从字节解码。每个变体携带编码值（如 `Halt = 0x00`），`instruction_length()` 返回该指令在内存中占用的字节数。
- **`Register` 枚举**：15 个通用寄存器 + `None`（编码 0xF），通过 `from_nibble(u8)` 从半字节解码。`as_index()` 返回寄存器文件数组下标。
- **`ConditionCodes` 结构体**：维护 ZF/SF/OF 三个标志位。`update()` 在 ALU 运算后根据结果更新标志（加/减法计算溢出，逻辑运算 OF=0）。`evaluate()` 根据当前标志和指令类型判断条件是否满足（跳转和条件传送共用同一套条件判断逻辑）。
- **`Status` 枚举**：CPU 运行状态（AOK/HLT/ADR/INS）。

#### `memory.rs` — 存储层

- **`Memory` 结构体**：64KB（65536 字节）的 `Vec<u8>`，模拟字节寻址内存。
- **COE 解析**（`load_from_coe`）：逐行解析 COE 文件头（`memory_initialization_radix` 指定进制），然后将数据区每个逗号分隔项写入连续内存地址。支持 radix 2/10/16。
- **读写接口**：`read_byte`/`write_byte`（1 字节）、`read_word`/`write_word`（8 字节，小端序）。所有操作带地址越界检查。
- **转储**：`dump(addr, len)` 按行输出十六进制 + ASCII；`dump_nonzero()` 仅输出非零区域。

#### `cpu.rs` — 执行核心

- **`Cpu` 结构体**：持有 `Memory`、16 个 `u64` 寄存器、PC、条件码、运行状态。`%rsp` 初始化为栈顶 `0x1000`。
- **`run()`**：主循环，反复调用 `step()` 直到状态非 AOK 或超过 1000 万周期安全限制。
- **`step()` — 单周期执行**：
  1. **取指**：从 PC 读取首字节，通过 `Opcode::from_byte()` 解码。根据指令长度读完整指令字节。
  2. **译码**：从指令第 2 字节提取 rA/rB（高/低半字节）。对于 10 字节指令（irmovq/rmmovq/mrmovq），从 byte[2..10] 读取 8 字节立即数；对于 9 字节指令（jXX/call），从 byte[1..9] 读取 8 字节目标地址。均按小端序解码。
  3. **推进 PC**：`PC += 指令长度`（跳转指令在执行阶段可能覆写 PC）。
  4. **执行**：通过 `match opcode` 分发到各指令的处理逻辑：
     - `halt`：设状态为 HLT
     - `rrmovq`/`cmovXX`：根据条件码判断是否传送
     - `irmovq`：立即数写入寄存器
     - `rmmovq`/`mrmovq`：寄存器基址 + 偏移量寻址，读写内存
     - `addq`/`subq`/`andq`/`xorq`：ALU 运算，更新条件码，结果写回 rB
     - `jXX`：根据条件码判断是否跳转（修改 PC）
     - `call`：返回地址压栈，跳转
     - `ret`：从栈弹出地址，跳转
     - `pushq`/`popq`：栈操作，`%rsp` 自减/自增 8

#### `main.rs` — 入口

解析命令行参数：`<coe_file> [--mem] [--dump <addr> <len>]`。加载 COE → 创建 CPU → 运行 → 打印寄存器 → 可选内存转储。

## 使用方法

```bash
cd y86sim-rust
cargo build --release

# 运行测试
cargo run -- test_sum.coe --mem

# 转储指定内存区域
cargo run -- test_sum.coe --dump 0x100 16
```

## 测试验证

`test_sum.coe` 执行 1+2+...+100，预期结果：`%rcx = 5050`，内存 `0x100` 处存储 `0x13ba`。
