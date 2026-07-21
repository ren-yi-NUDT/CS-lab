# Helpme — 设计思路详解

这份文档假设你刚接触多线程、SIMD、内嵌汇编。读完之后，你应该能讲清楚三件事：

1. 这个实验到底在搜什么？
2. 单线程版为什么慢？我们的多线程版怎么提速？
3. 在多线程基础上，AVX2 SIMD 和内嵌汇编各自再榨了多少性能出来，怎么榨的？

---

## 一、先搞清楚问题：到底在搜什么？

打开 `SearchRandom.c` 看一眼。这套"随机数机制"长这样：

```
某个应用产生密码 X ∈ [0, 2^32)
然后调用两次 GenerateRandomNumber：
    第一次：传入 (X, 0x29A)，更新得到 (Y, Z)
    第二次：传入 (Y, Z)，更新得到 (A, B)
最后把 B 暴露给用户
```

`GenerateRandomNumber` 的内部是固定的算术：

```c
unsigned long long x = (unsigned long long)*rand1_h;   // 取高 32 位
x *= 0x6AC690C5;                                       // 乘一个大常数
x += *rand1_l;                                         // 加低 32 位
*rand1_h = (unsigned int)x;                            // 写回高 32 位
*rand1_l = (unsigned int)(x >> 32);                    // 写回低 32 位
```

**关键观察：这个"随机数"机制是确定性的**——只要 X 一样，最后算出的 B 就一定一样。我们手里有 B = `0x39A6FFBB`，目标是反查所有满足条件的 X。

**怎么反查？** 没有数学捷径，只能穷举：把 X 从 0 试到 `0xFFFFFFFF`，对每个 X 算两步得到 B，看 B 等不等于目标值。一共要试 2³² ≈ 42.9 亿次。

---

## 二、基线版（`SearchRandom.c`）为什么慢？

核心循环长这样：

```c
for (i = 0; i < 0xFFFFFFFF; i++) {
    h = i; l = 0x29A;
    GenerateRandomNumber(&h, &l);   // 第一次
    GenerateRandomNumber(&h, &l);   // 第二次
    if (l == search_val) { /* 命中 */ }
}
```

**慢的两个原因**：

1. **单线程**：现代 CPU 都是多核的，比如这台 i9-14900HX 有 24 个逻辑核。单线程程序只用了其中一个，剩下 23 个核闲着发呆。
2. **每次只算一个值**：每次循环算 1 个 X 对应的 B。CPU 内部的 ALU（算术逻辑单元）一次只能算这一个。

在我的机器上，基线版耗时 **~13 秒**。

---

## 三、第一步加速：多线程（pthread）

### 思路

把 `[0, 2^32)` 这 42.9 亿个候选值划分成 N 份，开 N 个线程，每个线程独立算自己那份，结果汇总即可。

```
单线程：     [0 ──────────────────────────────── 2^32)
                 1 个核从头干到尾

4 线程：     [0 ─── 1 ───][2 ─── 3 ───][4 ─── 5 ───][6 ─── 7 ───]
              线程 0        线程 1       线程 2       线程 3
              同时跑在 4 个核上
```

### 用到的 pthread API

| 函数 | 作用 |
|------|------|
| `pthread_create(&tid, NULL, worker, &arg)` | 起一个线程，从 `worker` 函数开始执行，参数是 `arg` |
| `pthread_join(tid, NULL)` | 主线程阻塞等子线程跑完 |
| `pthread_exit(NULL)` | 子线程主动退出 |
| `pthread_mutex_lock / unlock` | 互斥锁，保护共享资源（比如 `printf` 不能让两个线程交错打印） |

### 区间划分的一个小坑

最朴素的想法是：每个线程分到 `chunk = 2^32 / N` 个值，第 k 个线程负责 `[k*chunk, (k+1)*chunk)`。

**问题**：最后一个线程的右端点 `(N)*chunk = 2^32`，但 `unsigned int` 只有 32 位，`2^32` 会回绕成 0。如果循环条件写成 `i < end`，最后一个线程永远跑不到 `end`。

**我们的解法**：循环条件改成 `i != end`，让最后一个线程的 `end = 0`，利用 uint32 自然回绕。循环会处理完 `0xFFFFFFFF` 之后 `i++` 自然变 0，此时 `i == end` 退出。这样不需要任何特判。

```c
for (i = begin; i != end; i++) { ... }
```

### 多线程版的效果

| 线程数 | 耗时 |
|--------|------|
| 1（基线） | ~13 s |
| 4 | ~3.5 s |
| 8 | ~2 s |
| 24 | ~1.2 s |

24 线程时加速比约 **11×**，不到 24× 的原因是 `pthread_create` / `pthread_join` 本身有开销，加上线程间缓存干扰。

---

## 四、第二步加速：AVX2 SIMD

### 什么是 SIMD？

SIMD = **S**ingle **I**nstruction, **M**ultiple **D**ata（单指令多数据）。

普通指令：一条指令操作一个数。比如 `mul` 一次乘两个 32 位数。

SIMD 指令：一条指令同时操作多个数。比如 AVX2 的 `vpmuludq`，一条指令同时做 **4 路 32×32→64 无符号乘法**。

```
普通指令（标量）：
   a0 * b0 ──┐
   a1 * b1 ──┤ 每次一条指令
   a2 * b2 ──┤
   a3 * b3 ──┘

AVX2 指令（向量）：
   [a0, a1, a2, a3] × [b0, b1, b2, b3] ── 一条指令同时算 4 个乘积
```

AVX2 寄存器有 256 位，可以装 4 个 64 位整数（或者 8 个 32 位整数）。我们这里走 64 位通道，因为乘法结果可能超过 32 位。

### 怎么把算法 SIMD 化？

回顾 `GenerateRandomNumber`：

```
x = h * 0x6AC690C5 + l     // 64 位运算
h_new = x 的低 32 位
l_new = x 的高 32 位
```

整体两次调用后：

```
sum1 = i * M + 0x29A               (64 位)
h1   = sum1 & 0xFFFFFFFF           (低 32 位)
l1   = sum1 >> 32                  (高 32 位)

sum2 = h1 * M + l1                 (64 位)
l2   = sum2 >> 32                  (这就是 B)
```

把这 7 步操作都换成 AVX2 向量指令，就能一次处理 4 个 X：

| 步骤 | AVX2 指令 | 含义 |
|------|-----------|------|
| `base * M` | `vpmuludq` | 4 路 32×32→64 乘法 |
| `+ 0x29A` | `vpaddq` | 4 路 64 位加法 |
| `& 0xFFFFFFFF` | `vpand` | 取低 32 位 |
| `>> 32` | `vpsrlq` | 4 路逻辑右移 |
| `== search_val` | `vpcmpeqq` | 4 路相等比较 |
| 提取比较结果 | `vmovmskps` | 把比较结果压成 8 位掩码 |

### 关键内建函数（intrinsic）

我们用 `<immintrin.h>` 提供的 intrinsic（C 函数包装的指令）：

```c
__m256i vM    = _mm256_set1_epi64x(0x6AC690C5ull);   // 把常数广播到 4 个通道
__m256i sum1  = _mm256_add_epi64(_mm256_mul_epu32(base, vM), vL29A);
__m256i h1    = _mm256_and_si256(sum1, vMASK32);
__m256i l1    = _mm256_srli_epi64(sum1, 32);
__m256i sum2  = _mm256_add_epi64(_mm256_mul_epu32(h1, vM), l1);
__m256i l2    = _mm256_srli_epi64(sum2, 32);
__m256i cmp   = _mm256_cmpeq_epi64(l2, vSEARCH);
unsigned mask = _mm256_movemask_epi8(cmp);
```

### 怎么知道哪个通道命中了？

`vmovmskps` 返回 8 位掩码，每位对应一个 32 位通道的最高位。

`vpcmpeqq` 比较的是 64 位通道，每个 64 位通道由 2 个 32 位通道组成，匹配时全置 1（即两个 32 位通道的最高位都是 1）。

所以 64 位通道 k 命中 ⇒ 32 位掩码的 bit `2k` 和 `2k+1` 都是 1。我们检查 `mask & 0x02`（lane 0）、`mask & 0x08`（lane 1）、`mask & 0x20`（lane 2）、`mask & 0x80`（lane 3）。

### 纯 intrinsic 版效果

| 版本 | 24 线程耗时 |
|------|------|
| 多线程（无 SIMD） | ~1.2 s |
| 多线程 + AVX2 intrinsic | ~0.86 s |

SIMD 只带来 ~1.4× 额外加速，远不到理论的 4×。**罪魁祸首是 `-O0`**——pptx 要求不能用 `-O1/-O2/-O3` 等优化级别，gcc 默认的 `-O0` 会把每个 intrinsic 计算结果存回栈，再从栈读回来给下一条 intrinsic，相当于每次都多了 2 次内存访问。这正是我们要进一步上内嵌汇编的原因。

---

## 五、第三步加速：内嵌汇编

### 为什么 `-O0` 慢？

`gcc -O0` 的特点是**逐条翻译、不做优化**。每个 C 语句独立翻译，变量都放栈上。对一个 intrinsic 调用：

```
理想：result = a * M          // 全在寄存器里，1 条指令
实际：load a from stack       // 多一次内存读
      load M from stack       // 多一次内存读
      vpmuludq                // 真正的计算
      store result to stack   // 多一次内存写
```

YMM 寄存器有 16 个，但 `-O0` 不会主动把它们用满——它宁愿把中间结果都塞栈。

### 内嵌汇编怎么做？

`asm volatile(...)` 让我们直接写汇编。整个热循环在一个 `asm` 块里，所有变量都明确分配到寄存器，从源头躲开 `-O0` 的栈访问。

简化后的热循环长这样（实际代码里我们每次处理 8 个值，这里展示 4 个的版本）：

```c
asm volatile (
    "1:\n\t"
    "vpmuludq  %[M],   %[b0], %[t0]\n\t"   // t0 = b0 * M
    "vpaddq    %[L],   %[t0], %[t0]\n\t"   // t0 = b0*M + 0x29A
    "vpand     %[MSK], %[t0], %[t1]\n\t"   // t1 = h1
    "vpsrlq    $32,    %[t0], %[t2]\n\t"   // t2 = l1
    "vpmuludq  %[M],   %[t1], %[t1]\n\t"   // t1 = h1 * M
    "vpaddq    %[t2],  %[t1], %[t1]\n\t"   // t1 = sum2
    "vpsrlq    $32,    %[t1], %[t1]\n\t"   // t1 = l2
    "vpcmpeqq  %[S],   %[t1], %[t1]\n\t"   // t1 = (l2 == SEARCH)
    "vmovmskps %[t1],  %[mlo]\n\t"         // mlo = 8 位掩码
    // ... 命中处理 ...
    "vpaddq %[STP], %[b0], %[b0]\n\t"      // b0 += 8
    "decq  %[it]\n\t"                       // iters--
    "jnz   1b\n\t"                          // 循环
    : /* 输出操作数 */
    : /* 输入操作数 */
    : /* clobber 列表 */
);
```

### 操作数约束怎么读

`%[name]` 是命名操作数。约束字符串告诉 gcc 把这个变量分配到哪种寄存器：

| 约束 | 含义 | 我们怎么用 |
|------|------|------------|
| `"x"` | 任意 SSE/AVX 寄存器（xmm 或 ymm） | 所有 `__m256i` 变量 |
| `"r"` | 任意通用寄存器（eax/rax 等） | `unsigned int`、`void*`、`uint64_t` |
| `"=x"` | 只写输出 | 我们没用，所有输出都是读写 |
| `"+x"` | 读写输出 | `b0`、`b1`（每轮 += STEP） |
| `"=&r"` | 早clobber（写早于读）的临时寄存器 | 临时变量 `mlo`、`tmp` 等 |

例如：

```c
: [b0]  "+x" (b0),          // b0 在 ymm 寄存器里，循环内会改
  [hp]  "+r" (hptr),        // hptr 在通用寄存器里，循环内会改
  [mlo] "=&r" (m_lo),       // m_lo 是临时通用寄存器，被写之后才被读
  [t0]  "=&x" (t0),         // t0 是临时 ymm 寄存器
  ...
: [M]   "x" (vM),           // vM 是只读输入，放 ymm 寄存器
  [L]   "x" (vL),
  ...
```

### 8-wide：每次循环处理 8 个值

16 个 YMM 寄存器的预算：

| 寄存器 | 用途 |
|--------|------|
| 5 个常量 | M、L、MASK、SEARCH、STEP |
| 2 个 base | `b0`、`b1`（各装 4 个候选值） |
| 6 个临时 | `t0..t5` |
| 共 13 个 | 还剩 3 个余量 |

实际代码里我们同时维护两份计算管线（一份处理 `b0`，一份处理 `b1`），用同一组常量做 SIMD，**单次循环 8 个候选值同时算 B**。

### `vmovd %x[b0], %[tmp]` 的小坑

`b0` 的类型是 `__m256i`，gcc 给它分配的是 YMM 寄存器。但 `vmovd` 只接受 XMM 操作数。`%x[b0]` 中的 `x` 是操作数修饰符，意思是"用 XMM 别名"（即 ymm0 的低 128 位 = xmm0）。没有这个修饰符会报 `operand type mismatch`。

---

## 六、命中处理的小技巧：延迟反解

### 问题

命中时我们要 `printf`，但 `printf` 不是线程安全的、也不是异步信号安全的，直接在 asm 循环里 `call printf` 会很复杂（要保存所有 YMM 寄存器、设置调用约定等）。

而且命中极稀疏——2³² 个值里只有 3 个命中。为这 3 次命中，热循环里塞一堆指令太亏了。

### 解法：延迟反解

asm 循环里**不立刻处理命中**，只往私有 `hits[]` 缓冲里写一条 12 字节记录：

```c
typedef struct {
    unsigned int i_start;   // 当前迭代的 i+0
    unsigned int mask_lo;   // b0 的 vmovmskps 掩码
    unsigned int mask_hi;   // b1 的 vmovmskps 掩码
} hit_t;
```

asm 内部命中分支只要写 3 个 32 位数：

```asm
"vmovd %x[b0], %[tmp]\n\t"        // tmp = i+0
"mov  %[tmp],  0(%[hp])\n\t"      // hits[hcount].i_start = i+0
"mov  %[mlo],  4(%[hp])\n\t"      // hits[hcount].mask_lo = mlo
"mov  %[mhi],  8(%[hp])\n\t"      // hits[hcount].mask_hi = mhi
"add  $12,     %[hp]\n\t"         // hptr 前进
"incl %[hc]\n\t"                  // hcount++
```

热循环跑完之后，由 C 代码遍历 `hits[]`，根据掩码位反解具体命中的是 `i+0`、`i+1`、...、`i+7` 中的哪一个，再调 `printf`：

```c
for (int k = 0; k < hcount; k++) {
    unsigned int i0 = hits[k].i_start;
    if (hits[k].mask_lo & 0x02u) report(t, i0 + 0);   // lane 0 命中
    if (hits[k].mask_lo & 0x08u) report(t, i0 + 1);   // lane 1
    if (hits[k].mask_lo & 0x20u) report(t, i0 + 2);   // lane 2
    if (hits[k].mask_lo & 0x80u) report(t, i0 + 3);   // lane 3
    if (hits[k].mask_hi & 0x02u) report(t, i0 + 4);   // b1 lane 0
    if (hits[k].mask_hi & 0x08u) report(t, i0 + 5);
    if (hits[k].mask_hi & 0x20u) report(t, i0 + 6);
    if (hits[k].mask_hi & 0x80u) report(t, i0 + 7);
}
```

**好处**：

- 热循环只有命中分支那几条指令，绝大多数迭代完全不进这分支
- `printf` 调用、锁操作都搬到循环外
- `hits[]` 容量 64 足够（实际命中只有 3 个）

---

## 七、性能对比

| 版本 | 24 线程耗时 | 加速比 | 备注 |
|------|------|--------|------|
| 基线（单线程） | ~13 s | 1× | `SearchRandom.c` |
| + pthread 多线程 | 1.18 s | 11× | 区间划分 + 互斥锁 |
| + AVX2 intrinsic | 0.86 s | 15× | 4 路 SIMD，被 -O0 栈访问拖累 |
| + 内嵌汇编 8-wide | **0.107 s** | **121×** | 全寄存器化，热循环 ~20 条指令 |

最优配置 64 线程时能跑到 **0.086 s**，加速比约 **151×**。

---

## 八、进一步思考

### 为什么 24 线程不是 24× 加速？

理论上线程数等于核数应该接近线性加速。实际加速只有 ~11× 的原因：

1. **pthread 创建/销毁开销**：每个线程大约几十微秒，24 个线程就是毫秒级开销
2. **缓存一致性**：多核读写共享变量（比如全局命中计数器、`stdout` 缓冲）会有缓存同步开销
3. **OS 调度**：线程在核之间漂移会导致缓存失效

### 为什么不再多线程反而更慢？

超过物理核数（24）后，多个线程会争抢同一个核，加上上下文切换开销，速度反而下降。但我们的实验里 32/48/64 线程反而比 24 稍快——因为总耗时只有 ~0.1 秒，单个线程的工作量很少，更多线程分摊了 chunk 边界的不均衡。

### AVX-512 有没有用？

i9-14900HX 在硬件上支持 AVX-512，但 Intel 在消费级 Raptor Lake 上**禁用了 AVX-512**（通过微码 + BIOS）。所以这台机器只有 AVX2 可用。

### 算法上能不能不穷举？

不能。`GenerateRandomNumber` 实质是 `x = h*M + l (mod 2^64)` 然后拆高低位，这是一种轻量级哈希。哈希函数的设计目标就是单向不可逆，没有数学捷径。即使能逆，反查表需要的存储（2³² × 8 字节 = 32 GB）也不现实。所以穷举 + 并行加速是唯一可行路径。

---

## 九、参考代码结构

`Thread.c` 的整体结构：

```
main()
├── 解析命令行参数（线程数）
├── 检查 CPU 是否支持 AVX2
├── 计算每个线程的区间 [begin, end)
├── pthread_create 起 N 个 worker_opt
├── pthread_join 等所有线程结束
└── 汇总耗时和命中数

worker_opt()                       ← 每个线程入口
├── 初始化常量向量 vM, vL, vMSK, vS, vSTP
├── 初始化 base 向量 b0, b1
├── 内嵌汇编热循环              ← 真正干活的地方
│   ├── SIMD 算 8 个 l2
│   ├── vmovmskps 提掩码
│   ├── 命中时往 hits[] 写 12 字节
│   └── b0 += 8, b1 += 8, 循环
├── process_hits() 反解命中记录，调 printf
└── 标量尾部处理 < 8 个剩余值
```

---

## 十、动手建议

如果你想自己改这个程序试试：

1. **改 SEARCH_VAL**（在 `main` 里）看看能不能搜出别的密码
2. **改线程数**重新编译跑 `./Thread 16`，对比性能
3. **去掉一段 SIMD 看看退化多少**：比如把第二组 b1 的处理删掉，只剩 4-wide，看耗时变化
4. **加 -O2 编译**（虽然违反 pptx 要求）观察 gcc 自己优化能达到什么程度

读完这份文档之后，建议你回去对照源码读一遍，把每个关键点和这里讲的对应起来。源码里的注释也写得比较详细。
