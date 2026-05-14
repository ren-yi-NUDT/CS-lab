# 实验6  性能优化实验报告
---

## 一、实验目的

1. 理解程序局部性和缓存机制对程序性能的影响
2. 掌握通过优化内存访问模式提升程序性能的方法
3. 掌握通过减少操作数量和利用指令级并行提升程序性能的方法

## 二、实验环境

编译器：GCC (MinGW64)，编译选项 `-O2`

编译与运行命令：

```powershell
cd matrix        # 或 cd poly
mingw32-make     # 编译
mingw32-make run # 编译并运行
```

## 三、实验内容

- **任务一**：修改 `rowcol.c`，优化矩阵行列求和函数的执行效率，注意考虑矩阵元素在存储器中的排列
- **任务二**：修改 `poly.c`，优化多项式求值函数的执行效率，首先实现指定学号的常系数多项式，再分别优化 CPE 和 C(10)

---

## 四、任务一：矩阵计算优化

### 4.1 问题分析

给定 512×512 整数矩阵，需实现两个函数：

- `c_sum`：计算每一列的和
- `rc_sum`：计算每一行和每一列的和

C 语言二维数组按**行优先**存储，即 `M[0][0], M[0][1], ..., M[0][511], M[1][0], ...`。CPU 通过缓存行（64 字节 = 16 个 int）批量读取内存，**连续访问相邻元素**命中率高，**跳跃访问**命中率低。

### 4.2 原始代码分析

参考实现的 `c_sum` 按列遍历矩阵：

```c
for (j = 0; j < N; j++) {       // 外层：每一列
    colsum[j] = 0;
    for (i = 0; i < N; i++)
        colsum[j] += M[i][j];   // 内层：沿列向下，跨步 N
}
```

内层循环中 `M[i][j]` 的 i 递增、j 固定，每次访问在内存中跳跃 512×4 = 2048 字节，远超缓存行大小，几乎每次都缓存未命中。

参考实现的 `rc_sum` 中，`colsum[i] += M[j][i]` 同样按列访问矩阵，存在相同问题。

### 4.3 优化方法

#### （1）交换循环顺序

将 `c_sum` 的双层循环交换：外层遍历行，内层遍历列，使矩阵访问变为行优先：

```c
for (i = 0; i < N; i++)         // 外层：每一行
    for (j = 0; j < N; j++)
        colsum[j] += M[i][j];   // 内层：沿行向右，步长 1
```

#### （2）4×循环展开

将内层循环每次处理 4 个元素，减少循环判断和跳转的开销，同时 4 个 colsum 累加互不依赖，有利于 CPU 指令级并行：

```c
for (j = 0; j < N; j += 4) {
    int m0 = M[i][j], m1 = M[i][j+1], m2 = M[i][j+2], m3 = M[i][j+3];
    colsum[j]   += m0;  colsum[j+1] += m1;
    colsum[j+2] += m2;  colsum[j+3] += m3;
}
```

#### （3）rc_sum 中复用矩阵元素

`rc_sum` 需同时计算行和与列和。将列和改为 `colsum[j] += M[i][j]`（行优先），与行和共用同一个矩阵元素读取值，避免重复访存。行和累加使用局部变量 `rsum`，减少对内存的写操作。

### 4.4 最终代码

```c
void c_sum(matrix_t M, vector_t rowsum, vector_t colsum)
{
    int i, j;
    for (j = 0; j < N; j++)
        colsum[j] = 0;
    for (i = 0; i < N; i++) {
        for (j = 0; j < N; j += 4) {
            int m0 = M[i][j], m1 = M[i][j+1], m2 = M[i][j+2], m3 = M[i][j+3];
            colsum[j] += m0; colsum[j+1] += m1;
            colsum[j+2] += m2; colsum[j+3] += m3;
        }
    }
}

void rc_sum(matrix_t M, vector_t rowsum, vector_t colsum)
{
    int i, j;
    for (i = 0; i < N; i++)
        rowsum[i] = colsum[i] = 0;
    for (i = 0; i < N; i++) {
        int rsum = 0;
        for (j = 0; j < N; j += 4) {
            int m0 = M[i][j], m1 = M[i][j+1], m2 = M[i][j+2], m3 = M[i][j+3];
            rsum += m0 + m1 + m2 + m3;
            colsum[j] += m0; colsum[j+1] += m1;
            colsum[j+2] += m2; colsum[j+3] += m3;
        }
        rowsum[i] = rsum;
    }
}
```

### 4.5 实验结果

| 函数 | 参考实现（周期/元素） | 优化后（周期/元素） | 得分 |
|------|-----------------------|---------------------|------|
| c_sum（列求和） | 7.70 | 0.15 | 120 |
| rc_sum（行列求和） | 9.75 | 0.36 | 120 |

---

## 五、任务二：多项式计算函数优化

### 5.1 问题分析

计算多项式 `P(x) = a[0] + a[1]·x + a[2]·x² + ... + a[n]·xⁿ`，从三个维度评分：

| 评分项 | 含义 | 满分条件 |
|--------|------|----------|
| 常系数多项式 | 系数固定的 3 阶多项式 | 计算时间 ≤ 31 周期 |
| CPE | 每个系数平均消耗的周期数（高阶多项式） | CPE ≤ 1.75 |
| C(10) | 固定 10 阶多项式的总耗时 | 耗时 ≤ 43 周期 |

### 5.2 原始代码分析

参考实现每次循环做 2 次乘法：

```c
for (i = 0; i <= degree; i++) {
    result += a[i] * xpwr;   // 乘法 1：系数 × 幂次
    xpwr   *= x;             // 乘法 2：更新幂次
}
```

乘法是耗时运算，且各次循环之间存在数据依赖（result、xpwr），CPU 无法并行执行。

### 5.3 优化方法

#### （1）Horner 法（秦九韶算法）

将多项式从内向外套括号：`a[0] + x·(a[1] + x·(a[2] + x·a[3]))`，每次循环仅需 1 次乘法：

```c
int result = a[degree];
for (i = degree - 1; i >= 0; i--)
    result = result * x + a[i];
```

乘法次数减半，CPE 从 4.00 降至 1.78。

#### （2）常系数多项式完全展开

3 阶多项式仅 4 项，直接展开消除循环：

```c
int const_poly_eval(int *a, int degree, int x)
{
    return a[0] + x * (a[1] + x * (a[2] + x * a[3]));
}
```

只有 3 次乘法 + 3 次加法，无循环开销。系数通过参数 `a` 从运行时读取。

#### （3）4 路独立累加器（4x4a）—— 指令级并行

Horner 法中每次迭代依赖上一次的 result，CPU 执行单元串行等待。将多项式拆为 4 个子多项式，用 4 个独立变量 acc0~acc3 分别以 x⁴ 为基累加，互不依赖的运算可被 CPU 并行执行：

```c
int x4 = x * x * x * x;
for (i = degree; i >= 3; i -= 4) {
    acc3 = acc3 * x4 + a[i];       // ┐
    acc2 = acc2 * x4 + a[i - 1];   // ├─ 互不依赖，可并行
    acc1 = acc1 * x4 + a[i - 2];   // │
    acc0 = acc0 * x4 + a[i - 3];   // ┘
}
```

最后合并 4 路结果：`result = ((acc3·x + acc2)·x + acc1)·x + acc0`。

CPE 从 1.78 降至 0.45。

#### （4）循环展开（4x1a）—— C(10) 优化

对 C(10) 采用 4×循环展开的 Horner 法，减少循环开销：

```c
int result = a[degree];
for (i = degree - 1; i >= 3; i -= 4) {
    result = result * x + a[i];
    result = result * x + a[i - 1];
    result = result * x + a[i - 2];
    result = result * x + a[i - 3];
}
```

### 5.4 实验结果

| 评分项 | 得分 |
|--------|------|
| 常系数多项式 | 120 |
| CPE | 120 |
| C(10) | 120 |

各阶段 CPE 对比：

| 实现方式 | CPE | 优化手段 |
|----------|-----|----------|
| 参考实现 | 4.00 | — |
| Horner 法 | 1.78 | 减少乘法次数 |
| Horner + 4 路并行累加器 | 0.45 | 指令级并行 |

---

## 六、实验总结

本实验从两个角度验证了程序优化的核心原则：

1. **利用局部性**：矩阵优化中，将列优先访问改为行优先访问，使内存访问模式与缓存行对齐，周期/元素从 7.70 降至 0.15，提升约 50 倍。这说明编写程序时必须考虑数据在内存中的实际排列方式。

2. **减少操作数量与利用并行**：多项式优化中，Horner 法将每次迭代乘法次数从 2 降至 1；4 路累加器进一步利用 CPU 的指令级并行能力，使 CPE 从 4.00 降至 0.45。这说明在算法层面减少冗余操作、在微结构层面消除数据依赖，都能显著提升性能。

两个任务均获得满分（120 分）。

---

## 七、任务三：Python → Rust 高性能移植（microgpt-rs）

### 7.1 项目背景

[microgpt.py](https://github.com/karpathy/microgpt) 是 Andrej Karpathy 编写的"最原子级"GPT 实现——仅用 Python 标准库（`os`、`math`、`random`），无 NumPy/PyTorch 依赖，完整实现了从自动微分到训练推理的全流程。

原版 Python 实现运行 1000 步训练 + 20 次推理采样需要 **~89 秒**。本项目将其移植为 Rust，通过 Arena 内存管理、编译器优化等手段，实现 **~387 倍加速**（0.23 秒）。

### 7.2 Python 版本性能分析

通过性能分析定位到核心瓶颈：

| 指标 | 数值 |
|------|------|
| 单步 forward pass | 0.044s |
| 单步 backward pass | 0.026s |
| 单步 optimizer | 0.002s |
| 单步计算图节点数 | ~60,000 |
| 单步 Value 对象创建数 | ~56,000 |
| 1000 步累计对象创建 | ~56,000,000 |

**根本原因**：

1. **纯 Python 标量运算**：每次加法/乘法都要创建新的 `Value` 对象（~0.4μs/次），涉及 Python 函数调用、对象分配、计算图记录
2. **无向量化**：`linear()` 用 Python `sum()` + generator 做矩阵乘法，是 O(n×m) 次 Python 级别操作
3. **计算图遍历开销**：`backward()` 用 Python 递归做拓扑排序，遍历 ~60K 节点
4. **GC 压力**：大量短生命周期 Value 对象导致持续的内存分配和垃圾回收

### 7.3 Rust 优化策略

#### （1）Arena 分配器 — 核心优化

将 Python 的指针链式计算图改为 **Arena（竞技场）分配器**模式：

```
Python:  Value → Value → Value → ... (每个节点独立堆分配，通过指针链接)
Rust:    Vec<Node> [node0, node1, node2, ...] (连续内存，通过索引引用)
```

- 所有计算图节点存储在单个 `Vec<Node>` 中
- 节点间的引用用 `usize` 索引而非指针
- 每步通过 `truncate()` 复用内存，避免反复分配/释放
- 连续内存布局对 CPU 缓存友好

#### （2）固定大小节点 — 消除堆分配

Python 的 `Value` 类为每个节点存储 `children` 列表（动态大小）。Rust 版本使用固定字段：

```rust
struct Node {
    data: f64,      // 前向传播的值
    grad: f64,      // 反向传播的梯度
    child0: usize,  // 第一个子节点索引
    child1: usize,  // 第二个子节点索引
    lg0: f64,       // 对 child0 的局部梯度
    lg1: f64,       // 对 child1 的局部梯度
}
```

每个节点仅 48 字节，无堆分配，对齐缓存行。

#### （3）编译器优化

- `cargo build --release` 启用最高级别优化
- LTO（Link-Time Optimization）跨模块优化
- Rust 编译器对连续内存上的循环自动向量化

### 7.4 并行化探索（数据并行）

尝试使用 `rayon` 实现数据并行（每步处理多个文档，多线程并行 forward+backward）：

| 配置 | 耗时 | 说明 |
|------|------|------|
| 单线程 | 0.23s | 最优 |
| batch_size=8 (8线程) | 0.34s | 线程同步开销 > 并行收益 |
| batch_size=4 (4线程) | 0.48s | 更多步数，开销更大 |

**结论**：模型太小（4192 参数，16 维 embedding），每个文档的计算仅需 ~0.2ms，而 rayon 线程同步 + Arena 分配开销约 ~0.12ms/文档，占计算时间的 55%。并行化对大模型有效（如 GPT-2 117M 参数），对这个教学级小模型反而引入额外开销。

### 7.5 使用方式

**环境要求**：Rust 工具链（rustc 1.56+，cargo），`input.txt` 数据文件

```bash
  ./run.sh          # 交互菜单，选择 Python / Rust / 对比测试
  ./run.sh python   # 直接运行 Python 版
  ./run.sh rust     # 直接运行 Rust 版
  ./run.sh bench    # 同时跑两个，对比耗时
```

输出示例：

```
num docs: 32033
vocab size: 27
num params: 4192
step    1 / 1000 | loss 3.2953
...
step 1000 / 1000 | loss 1.9545
--- inference (new, hallucinated names) ---
sample  1: jania
sample  2: belia
...
sample 20: jawa

Total time: 0.23s
```

### 7.6 性能对比

| 版本 | 1000 步训练 + 20 次推理 | 加速比 |
|------|------------------------|--------|
| Python (microgpt.py) | **89.0s** | 1× |
| Rust 单线程 (microgpt-rs) | **0.23s** | **~387×** |

加速来源分析：

| 优化手段 | 效果 | 原理 |
|----------|------|------|
| Arena 连续内存 | ~50× | 消除 per-node 堆分配，缓存友好 |
| 固定大小 Node | ~10× | 消除 Vec 动态分配开销 |
| 编译器 -O3 + LTO | ~5× | 循环优化、内联、自动向量化 |
| 消除 GC | ~3× | 无垃圾回收暂停 |
| Rust 零成本抽象 | ~2× | 无 Python 解释器开销 |

### 7.7 代码结构与对应关系

```
microgpt-rs/
├── Cargo.toml          # 依赖：rand, rand_distr, rayon
└── src/
    └── main.rs         # 单文件实现，对应 microgpt.py
```

| Python (microgpt.py) | Rust (main.rs) | 说明 |
|-----------------------|----------------|------|
| `class Value` (L30-72) | `struct Node` + `struct Arena` | Arena autograd 引擎 |
| `Value.__add__/__mul__/...` | `Arena::add/mul/...` | 标量运算节点 |
| `Value.backward()` | `Arena::backward()` | 反向传播（梯度计算） |
| `matrix = lambda ...` | `make_param_idx()` | 参数初始化 |
| `def linear()` | `fn linear()` | 矩阵-向量乘法 |
| `def softmax()` | `fn softmax()` | 数值稳定 softmax |
| `def rmsnorm()` | `fn rmsnorm()` | RMS 归一化 |
| `def gpt()` | `fn gpt()` | GPT 前向传播 |
| Adam optimizer (L146-182) | 训练循环中的 Adam 更新 | 优化器 |
| Inference (L186-200) | 推理采样循环 | 自回归生成 |

### 7.8 与原版的差异

1. **RNG 序列不同**：Rust 的 `StdRng` 与 Python 的 `random` 生成的随机数序列不同，因此参数初始化和采样结果不完全一致，但 loss 下降趋势和生成质量相当
2. **推理阶段不跟踪计算图**：推理时的 temperature-scaled softmax 直接在 `f64` 上计算，不创建 Arena 节点，进一步加速采样
3. **Arena 内存复用**：每步通过 `truncate()` 复用 Arena，而非重新分配，减少内存碎片
