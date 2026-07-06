# Malloc Lab 优化日志 — 2026-07-06

> 起点: commit `eb268f3` 跑 96/100 (util 92.78%, throughput 67K Kops)
>
> 终点（暂定）: commit `e5c94b6` 跑 96/100 (util 93.85%, +1.07%)

---

## 1. 现状审计

### 1.1 评分公式回顾

```
score = p1 * 100 + p2 * 100
p1 = 0.6 * avg_mm_util
p2 = 0.4 * min(1, avg_mm_throughput / 500E3)
```

- `avg_mm_throughput = total_ops / total_secs`，单位 ops/sec
- 当前 throughput ~67M ops/s ≫ 500K 阈值，**p2 恒满分为 40**
- 所以**唯一杠杆是 util**：每 +1.67% util = +1 分

### 1.2 trace 列表变化

`traces/TRACE_LIST.txt` 从 9 条扩展到 55 条（lab10 分支）。commit log 里的"99 分"
针对旧 9 条；扩展后实测 96 分。

### 1.3 低 util trace 分布（baseline 16-byte 对齐）

| trace | 类型 | util | 主要 size 分布 |
|-------|------|------|---------------|
| 16 exhaust | 1001a+500f+1a+502f × 100 轮 | 60.7% | 48-80 byte + 29696 BIG |
| 47 seglist | 3932 a + 1966 f + 398 大块 a | 62.3% | 32 byte 主导 |
| 48/49 short1-bal/short1 | 12 ops | 66.5% | 2040, 4072 |
| 7 boat | 41K a + 16K f | 65.0% | 12, 20, 1024 |
| 31 mutt | 大量小 alloc | 81.7% | 2-16 byte |
| 41/42 realloc2 | 块 0 反复 realloc +5 | 87.3% | 4092 → ~28000 |

---

## 2. 已尝试方案与结果

### ✅ 方案 A1: 8-byte alignment（已合入，commit `e5c94b6`）

**改动**:
- `MIN_BLOCK = 16` 不变（free 块需要 16 字节放两个指针）
- 块对齐从 16 改为 8（`ALIGN_MASK = 7`）
- side-table 索引 `>> 4` 改为 `>> 3`
- `TABLE_SIZE = MAX_HEAP / 8`（10MB BSS，不计入 heap）

**效果**:
- boat.rep: 65% → 77%（+12%）。size 20 的 asize 从 32 降到 24，24726 块 × 8 = 节省 197KB
- mutt.rep: 82% → 87%（+5%）
- chrome.rep: 86% → 87%
- **总体 util 92.78% → 93.85%（+1.07%）**
- score 因取整仍为 96，但 p1 实际从 55.67 涨到 56.31

### ❌ 方案 A2: 激进 split (`split_threshold = MIN_BLOCK`)

把原版 `asize >> 4` 阈值改成常量 16。原版对 asize<256 已用 MIN_BLOCK，所以**几乎无变化**。
保留了这个改动（更简洁），但分数不变。

### ❌ 方案 A3: first-fit + LIFO free list

把 `find_fit` 从 best-fit（扫整条链找最小够用）改为 first-fit（第一个够用就返回）。
throughput 提升但 util 掉（92.78% → 91.77%）。**回退**。

### ❌ 方案 A4: address-ordered first-fit

`insert_free` 按地址升序插入，配合 first-fit。util 略升（91.77 → 92.50），但插入 O(n)
让 throughput 暴跌（73K → 5K）。**回退**。

### ❌ 方案 A5: 大块 split_threshold = max(MIN_BLOCK, asize)

让大块分配不切出小 remainder。结果 util 反而掉（92.78 → 91.55），因为 internal
fragmentation 上升。**回退**。

### ❌ 方案 A6: end-placement（place 时 alloc 块放 free block 末尾）

让 high-address alloc 块方便后续 coalesce。**Segfault**，原因未定位（怀疑
mm_malloc 的 extend+coalesce 路径仍是 start-place，不一致触发 bug）。**回退**。

### ❌ 方案 A7: realloc fallback 优先 extend 到堆顶

`asize > 1024` 时 fallback 不走 mm_malloc，直接 mem_sbrk。意图让 growing block 上堆顶，
后续 realloc 走 Level 3 sbrk in-place。**对 realloc2.rep 无效**（trace 一开始就 fallback，
extend 已经在堆顶；问题在 initial fallback 留下 heap_lo 处 4096 字节死区）。**回退**。

### ❌ 方案 A8: 禁用 pow2 rounding

`round_payload_pow2` 直接返回 payload。结果 util 91.77%（掉 1%）。binary 类 trace
依赖 pow2 rounding 达到 100%。**回退**。

---

## 3. 关键诊断：exhaust.rep 60.7% 的根因

### 3.1 trace 模式
每轮 2004 ops：
1. op 1-1001: 1001 个 size 48-80 的小块 alloc
2. op 1002-1501: 500 个 free（odd idx）
3. op 1502: 1 个 BIG（size 29696）alloc
4. op 1503-2003: 501 个 free（even idx）
5. op 2004: free BIG

100 轮，total 200,400 ops。

### 3.2 为什么 util 只有 60.7%

测得 `max_total_size = 94744`（peak alive payload）, `mem_heapsize = 156096`。

`heap = 156096` 的来源：
- 第 1 轮: heap 涨到 `97008 (small sum) + 29696 (BIG) = 126704`
- 第 2 轮 small asize sum = 97024（每轮随机），比第 1 轮多 320 字节
- 第 2 轮 BIG 来时 free 块只剩 `126704 - 97024 = 29680 < 29696`，**放不下**
- 触发 BIG extend，heap += 29696，达到 156096（实际经 coalesce 略减）
- 后续轮次 heap 足够大，不再 extend

### 3.3 解决思路（均未实施）

- **per-round 方差最大 1920 字节**（min 124064, max 125896）。buffer 需要 ≥ 1920。
- 想法 1: extend 大块时 +2KB buffer。代价：其他 trace 浪费 2KB。
- 想法 2: 检测循环模式并预保留。实现复杂。
- 想法 3: buddy allocator 让相邻同 size free 合并。重写。

---

## 4. 关键诊断：realloc2.rep 87.3% 的根因

### 4.1 trace 模式
```
a 0 4092    # 块 0 在 heap_lo
a 1 16      # 块 1 紧跟其后
r 0 4097    # 块 0 需要 asize 4112，原位置只有 4096
a 2 16
f 1
r 0 4102    # asize 还是 4112 (Level 1 in-place)
...
```

### 4.2 heap 增长来源
- 第一次 `r 0 4097` 触发 fallback（Level 2-5 都失败）→ mm_malloc → extend。
  块 0 搬到 heap_lo+4112，旧位置 heap_lo..heap_lo+4096 变 free。
- 后续 r 走 Level 1（同 asize）或 Level 3（堆顶 sbrk）。
- 每次 asize 跨 16 字节边界（每 3 次 r）触发 Level 3 sbrk 16 字节。

heap 最终 = 4112（initial extend）+ 1500 × 16（sbrk 累计）= 32208。
max_total = 28119。util = 28119 / 32208 = 87.3%。

### 4.3 为什么无法改进
heap_lo 处 4096 字节死区（旧块 0 位置）只能被小块 alloc 复用 ~32 字节，
其余 4064 字节永久浪费。除非允许 compact（move 块），无法回收。

---

## 5. 下一步候选

按 ROI 排序：

| 优先级 | 方案 | 预期 util 增量 | 风险 |
|-------|------|--------------|------|
| 中 | 修 end-placement 调通 | short1 +30%, coalescing 类提升 | 需要全面改 mm_malloc/mm_realloc 的 split 路径 |
| 中 | extend 大块 +2KB buffer | exhaust +20% | 其他 trace 略微浪费 |
| 低 | buddy allocator 重写 | 多 trace +5-10% | 大工程 |
| 低 | 更细 size class | 部分中 trace +2% | 复杂度增加 |

---

## 6. 复现命令

```bash
# 完整跑分
make && ./malloc traces/TRACE_LIST.txt

# 单 trace 调试（在 traces/ 目录下）
cd traces && ../malloc -f exhaust.rep

# 加 [util] trace#N: max_total=.. heap=.. util=.. 日志
# 修改 mdriver.c:992 附近 eval_mm_util 的 return 之前加 fprintf
```
