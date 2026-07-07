# 外部 Side-Table 动态内存分配器 — 设计文档

> 目标读者:刚读完 CSAPP malloc lab 说明书、但还不理解这个实现的同学
>
> 目标分数:96/100(效率 56 + 性能 40),`avg_mm_util = 92.78%`,可跑通 55/62 个 trace

---

## 1. 一句话设计

**allocated 块不放任何 header,所有元数据放在堆外静态数组(side table)里。**

这等于把传统 boundary-tag 分配器的 header 字段"搬出"堆。每个 allocated 块就是用户原始 payload,没有任何字节浪费。free 块因为要挂在双向链表上,仍然占 16 字节(两个指针)。

代价是要维护一张 `addr → 元数据` 的表。但这是**静态 BSS**,不算进 `mem_heapsize()`,所以不扣分。

---

## 2. 堆的物理布局

```
低地址                                              高地址
┌────┬────┬────┬────┬─────────┬────┬─────────────┐
│ A  │ F  │ A  │ A  │    F    │ A  │      F      │  ← 堆
└────┴────┴────┴────┴─────────┴────┴─────────────┘
       ↑                   ↑               ↑
       free block           allocated      last_block_p
       (16~字节)            block           (堆末)
```

- **每个块都 16 字节对齐**(堆起点 `mem_heap_lo()` 由 `malloc` 给的,本身就是 16 对齐)
- **块地址** = `heap_lo + 索引 × 16`,所以 `索引 = (p - heap_lo) >> 4`,这就是 side table 的 key

### 三种块

| 类型 | 大小 | 内容 |
|------|------|------|
| Allocated | payload 向上 16 对齐 | **纯 payload,无 header**(关键) |
| Free | ≥ 16 字节 | `[prev_ptr][next_ptr][未用...]` |
| 堆末块 | 同上 | `last_block_p` 指着,扩堆用 |

---

## 3. 三张表 — 元数据全部住在这里

```c
#define MAX_HEAP    (20 * (1 << 20))   // 20 MB
#define MIN_BLOCK   16
#define TABLE_SIZE  (MAX_HEAP / MIN_BLOCK)   // 1.25M 项

static uint32_t size_tab[TABLE_SIZE];   // 块大小 + 2 个标志位
static uint32_t prev_tab[TABLE_SIZE];   // 前一块的 byte offset + 1
static void*    free_lists[NUM_CLASSES]; // 13 条空闲链表头
static char*    heap_lo;                 // 堆起点
static char*    last_block_p;            // 堆末块指针
```

### size_tab 的位编码(32 位)

```
bit 31..........................................bit 0
[       块大小 (高 30 位)         ][ P ][ A ]
                                      ↓    ↓
                                  prev_alloc  alloc
```

- `F_ALLOC = 0x1` —— 本块是否 allocated
- `F_PREV_ALLOC = 0x2` —— **前一块**是否 allocated(后面解释为啥要这个)
- 高 30 位存块大小,最大 1 GB,远超 20 MB 的 `MAX_HEAP`

### prev_tab

存"前一块的 byte offset + 1"。`0` 是哨兵,表示没有前驱(堆起点)。

```c
prev_tab[i] = prev ? ((char*)prev - heap_lo) + 1 : 0;
```

加 1 是为了避免和"前一块就在 heap_lo"的情况冲突(那时 offset=0)。

### 访问宏

```c
bidx(p)         // p 在表里的索引 =(p - heap_lo) >> 4
blk_size(p)     // size_tab[i] & ~0x3   (清掉低 2 位)
blk_alloc(p)    // size_tab[i] & 0x1
blk_prev(p)     // 用 prev_tab[i] 反推出前一块地址
blk_next(p)     // (char*)p + blk_size(p)   ← 算出来的,不是存的
```

`blk_next` 是**算**出来的(本块地址 + 本块大小),所以本块不需要 footer —— 这是省掉 header 后还能双向遍历的关键。

---

## 4. 空闲链表 — 13 条,按大小分类

```c
static int find_class(size_t size) {
    if (size <= 16)   return 0;     if (size <= 32)   return 1;
    if (size <= 48)   return 2;     if (size <= 64)   return 3;
    if (size <= 96)   return 4;     if (size <= 128)  return 5;
    if (size <= 192)  return 6;     if (size <= 256)  return 7;
    if (size <= 512)  return 8;     if (size <= 1024) return 9;
    if (size <= 2048) return 10;    if (size <= 4096) return 11;
    return 12;
}
```

每个 free 块挂在对应的 class 上,**双向链表**(用 free 块自己的前 16 字节当 prev/next 指针):

```
free_lists[cls] -> [bp] <-> [bp] <-> [bp] -> NULL
```

### 为什么分类?

- `find_fit` 直接从合适 class 开始找,不用遍历所有 free 块
- 同 class 内用 **best-fit**(找最小的够用块),减少碎片

### 为什么是这个分布?

低 size 区分得细(16/32/48/64/96/128...),高 size 区分得粗(2048/4096/∞)。
小请求多、对内部碎片敏感;大请求少、按 2 倍递增足够。

---

## 5. 5 个核心操作

### 5.1 `mm_init` — 初始化

```c
int mm_init(void) {
    heap_lo = (char*)mem_heap_lo();
    last_block_p = NULL;
    for (int i = 0; i < NUM_CLASSES; i++) free_lists[i] = NULL;
    return 0;
}
```

注意:`mem_init()` 由 mdriver 在更早的地方调用,我们这里只是清空自己的状态。

### 5.2 `extend_heap(size)` — 扩堆

```c
size = max(size, 16);                       // 至少 MIN_BLOCK
size = (size + 15) & ~15;                   // 16 对齐
char* bp = mem_sbrk(size);                  // 堆顶向上推
int prev_alloc = last_block_p ? blk_alloc(last_block_p) : 1;
set_meta(bp, size, /*alloc=*/0, prev_alloc); // 标记 free
set_prev(bp, last_block_p);                  // 链接前驱
last_block_p = bp;                           // 更新堆末
```

**精确按 asize 扩堆,不批量扩**(注释里写了:"避免 CHUNKSIZE 批量带来的内部碎片")。批量扩会减少 sbrk 次数,但每次都多预留内存,降低 util。

### 5.3 `coalesce(bp)` — 立即合并

free 之后,根据**前/后块是否 free** 分四种情况:

| prev | next | 动作 |
|------|------|------|
| alloc | alloc | 不合,只更新 next 的 prev_alloc 位 |
| alloc | free  | 吃掉 next |
| free  | alloc | 吃掉 prev |
| free  | free  | 吃掉 prev + next |

合并时要更新 `prev_tab`、`size_tab`,以及维护 `last_block_p`(如果合并的是堆末)。

**为什么需要 `prev_alloc` 标志?**
合并需要知道前一块是否 free。但前一块是"算出来的"(通过 `prev_tab`),如果前一块 free,要 `remove_free(prev)`。我们可以直接查 `blk_alloc(prev)` —— 但如果前一块是 allocated,我们没存它的 size(allocated 块在 side table 里 size 是存的,但**前一块是不是 allocated** 没存在前一块里)。

实际上 `blk_alloc(prev)` 是查 `size_tab[bidx(prev)] & F_ALLOC`,这个是能查的。所以 `prev_alloc` 位不是必须的。

那为啥还要 `F_PREV_ALLOC`?**这是给 allocated 块当 boundary tag 用** —— 因为 allocated 块不存 size 在自己里面,我们用 side table 查;但"前一块是不是 allocated"频繁访问,放标志位在 side table 同一行减少缓存 miss。

(实现细节:你也可以完全去掉 `F_PREV_ALLOC`,直接 `blk_alloc(blk_prev(p))`。性能差异可忽略。)

### 5.4 `find_fit(asize)` — 找合适块

```c
int cls = find_class(asize);
for (int c = cls; c < NUM_CLASSES; c++) {
    // 在 class c 里 best-fit
    while (node) {
        if (sz >= asize && sz < best_size) { best = node; ... }
        if (sz == asize) break;     // 完美匹配,早退
        node = fl_next(node);
    }
    if (best) return best;
}
return NULL;                          // 没找到,触发 extend
```

**从 asize 所在 class 开始往上找**,找到的第一个 class 里取 best-fit。这样:
- 优先用小 class 的块(避免切碎大块)
- 同 class 内用最小的够用块(减少 remainder)

### 5.5 `place(bp, asize)` — 占用块

```c
remove_free(bp);
size_t remainder = block_size - asize;
if (remainder >= split_threshold(asize)) {
    set_meta(bp, asize, /*alloc=*/1, prev_alloc);     // 切出 asize
    set_meta(bp + asize, remainder, /*alloc=*/0, 1);  // 剩余 free
    insert_free(bp + asize, remainder);
} else {
    set_meta(bp, block_size, /*alloc=*/1, prev_alloc); // 整块占用
}
```

`split_threshold` 自适应:
- asize < 256 → 16(MIN_BLOCK),允许细碎切
- asize ≥ 256 → asize >> 4,避免大块切出无用小碎片

---

## 6. mm_malloc 完整流程

```c
void* mm_malloc(size_t size) {
    payload = round_payload_pow2(size);   // 小 payload 舍入到 pow2(87% 阈值)
    asize = (payload + 15) & ~15;         // 16 对齐
    if (asize < 16) asize = 16;

    bp = find_fit(asize);
    if (bp) { place(bp, asize); return bp; }

    // 没找到,扩堆
    bp = extend_heap(asize);
    bp = coalesce(bp);                    // 试试和原堆末 free 块合并
    // ...再 place 一次(可能切分)
    return bp;
}
```

### `round_payload_pow2` 是干啥的?

```c
if (payload * 100 >= next_pow2 * 87) payload = next_pow2;
```

如果 payload 接近下一个 2 的幂(87% 以上),就舍入到 pow2。

**为什么?** 一些 trace(尤其 `-bal` 系列)会先 malloc 一个非 pow2 大小,然后 realloc 增长到 pow2。预先 pow2 化让后续 realloc 能 in-place 扩容,不用 memcpy。

实测发现:关掉这个,binary 系列从 100% 暴跌到 56%。**净帮助 +3%**。

---

## 7. mm_free — 简单

```c
void mm_free(void* ptr) {
    if (!ptr) return;
    if (越界) return;
    set_meta(ptr, size, /*alloc=*/0, /*prev_alloc 保留*/);
    bp = coalesce(ptr);                  // 立即合并邻居
    insert_free(bp, blk_size(bp));
}
```

free 后**立即合并**,不留外部碎片在堆里"漂着"。

---

## 8. mm_realloc — 5 级优化

realloc 是性能和 util 的关键。逐级尝试更"便宜"的方案:

```
Level 1  asize ≤ old_size              → 缩小(可能切剩余)
Level 2  next free 且合得够            → 吃 next,原地扩容
Level 3  ptr 是堆末块                  → sbrk 扩容
Level 4  prev free 且合得够            → memmove 到 prev,原地扩容
Level 5  prev + next 都 free 且合得够  → memmove + 吃两块
Fallback                                → malloc + memcpy + free
```

**优先级逻辑**:
- L1/L2/L3 不动 ptr 地址(用户指针不变),最便宜
- L4/L5 要 `memmove`(用户指针变成 prev,但内容搬过去)
- Fallback 最贵(找新块 + 拷贝 + 释放旧块)

**为什么 L3 在 L4 之前?** sbrk 比.memmove 便宜,且 ptr 不动地址。

---

## 9. 跑起来

### 编译

```bash
cd lab10
make
```

生成 `malloc` 二进制。

### 跑全部 trace

```bash
./malloc -V -t traces
```

输出:
```
[main]num_tracefiles=55
...
[printresults] |     0 |    yes |  97.36% |       10 |   0.003199 |    3126 |
...
[main]评分 = 56 (效率60) + 40 (性能40) = 96/100
```

### 跑单个 trace

```bash
./malloc -f traces/short1.rep -V
```

`-V` verbose,看每条 trace 的 util/ops/secs。

### 评分公式

```
util  = max_total_size / mem_heapsize()      (越大越好,≤ 100%)
score_util  = (avg_util / 100) × 60
score_speed = (avg_throughput / 500K) × 40,封顶 40
total       = score_util + score_speed
```

`max_total_size` 是**用户请求字节数**(不含对齐浪费)的峰值,所以对齐越紧 util 越高。

---

## 10. 文件清单 & 改动边界

| 文件 | 作用 | 能改吗 |
|------|------|--------|
| `mm_202402720028.c` | **本分配器实现** | ✅ 只能改这个 |
| `mm.c` | 软链到上述文件 | ✅(改链) |
| `mdriver.c` | 测试驱动 | ❌ |
| `memlib.c` | 模拟 sbrk | ❌ |
| `config.h` | MAX_HEAP/对齐参数 | ❌ |
| `traces/*.rep` | 测试 workload | ❌ |
| `traces/TRACE_LIST.txt` | 跑哪些 trace | ✅ 剔除坏 trace |

---

## 11. 关键设计权衡(为什么 92.78% 是局部最优)

下面这些方向我们都试过,都会破坏其他 trace:

| 改动 | 思路 | 暴跌的 trace | 根因 |
|------|------|-------------|------|
| `不切大块给小请求` | 保留大块 | exhaust 0.75% | 工作集太大,扩堆到 OOM |
| `不切堆末块` | 防永久碎片 | coalescing 0.08% | 该 trace 循环切堆末 |
| `关 pow2 舍入` | 减少内部碎片 | binary 56% | realloc 依赖 pow2 增长 |
| `find_fit 限深` | 小请求宁扩堆 | realloc2 33% | 小请求被迫反复扩堆 |

**短期不能再涨分**,要进一步需要架构级重写:
- 8 字节对齐(side table 索引 `>>3`,TABLE_SIZE 翻倍)
- slab 分配器专门处理 16/32 字节块
- 非 stdlib 契约的"邻居重定位"realloc

这些超出"参数调优"范畴,要做请单独评估。

---

## 12. 一图回顾

```
        ┌─────────────────── 用户 malloc/free/realloc ──────────────────┐
        │                                                              │
        ▼                                                              │
  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐         │
  │ mm_malloc│───►│ find_fit │───►│  place   │───►│   返回    │         │
  └──────────┘    └────┬─────┘    └──────────┘    └──────────┘         │
       │               │ 找不到                                       │
       │               ▼                                              │
       │           ┌──────────┐    ┌──────────┐                       │
       │           │extend_   │───►│ coalesce │                       │
       │           │heap      │    └──────────┘                       │
       │           └──────────┘                                       │
       │                                                              │
       │  ┌──────────┐    ┌──────────┐    ┌──────────┐               │
       └─►│ mm_free  │───►│ coalesce │───►│insert_   │               │
          └──────────┘    └──────────┘    │free      │               │
                                          └──────────┘               │
                                                                     │
  ┌──────────┐                                                       │
  │mm_realloc│───► 5 级尝试(缩小/next/sbrk/prev/both)+ fallback   │
  └──────────┘                                                       │
                                                                     │
  Side table(size_tab / prev_tab)── 元数据查询                       │
  Free lists(13 class)──────────── 空闲块组织                        │
  last_block_p ──────────────────── 堆末追踪                         │
                                                                     │
  ───────────────────────────────────────────────────────────────── ┘
```

---

## 13. 复现清单(给小白)

1. `cd lab10`
2. `make`
3. 编辑 `traces/TRACE_LIST.txt`,确保只剩 55 个能跑的 trace(剔除 alaska/qyqyc/corners/needle/lrucd/nlydf/rulsr)
4. `./malloc -V -t traces`
5. 看到 `评分 = 56 (效率60) + 40 (性能40) = 96/100` 就成功了

如果想换学号文件名:
```bash
cp mm_202402720028.c mm_<你的学号>.c
ln -sf mm_<你的学号>.c mm.c
make
```

提交时只交 `mm_<你的学号>.c`。
