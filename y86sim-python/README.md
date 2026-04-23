# Y86-64 模拟器（Python 版）

基于 Python 3 实现的 Y86-64 指令集模拟器，从 Xilinx COE 文件加载机器码并解释执行。

与 Rust 版功能完全一致，代码结构对应，便于理解实现原理。

## 代码架构

```
y86sim-python/
├── main.py            — 程序入口
├── instruction.py     — 指令与寄存器定义
├── memory.py          — 内存与 COE 解析
├── cpu.py             — CPU 执行核心
└── test_sum.coe       — 测试用 COE 文件（1加到100）
```

### 模块职责

#### `instruction.py` — 指令定义层

定义 Y86-64 指令集的核心数据与常量：

- **`Opcode` 枚举**：27 种指令，枚举值即为字节编码。`decode_opcode(byte)` 从字节查找对应的 `Opcode`。
- **`LEN_BY_OPCODE` 字典**：每种指令对应的字节长度（1/2/9/10），用于取指时确定读取范围。
- **`decode_regs(byte)`**：从寄存器字节解码出 `(rA, rB)` 两个 0-F 的半字节。
- **`ConditionCodes` 类**：维护 ZF/SF/OF 三个布尔标志位。`update(result, a, b, op)` 在 ALU 运算后更新标志——加法溢出用 `~(a^b) & (a^result)` 判断同号溢出，减法用 `(a^b) & (a^result)` 判断异号溢出。`evaluate(op)` 根据标志位和指令类型返回条件是否成立。
- **常量**：`MEM_SIZE=65536`（内存大小）、`STACK_TOP=0x1000`（栈顶）、`NONE_REG=0xF`（空寄存器编码）、`MASK64`（64 位掩码）。

#### `memory.py` — 存储层

- **`Memory` 类**：内部使用 `bytearray(65536)` 存储字节，比 `list` 更节省内存且支持 `struct` 直接操作。
- **`load_coe(path)`**：解析 COE 文件。逐行扫描，识别 `memory_initialization_radix` 头确定进制（2/10/16），进入 `memory_initialization_vector` 数据区后，按逗号分隔逐项解析并写入连续地址。返回总共加载的字节数。
- **读写接口**：`read_byte`/`write_byte` 操作单字节；`read_word`/`write_word` 使用 `struct.unpack_from('<Q')` / `struct.pack_into('<Q')` 读写 8 字节小端序 64 位整数。均带越界检查。
- **转储**：`dump(start, length)` 按 16 字节行输出十六进制+ASCII；`dump_nonzero()` 扫描全内存仅输出非零区域。

#### `cpu.py` — 执行核心

- **`Cpu` 类**：持有 `Memory` 实例、15 个 `u64` 寄存器列表、PC、`ConditionCodes`、运行状态字符串。`regs[4]`（%rsp）初始化为 `0x1000`。
- **`run(max_cycles=10_000_000)`**：主循环，反复调用 `_step()` 直到状态非 `AOK` 或超限。
- **`_step()` — 单周期执行**：
  1. **取指**：从 `mem.data[pc:pc+length]` 直接切片获取全部指令字节。
  2. **译码**：从第 2 字节 `decode_regs()` 得到 rA/rB。10 字节指令从 `raw[2:10]` 读取立即数，9 字节指令从 `raw[1:9]` 读取目标地址，均用 `int.from_bytes(..., 'little')` 解码。
  3. **推进 PC**：`pc += length`。
  4. **执行**：`if/elif` 链分发到各指令处理。ALU 运算结果用 `& MASK64` 截断为 64 位，然后调用 `cc.update()` 更新条件码。`rmmovq`/`mrmovq` 的地址用 `(reg + offset) & MASK64` 处理溢出。栈操作（`pushq`/`popq`/`call`/`ret`）通过 `%rsp` 自减/自增 8 管理。
- **`print_registers()`**：格式化输出全部寄存器、PC、条件码、状态。`to_i64()` 辅助函数将无符号 64 位值转为有符号显示。

#### `main.py` — 入口

解析命令行参数：`<coe_file> [--mem] [--dump <addr> <len>]`。流程：加载 COE → 创建 CPU → 运行 → 打印寄存器 → 可选内存转储。

## 使用方法

```bash
cd y86sim-python

# 运行测试
python3 main.py test_sum.coe --mem

# 转储指定内存区域
python3 main.py test_sum.coe --dump 0x100 16
```

## 与 Rust 版的对照

| 特性 | Rust 版 | Python 版 |
|------|---------|-----------|
| 寄存器类型 | `Register` 枚举 + `as_index()` | `int`（0-14 为有效，15=None） |
| 指令长度 | `Opcode::instruction_length()` 方法 | `LEN_BY_OPCODE` 字典查找 |
| 内存读写 | `u64::from_le_bytes()` / `to_le_bytes()` | `struct.unpack_from` / `pack_into` |
| 条件码 | `ConditionCodes` 结构体 + 方法 | `ConditionCodes` 类 + 方法 |
| 溢出检测 | 有符号比较（`as i64`） | 位运算（异或高位判断） |
| 错误处理 | `Result<T, String>` | `raise RuntimeError/ValueError` |
| 64 位截断 | `wrapping_add`/`wrapping_sub` | `& MASK64` |

## 测试验证

`test_sum.coe` 执行 1+2+...+100，预期结果：`%rcx = 5050`，内存 `0x100` 处存储 `0x13ba`，共 306 周期。
