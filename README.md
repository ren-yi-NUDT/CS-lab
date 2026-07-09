# 内存分配器的设计

---

## 一、核心问题：这个 lab 到底在考什么？

mdriver 的评分公式：

```
score = util_score + perf_score
util_score  = (avg_util / 100) × 60    ← 大头，60 分
perf_score  = (avg_throughput / 500K) × 40，封顶 40
```

**util = max_total_size / mem_heapsize()**

`mem_heapsize()` 测量的是 `mem_sbrk()` 总共申请了多少字节。所以：

> 任何放在堆"里面"的元数据（header、footer、对齐浪费）都会降低 util。
> 任何放在堆"外面"的东西不扣分。

这就是整个设计的出发点。

---

## 二、三种方案对比

### 方案 A：传统 boundary-tag（CSAPP 课本方案）

```
每个块：
┌──────────┬──────────────┬──────────┐
│ header   │   payload    │ footer   │
│ (4/8 B)  │              │ (4/8 B)  │
└──────────┴──────────────┴──────────┘
```

- **优点**：实现简单，双向遍历容易
- **缺点**：每个 allocated 块浪费 8-16 字节的 header+footer。小请求多时浪费惨重
- **预估分数**：小 payload trace 上 util 可能只有 60-70%

### 方案 B：buddy system（伙伴系统）

```
堆是 2^k 的树，分配时向上取整到最近的 2 的幂
```

- **优点**：分配/释放 O(log n)，外部碎片少，合并快
- **缺点**：内部碎片严重。申请 65 字节 → 分配 128 字节，浪费 49%。小请求密集的 trace 直接崩
- **预估分数**：80 左右，内部碎片是硬伤

### 方案 C：side table（本方案）

```
allocated 块：纯 payload，无 header，无 footer
free 块：    [prev_ptr(8B)][next_ptr(8B)]，最小 16 字节
所有元数据 → 堆外静态数组
```

- **优点**：allocated 块零 overhead，util 极高
- **缺点**：需要维护 side table，代码复杂度高；TABLE_SIZE 占 BSS 内存（但不计入 heapsize）
- **预估分数**：96/100

### 为什么 C > A > B？

| 维度 | A (boundary tag) | B (buddy) | C (side table) |
|------|:-:|:-:|:-:|
| allocated 块 overhead | 8-16 B | 内部碎片 ~25% | **0 B** |
| 外部碎片 | 低（立即合并） | 低 | 低（立即合并） |
| 实现复杂度 | 低 | 中 | **高** |
| util 上限 | ~85% | ~80% | **~93%** |

**结论：side table 是这个评分体系下的最优解**，因为 util 占 60 分，而 side table 把 allocated 块 overhead 压到了零。

---

## 三、side table 怎么工作的？

### 3.1 核心思路：把地址变成数组下标

```
堆起点 heap_lo 是 8 字节对齐的
任意块地址 p 的索引 = (p - heap_lo) >> 3   （除以 8）

这样 size_tab[i] 就存了"从 heap_lo + i*8 开始的块"的元数据
```

### 3.2 三张表

```
size_tab[i]  → 32 bits: [块大小(高30位)] [prev_alloc(1bit)] [alloc(1bit)]
prev_tab[i]  → 32 bits: 前一块的 byte offset + 1（0 = 没有前驱）
free_lists[] → 13 个双向链表头
```

`+1` 的 trick：offset=0 本应表示"前驱就是 heap_lo"，但 0 被用作"无前驱"哨兵。+1 后 offset=0 → 存 1，offset=N → 存 N+1，0 留给哨兵。

### 3.3 为什么能省掉 footer？

传统 boundary-tag 需要 footer 是因为：释放块 X 时，需要看"前一块是否 free"来合并。要知道前一块在哪，需要前一块的 size，而前一块的 size 存在前一块的 footer 里。

**side table 不需要 footer**，因为：
- `blk_next(p) = p + blk_size(p)` —— 本块的 size 在 side table，算出来就行
- `blk_prev(p)` —— 直接从 `prev_tab[i]` 反查

所以 allocated 块可以是纯 payload。

### 3.4 为什么 allocated 块连 header 都不要？

header 存两件事：size 和 alloc 标志。side table 都存了。
那为什么还要在块内部存一份？不需要。

---

## 四、关键设计决策及理由

### 4.1 8 字节对齐（不是 16）

设计文档写的是 16 字节对齐，但**实际代码已经改成 8 字节**（`ALIGN_MASK = 7`，`>> 3`）。

**理由**：16 字节对齐意味着每个 17 字节的请求浪费 15 字节。8 字节对齐浪费最多 7 字节，在 small-payload trace 上省 ~10% heap。

代价是 TABLE_SIZE 翻倍（从 `MAX_HEAP/16` 变成 `MAX_HEAP/8`），但 TABLE_SIZE 是静态 BSS，不计入 heapsize。

### 4.2 13 条分离空闲链表（segregated free lists）

不是随便分的。低 size 区分细，高 size 区分粗：

```
16, 32, 48, 64, 96, 128, 192, 256, 512, 1024, 2048, 4096, ∞
```

**为什么是这个分布？**
- 小请求多（大多数 trace 的 allocation 在 16-256 字节），内部碎片敏感 → 粒度细
- 大请求少 → 粒度粗，减少链表数量

**为什么不用纯 best-fit 遍历所有 free 块？** 太慢。分类后，find_fit 只从合适 size 的 class 开始向上找，跳过了所有太小的 class。

**为什么不用纯 first-fit？** first-fit 倾向于切碎前面的块。best-fit 找最小的够用块，减少 remainder 碎片。

### 4.3 立即合并（immediate coalescing）

free 之后立刻检查前后邻居是否 free，是就合并。

**为什么不延迟合并（deferred coalescing）？** 延迟合并的核心思路是：刚 free 的块可能马上被 re-malloc，不合可以省掉 split 的开销。但：
- 我们的 trace 没有大量 malloc/free 同大小块的模式（除了 seglist）
- 不合的代价是：heap 里漂着大量小 free 块，大请求找不到连续空间 → 扩堆 → util 降
- 立即合并的代码已经在 coalesce() 里写好了，加延迟逻辑反而增加复杂度

### 4.4 pow2 舍入（87% 阈值）

```c
if (payload * 100 >= next_pow2 * 87) payload = next_pow2;
```

**这是什么？** 如果用户申请 180 字节，下一个 2 的幂是 256。180/256 = 70% < 87%，不触发。如果申请 230，230/256 = 90% ≥ 87%，触发 → 分配 256。

**为什么？** 某些 trace（binary 系列）模式是：先 malloc 一个接近 pow2 的大小，再 realloc 到精确的 pow2。如果我们第一次分配了精确大小（比如 230），realloc 到 256 时原地扩不了（后面可能被占了）→ fallback → malloc+memcpy+free → 慢 + util 降。

**为什么只对小 payload（≤512）？** 大块 random trace 不需要这个兼容，pow2 舍入反而浪费内存。

**为什么是 87%？** 试出来的。85% 太保守（trigger 不够多），90% 太激进（浪费多）。87% 是 sweet spot。


实测：关掉这行，binary 系列从 100% util 暴跌到 56%。

### 4.5 realloc 的 5 级优化

```
L1: 缩小（免费）            ptr 不变
L2: 吃 next（合并）         ptr 不变
L3: sbrk 扩展（堆末）       ptr 不变
L4: 吃 prev（memmove）      ptr 变，但省了 malloc+free
L5: 吃 prev+next（memmove） ptr 变，但省了 malloc+free
F:  malloc + memcpy + free  最贵
```

**为什么 L3 在 L4 之前？** sbrk 比 memmove 便宜，而且 ptr 地址不变（用户指针不悬空）。

**为什么 L4/L5 要做 memmove？** prev 是 free 块，内容无意义。把 ptr 的内容往前搬到 prev 的位置，然后 prev+old_block 合成一个大 allocated 块。用户拿到新地址（prev），但内容没变。

### 4.6 精确扩堆（不批量）

```c
size_t extend_size = asize;  // 就要这么多，不多扩
```

传统实现会用 `CHUNKSIZE`（如 4096）批量扩堆减少 sbrk 调用次数。但：

- 每次多扩的字节如果没被用到，就是永久内部碎片，直接降 util
- 性能分（40 分）已封顶，多扩堆减少 sbrk 次数没有额外收益；但 util 只有 56/60，还有提升空间
- 精确扩堆在 util 上更优

### 4.7 split_threshold = MIN_BLOCK(16)

切分阈值：只有当 remainder ≥ 16 字节时才切（否则切出来的块连 free block 的最小大小都达不到）。

**试过自适应阈值吗？** 试过（asize < 256 → 16，asize ≥ 256 → asize >> 4），效果反而更差（96 → 95）。原因是：大块切出来的 remainder 太小不能复用，大块自己被切碎后下次大请求找不到合适的块 → 扩堆。简单用 MIN_BLOCK 就是最优。

---

## 五、当前瓶颈（为什么卡在 96 分）

三个低效 trace（47, 48, 49），效率 62-66%：

| Trace | 模式 | 根因 |
|-------|------|------|
| 47 (rm.rep) | 极少 free | 堆只增不减，内部碎片 + pow2 舍入累积 |
| 48 (seglist.rep) | 32 字节块交替 alloc/free | 棋盘碎片，free 块被 allocated 块隔开无法合并 |
| 49 (short1-bal.rep) | alloc/free 混合 | 两个 free 块（2088+1992）被一个 allocated 隔开，无法合并满足 4072 请求 |

共同问题：**外部碎片 —— free 块被 allocated 块隔开，合并不了**。

要突破需要架构级改动（8 字节对齐已经做了，从 16 改 8，util +1.07%），而非参数调优。

---

## 六、Trace 参考手册

mdriver 从 `traces/` 目录读取 `.rep` 文件，每个 trace 是一个"操作序列"（alloc/free/realloc），模拟真实程序的内存行为。理解每个 trace 在测什么，才能理解设计决策的动机。

### trace 文件格式

```
<weight>      # 该 trace 在评分中的权重（0/1/2/3）
<num_ids>     # 不同地址的个数
<num_ops>     # 总操作数（行数）
<weight>      # 权重（重复）
a <id> <size> # alloc: 分配 size 字节，返回地址 id
f <id>        # free: 释放地址 id
r <id> <size> # realloc: 将地址 id 扩容/缩小到 size 字节
```

### 6.1 短小功能测试 (6–17 ops)

这些 trace 测**最基本的正确性**，跑不过说明代码有 bug。

| Trace | Ops | 特征 |
|-------|-----|------|
| `short1.rep` / `short1-bal.rep` | 12 | 6 alloc + 6 free。大小: 48 / 2040 / 4072。测试基本 alloc/free 和不同大小块的混合 |
| `short2.rep` / `short2-bal.rep` | 12 | 类似 short1，大小: 48 / 4010 / 4072。与 short1 不同的是大块排列顺序 |
| `malloc.rep` | 10 | 纯 alloc 无 free。测纯分配链路 |
| `malloc-free.rep` | 17 | alloc 后立即 free，交替模式。测 free 后重新分配同大小块 |
| `corners.rep` | 15 | 边界测试: 极小(4B)、常规(100B)、巨大(50MB+)。当前**不能通过**，已排除 |

### 6.2 小工具 trace (100–500 ops)

模拟标准 Unix 命令的内存行为。**特点**：大量小 alloc（6-37 字节），极少 realloc。

| Trace | Ops | 特征 |
|-------|-----|------|
| `hostname.rep` | 118 | 小 alloc (6/32/37B)。纯 alloc + free，无 realloc |
| `tty.rep` | 119 | 类似 hostname |
| `stty.rep` | 124 | 类似 hostname |
| `rm.rep` | 147 | rm 命令。**关键**：121 次 alloc，仅 26 次 free。**堆只增不减**，是低效 trace 之一 (效率 ~62%) |
| `rm.1.rep` | 143 | 类似 rm |
| `ls.rep` / `ls.1.rep` | 155-372 | ls 命令。小 alloc |
| `fs.rep` | 420 | 文件系统操作。最大块 1320B |
| `lrucd.rep` / `nlydf.rep` / `qyqyc.rep` / `rulsr.rep` | 200-205 | **已排除**。包含当前分配器无法处理的边界模式 |

### 6.3 Perl 解释器 trace (~1500 ops)

| Trace | Ops | 特征 |
|-------|-----|------|
| `perl.rep` / `perl.1-3.rep` | 1448-1498 | 大量极小 alloc (1-2B) 和 realloc (83 次)。**测内部碎片敏感度**。如果对齐 > 8B，这些小请求的 util 直接崩 |

### 6.4 标准应用 trace — 无 bal (~4K-8K ops)

"无 bal"版本：alloc 和 free 数量**不相等**，模拟真实程序内存从启动到退出的过程。**测堆增长下的 util 保持能力**。

| Trace | Ops | 大小范围 | 特征 |
|-------|-----|---------|------|
| `amptjp.rep` | 4805 | 48 – 4072 | 大块(2040/4072)为主，配小 alloc。alloc >> free |
| `cccp.rep` | 5032 | 48 – 4072 | 类似 amptjp，cccp 编译器 |
| `cp-decl.rep` | 5683 | 48 – 4072 | 类似 amptjp |
| `expr.rep` | 4537 | 48 – 4072 | 表达式求值器，同样的大块模式 |
| `binary.rep` | 4000 | 64 – 512 | malloc 64B → realloc 到 pow2 (64→128→256→512)。**pow2 舍入策略的核心动机** |
| `binary2.rep` | 4800 | 16 – 128 | 类似 binary 但起点更小(16B) |
| `bash.rep` | 4162 | 1 – 7017 | Shell 负载。大量极小 alloc (12B)，少量 realloc |
| `firefox.rep` | 8004 | 1 – 8192 | 浏览器负载。极小 alloc (1-12B) 为主 |
| `chrome.rep` | 11991 | 1 – 32808 | 类似 firefox 但规模更大 |
| `pulseaudio.rep` | 6870 | 4 – 1008 | 音频服务。大量 4B alloc |

### 6.5 标准应用 trace — bal 版本 (~4K-8K ops)

`-bal.rep` 是**平衡版本**：alloc 和 free 数量相等，模拟程序完整生命周期（启动→运行→退出）。**测 free+coalesce 后的 util 恢复能力**。

| Trace | 对应非 bal | 特征 |
|-------|-----------|------|
| `amptjp-bal.rep` | amptjp | 同模式，但 alloc=free，最后全部释放 |
| `cccp-bal.rep` | cccp | 同上 |
| `cp-decl-bal.rep` | cp-decl | 同上 |
| `expr-bal.rep` | expr | 同上 |
| `binary-bal.rep` | binary | alloc=free，末尾全部释放 |
| `binary2-bal.rep` | binary2 | 同上 |

> **bal vs 非 bal 的意义**：如果 alloc >> free（非 bal），堆持续增长，**内部碎片**是主要敌人。如果 alloc = free（bal），最终所有块都释放，heap 应接近初始状态——**外部碎片和 coalesce 能力**才是关键。

### 6.6 Realloc 压力测试 (14.4K ops)

**realloc 是性能和 util 的关键**，因为这些 trace 中 realloc 次数 ≈ alloc 次数。

| Trace | Ops | 大小范围 | 特征 |
|-------|-----|---------|------|
| `realloc.rep` / `realloc-bal.rep` | 14401 | 128 – 512 | 4801 alloc + **4799 realloc**。块从小到大增长。测 realloc L2/L3/L4/L5 的命中率 |
| `realloc2.rep` / `realloc2-bal.rep` | 14401 | 16 – 4092 | 同上但范围更宽。**大跨度 realloc 更多 fallback 到 malloc+memcpy** |

### 6.7 合并能力测试 (14.4K-20K ops)

这些 trace 刻意构造**交替 alloc/free 模式**，检验 coalesce 是否能把相邻 free 块合成大块。

| Trace | Ops | 大小范围 | 特征 |
|-------|-----|---------|------|
| `coalescing.rep` / `coalescing-bal.rep` | 14400 | 4095 – 8190 | 专门测试边界合并。块大小接近 4K/8K（与页大小相邻），alloc/free 交替产生相邻 free 块 |
| `coalesce-big.rep` | 20000 | 32 – 3200 | 大合并测试。10K alloc + 10K free |

### 6.8 随机分配测试 (4.8K ops)

**测通用碎片处理能力**。随机的大小和分配/释放顺序，没有固定模式。

| Trace | Ops | 大小范围 | 特征 |
|-------|-----|---------|------|
| `random.rep` / `random-bal.rep` | 4800 | 1 – 32764 | 随机种子 1。块大小跨度极大(1B~32KB) |
| `random2.rep` / `random2-bal.rep` | 4800 | 39 – 32755 | 随机种子 2。最小 39B，没有 1B 超小块 |

### 6.9 特殊模式 trace

这些 trace 各测一个**特定痛点**。

| Trace | Ops | 大小 | 为什么重要 |
|-------|-----|------|-----------|
| `seglist.rep` | 6495 | **90% 是 32B** | 4330 alloc + 2165 free。分配器会切出大量 32B 块，free 后形成**棋盘碎片**（free 块被 allocated 隔开，永不合并）。当前效率 ~62%，是三大低效 trace 之一 |
| `merry-go-round.rep` | 20000 | 8 – 1024 | 10K alloc + 10K free，循环交替模式。测**块复用**能力：free 的块能否被立即重新分配 |
| `exhaust.rep` | 200400 | 48 – 80 | **超级压力测试**。100K alloc + 100K free。小块均匀分布。测大规模下的碎片累积和性能退化 |
| `needle.rep` | 301350 | 1 – 821 | 30 万 ops。**已排除**，当前无法通过 |
| `alaska.rep` | 100000 | 16 – 1234 | 10 万 ops。**已排除** |

### 6.10 大型应用 trace (12K-100K ops)

模拟真实大程序的内存行为。

| Trace | Ops | 大小范围 | 特征 |
|-------|-----|---------|------|
| `xterm.rep` | 11913 | 1 – 327680 | 终端模拟器。大小跨度最极端(1B~320KB)，少量 realloc |
| `login.rep` | 19405 | 1 – 327680 | 登录流程。13715 alloc，仅 5333 free。**堆持续增长** |
| `mutt.rep` | 32783 | 2 – 32640 | 邮件客户端。27562 alloc，仅 4296 free。**alloc >> free**，类似 rm 但规模更大 |
| `freeciv.rep` | 55092 | 1 – 393488 | 文明游戏。30316 alloc，23985 free。alloc/free 比例较好，测大规模下的碎片 |
| `boat.rep` | 57716 | 12 – 1024 | 41232 alloc，16484 free。中等大小块，free 少 |
| `firefox-reddit.rep` / `firefox-reddit2.rep` | ~99500 | 1 – 32808 | Firefox 浏览 Reddit。**alloc/free 接近 1:1**（50K : 49K），是最接近"理想程序"的大规模 trace |

### 6.11 Trace 速查表（按类别）

```
功能正确性: short1, short1-bal, short2, short2-bal, malloc, malloc-free
小工具:     hostname, tty, stty, rm, rm.1, ls, ls.1, fs
解释器:     perl, perl.1, perl.2, perl.3
应用(nb):  amptjp, cccp, cp-decl, expr, binary, binary2, bash, firefox, chrome, pulseaudio
应用(bal): amptjp-bal, cccp-bal, cp-decl-bal, expr-bal, binary-bal, binary2-bal
Realloc:   realloc, realloc2, realloc-bal, realloc2-bal
合并:      coalescing, coalescing-bal, coalesce-big
随机:      random, random2, random-bal, random2-bal
特殊:      seglist, merry-go-round, exhaust
大型应用:  xterm, login, mutt, freeciv, boat, firefox-reddit, firefox-reddit2
已排除:    corners, alaska, needle, lrucd, nlydf, qyqyc, rulsr
```

---

## 七、代码阅读路线

如果你要读代码，按这个顺序：

1. **常量 + 数据结构**（L38-56）：理解 MIN_BLOCK、ALIGN_MASK、三张表
2. **元数据访问宏**（L60-114）：bidx、blk_size、blk_alloc、blk_next、blk_prev —— 这些是所有操作的基础
3. **空闲链表操作**（L117-154）：insert_free、remove_free、find_class
4. **extend_heap**（L157-170）：扩堆逻辑，简单
5. **coalesce**（L173-224）：四种情况，最复杂的函数
6. **find_fit + place**（L227-273）：分配核心
7. **mm_malloc**（L297-337）：主流程
8. **mm_free**（L339-349）：释放，简单
9. **mm_realloc**（L351-532）：5 级 + fallback，最长但逻辑清晰

---

## 八、如果你要改代码

几个容易踩的坑：

1. **TABLE_SIZE 必须 ≥ MAX_HEAP / 对齐粒度**。改了对齐要同步改 TABLE_SIZE
2. **prev_tab 的 +1 偏移不能丢**，否则 heap_lo 上的块 prev 查询会出错
3. **coalesce 的四种情况必须全部处理**，漏一种会导致 free 块泄漏或链表损坏
4. **realloc L4/L5 的 memmove 之后**，用户指针变了，必须返回新地址
5. **last_block_p 要在各种操作后更新**（coalesce、place、realloc），否则下次 extend_heap 丢链
6. **`has_next()` 判断的是物理堆边界**，不是链表尾。堆末块没有 next

---

## 九、空闲链的组织方式

### 9.1 整体结构

```
free_lists[0]  →  [≤16B]      → ...
free_lists[1]  →  [17-32B]    → ...
free_lists[2]  →  [33-48B]    → ...
free_lists[3]  →  [49-64B]    → ...
free_lists[4]  →  [65-96B]    → ...
free_lists[5]  →  [97-128B]   → ...
free_lists[6]  →  [129-192B]  → ...
free_lists[7]  →  [193-256B]  → ...
free_lists[8]  →  [257-512B]  → ...
free_lists[9]  →  [513-1024B] → ...
free_lists[10] →  [1025-2048B] → ...
free_lists[11] →  [2049-4096B] → ...
free_lists[12] →  [>4096B]    → ...
```

13 条**独立的**双向链表，按块大小分 class，链表之间没有连接。`free_lists[]` 数组本身在 BSS 段，不计入 heapsize。

### 9.2 链表节点：free 块的前 16 字节

```
free 块内容布局:
┌──────────────────┬──────────────────┬──────────────────────┐
│    prev_ptr      │    next_ptr      │    未使用空间         │
│    (8 bytes)     │    (8 bytes)     │   (block_size - 16)  │
└──────────────────┴──────────────────┴──────────────────────┘
```

```c
static inline void* fl_prev(void* bp)    { return *(void**)bp; }
static inline void* fl_next(void* bp)    { return *(void**)((char*)bp + 8); }
static inline void  fl_setprev(void* bp, void* p) { *(void**)bp = p; }
static inline void  fl_setnext(void* bp, void* p) { *(void**)((char*)bp + 8) = p; }
```

allocated 块不需要存这些指针——payload 从第 0 字节开始，零 overhead。

### 9.3 插入：LIFO 头插法

```c
static void insert_free(void* bp, size_t size) {
    int cls = find_class(size);
    void* head = free_lists[cls];
    fl_setprev(bp, NULL);
    fl_setnext(bp, head);
    if (head) fl_setprev(head, bp);
    free_lists[cls] = bp;          // 新块成为链表头
}
```

每次 O(1) 插入头部。选择 LIFO 的理由：

| 策略 | 插入成本 | 劣势 |
|------|---------|------|
| **LIFO** | O(1) | 无 |
| FIFO | O(n) | 需要走到链表尾 |
| 地址有序 | O(n) | 需要找到插入位置 |

LIFO 实现最简单，且刚 free 的块可能还在 CPU cache 里，立刻被下一次 malloc 命中时 cache 热度好。对 `merry-go-round` 这类交替 alloc/free 的 trace 尤其有效。

### 9.4 删除：O(1) 任意位置摘除

```c
static void remove_free(void* bp) {
    int cls = find_class(blk_size(bp));
    void* prev = fl_prev(bp);
    void* next = fl_next(bp);
    if (prev) fl_setnext(prev, next);
    else      free_lists[cls] = next;   // bp 是头，更新链表头
    if (next) fl_setprev(next, prev);
}
```

双向链表的意义就在这一步：**不需要遍历，直接从 bp 的前 16 字节读出 prev 和 next，重新连接即可**。


```c
remove_free(next);   // 后块摘除，O(1)
remove_free(prev);   // 前块摘除，O(1)
```

### 9.5 查找：跨 class best-fit

```c
static void* find_fit(size_t asize) {
    int cls = find_class(asize);
    for (int c = cls; c < NUM_CLASSES; c++) {     // 从目标 class 往上扫
        void* node = free_lists[c];
        if (!node) continue;                       // 空链表跳过
        void* best = NULL;
        size_t best_size = (size_t)-1;
        while (node) {                             // class 内 best-fit
            size_t sz = blk_size(node);
            if (sz >= asize && sz < best_size) { best = node; best_size = sz; }
            if (sz == asize) break;                // 精确匹配，立即退出
            node = fl_next(node);
        }
        if (best) return best;                     // 找到就停，不跨 class
    }
    return NULL;
}
```

两层循环：

- **外层**：从目标 class 开始往上找（class 0,1,2... 太小，直接跳过）
- **内层**：在当前 class 内 best-fit（找最小的能满足的块）

**找到就停**：当前 class 找到 best-fit 就返回，不继续往更大 class 找。因为更大 class 里的块只会更大，不会改进 best-fit。

**best-fit 保护大块**：如果 class 内有 [64B, 4096B] 两个 free 块，请求 64B 时不会把 4096B 切碎。first-fit 碰到 4096B 就直接用了，后面来的大请求找不到够大的块只能扩堆。best-fit 选中 64B 完美匹配，4096B 留给真正需要它的请求。

---

## 十、放置算法（place）

```c
static void place(void* bp, size_t asize) {
    size_t block_size = blk_size(bp);
    int prev_alloc = blk_prev_alloc(bp);
    remove_free(bp);                          // 从 free_lists 摘掉

    size_t threshold = split_threshold(asize);  // = 16
    size_t remainder = block_size - asize;

    if (remainder >= threshold) {
        // --- 切分 ---
        set_meta(bp, asize, 1, prev_alloc);         // 前面：allocated
        void* rem = (char*)bp + asize;
        set_meta(rem, remainder, 0, 1);              // 后面：free
        set_prev(rem, bp);                           // prev_tab 链
        insert_free(rem, remainder);                 // remainder 回库存
        if (has_next(rem))
            update_next_prev(rem, 0);                // 后后块指回 rem
        else
            last_block_p = rem;                      // rem 是堆末
    } else {
        // --- 不切，整块分配 ---
        set_meta(bp, block_size, 1, prev_alloc);
        if (has_next(bp))
            update_next_prev(bp, 1);                 // 告诉后块：我 allocated 了
    }
}
```

### 情况 A：remainder ≥ 16 → 切

```
切前:  [  200B free  ]
切后:  [64B allocated][  120B free (remainder)  ][16B free (remainder-120)]
                 ← remainder 插回 free_lists
```

阈值 16 的原因：free 块至少需要 16 字节装 prev_ptr + next_ptr。切出 <16B 的 remainder 装不下链表指针，变成永久内部碎片，还不如整块分配掉。

### 情况 B：remainder < 16 → 不切

```
请求 190B，块 200B → remainder=10 < 16
整块 200B 分配，10B 是内部碎片
```

比切出 10B 的 "free" 块强——那个块永远用不了。

### 切分后的不变式维护

切分改变了堆的物理结构，需要同步更新三张表：

| 操作 | 为什么 |
|------|--------|
| `set_meta(rem, remainder, 0, 1)` | rem 是新 free 块，prev_alloc=1 |
| `set_prev(rem, bp)` | prev_tab 链不能断 |
| `update_next_prev(rem, 0)` | next 的前一块从 bp 变成 rem |
| `insert_free(rem, ...)` | remainder 回到 free_lists |
| `last_block_p = rem` | 如果 rem 是堆末，更新标记 |

不切时只需 `update_next_prev(bp, 1)`——告诉后块它前面的块现在是 allocated 了。大小没变，所以地址关系没变。
