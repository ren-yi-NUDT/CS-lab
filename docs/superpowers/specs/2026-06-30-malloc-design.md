# Malloc Lab 极限档设计方案

**Date**: 2026-06-30
**Goal**: 在 CSAPP Malloc Lab 9 个默认 trace 上拿到 95+ 分
**Approach**: 分离空闲链表 + Header-only + 多级 realloc 优化（方案 A）
**Dev strategy**: 分 5 个阶段迭代，每阶段都能编译、能跑、能得分

---

## 1. 评分背景

总分 = `Utilization × 0.60 + Throughput × 0.40`（见 `config.h`）

- Utilization: `payload / heap_size`，越接近 1 越好
- Throughput: ops/sec，超过 500K ops/s 后不再加分
- 默认 9 个 trace（`*-bal.rep`）：`amptjp-bal`, `cccp-bal`, `cp-decl-bal`, `expr-bal`, `coalescing-bal`, `random-bal`, `random2-bal`, `binary-bal`, `binary2-bal`

约束：只能修改 `mm.c`，其他文件不能动。

---

## 2. 块格式与对齐

### 对齐
- 8 字节对齐（与 `config.h` 中 `ALIGNMENT` 一致）

### Header 编码（8 字节 size_t）
```
| 高位: size (整块大小，含 header/footer) | bit1: prev_alloc | bit0: alloc |
```

- `size`：整块字节数（已 8 字节对齐）
- `alloc`：1 = 已分配，0 = 空闲
- `prev_alloc`：1 = 前一物理块已分配（无 footer），0 = 前一物理块空闲（有 footer，可读 footer 找其 size）

### 块布局

**已分配块**（无 footer，省 8 字节）：
```
+--------+--------+--------+ ... +
| header | payload (≥ 8B)        |
+--------+--------+--------+ ... +
   8B       ≥8B
   最小 16B
```

**空闲块**（需 footer 供向后合并，需 prev/next 供链表操作）：
```
+--------+--------+--------+--------+ ... +--------+
| header |  prev  |  next  |  ...   | footer |
+--------+--------+--------+--------+ ... +--------+
   8B      8B       8B              8B
   最小 32B
```

### 关键宏
```c
#define WSIZE 8
#define DSIZE 16
#define CHUNKSIZE (1 << 12)  // 4096, sbrk 扩展单位

#define PACK(size, prev_alloc, alloc)  ((size) | ((prev_alloc) << 1) | (alloc))
#define GET(p)            (*(size_t*)(p))
#define PUT(p, val)       (*(size_t*)(p) = (val))
#define GET_SIZE(p)       (GET(p) & ~0x7)
#define GET_ALLOC(p)      (GET(p) & 0x1)
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)

#define HDRP(bp)  ((char*)(bp) - WSIZE)
#define FTRP(bp)  ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)
#define NEXT_BLKP(bp)  ((char*)(bp) + GET_SIZE((char*)(bp) - WSIZE))
#define PREV_BLKP(bp)  ((char*)(bp) - GET_SIZE((char*)(bp) - DSIZE))
```

### 边界哨兵
- **Prologue block**：16 字节，永久 allocated，紧跟在 padding 之后
- **Epilogue block**：8 字节（size=0, alloc=1），位于堆顶，每次 sbrk 时更新
- 作用：让 `coalesce` 不必特判边界

### 最小块大小
- Allocated: **16 字节**（header 8 + min payload 8）
- Free: **32 字节**（header 8 + prev 8 + next 8 + footer 8）
- Split 时保证剩余 ≥ 32 字节才能分裂

---

## 3. 分离空闲链表结构

```c
#define NUM_CLASSES 13
static void* free_lists[NUM_CLASSES];
```

### Size class 划分

| Class | 块大小范围（字节） |
|-------|-------------------|
| 0     | 16                |
| 1     | 17–32             |
| 2     | 33–48             |
| 3     | 49–64             |
| 4     | 65–96             |
| 5     | 97–128            |
| 6     | 129–192           |
| 7     | 193–256           |
| 8     | 257–512           |
| 9     | 513–1024          |
| 10    | 1025–2048         |
| 11    | 2049–4096         |
| 12    | 4097+             |

### 查 class 索引（O(1)）
```c
static int find_class(size_t size) {
    if (size <= 16)   return 0;
    if (size <= 32)   return 1;
    if (size <= 48)   return 2;
    if (size <= 64)   return 3;
    if (size <= 96)   return 4;
    if (size <= 128)  return 5;
    if (size <= 192)  return 6;
    if (size <= 256)  return 7;
    if (size <= 512)  return 8;
    if (size <= 1024) return 9;
    if (size <= 2048) return 10;
    if (size <= 4096) return 11;
    return 12;
}
```

### 链表节点
复用空闲块 payload 区，不分配额外内存：
- `prev` 指针放在 `bp` 处（payload 起点）
- `next` 指针放在 `bp + 8` 处

### 插入策略：LIFO
插到链表头部。理由：刚释放的块优先重用，缓存友好。

### 查找策略
- Class 0–5（小块）：first-fit（吞吐优先）
- Class 6–12（中大块）：best-fit（利用率优先）
- 找不到时向更大 class 扫描

### 链表操作（双向，O(1)）
```c
static void insert_free(void* bp, size_t size);  // 找 class，插到链表头
static void remove_free(void* bp);                // 用 prev/next 摘下
```

---

## 4. 关键操作

### `mm_init`
1. `mem_sbrk(4 * WSIZE)` 拿 32 字节起步空间
2. 写入：`[padding(8)][prologue header(8)][prologue footer(8)][epilogue(8)]`
3. prologue 永久 allocated（`size=16, prev_alloc=1, alloc=1`）
4. `free_lists[]` 全置 NULL
5. 视情况预 sbrk 一块 `CHUNKSIZE` 进 free list（可选优化）

### `mm_malloc(size)`
```
if size == 0 or size > MAX_HEAP: return NULL
asize = max(16, ALIGN(size + WSIZE))
cls = find_class(asize)

# 从 cls 起向上扫所有 class 找适配块
bp = find_fit(asize, cls)
if bp != NULL:
    place(bp, asize)  # 分配并视情况 split
    return bp

# 没找到，扩堆
bp = extend_heap(max(asize, CHUNKSIZE))
if bp == NULL: return NULL
place(bp, asize)
return bp
```

### `find_fit(asize, cls)`
```
for c from cls to NUM_CLASSES-1:
    if cls <= 5:  # 小块 first-fit
        for node in free_lists[c]:
            if GET_SIZE(node) >= asize:
                return node
    else:  # 大块 best-fit
        best = NULL; best_size = INF
        for node in free_lists[c]:
            sz = GET_SIZE(node)
            if sz >= asize and sz < best_size:
                best = node; best_size = sz
        if best: return best
return NULL
```

### `mm_free(ptr)`
```
if ptr == NULL: return
if ptr not in [mem_heap_lo(), mem_heap_hi()]: return  # 防御非法指针

size = GET_SIZE(HDRP(ptr))
# 标记当前块为 free
PUT(HDRP(ptr), PACK(size, GET_PREV_ALLOC(HDRP(ptr)), 0))
PUT(FTRP(ptr), PACK(size, GET_PREV_ALLOC(HDRP(ptr)), 0))
# 更新后块的 prev_alloc 位
next = NEXT_BLKP(ptr)
set_prev_alloc(next, 0)

bp = coalesce(ptr)
insert_free(bp, GET_SIZE(HDRP(bp)))
```

### `coalesce(bp)` — 4 种情况
读 `prev_alloc`（header 内位）+ 后块 alloc 位：

| Case | prev | next | 动作 |
|------|------|------|------|
| 1    | alloc | alloc | 不合并 |
| 2    | alloc | free  | 与后块合并（删后块出链表） |
| 3    | free  | alloc | 与前块合并（删前块出链表，bp 跳到前块） |
| 4    | free  | free  | 三块合一（删两个） |

每次合并都要更新 size、footer、后块 prev_alloc 位。返回合并后的块指针。

### `place(bp, asize)`
```
block_size = GET_SIZE(HDRP(bp))
remainder = block_size - asize

if remainder >= 32:  # 可以 split
    PUT(HDRP(bp), PACK(asize, prev_alloc, 1))
    # 后块（free）
    next = (char*)bp + asize
    PUT(HDRP(next), PACK(remainder, 1, 0))  # prev=已分配
    PUT(FTRP(next), PACK(remainder, 1, 0))
    insert_free(next, remainder)
    # 更新 next-next 的 prev_alloc
    set_prev_alloc(NEXT_BLKP(next), 0)
else:
    PUT(HDRP(bp), PACK(block_size, prev_alloc, 1))
    # 整块用掉，不拆
    # 更新后块的 prev_alloc = 1
    set_prev_alloc(NEXT_BLKP(bp), 1)
```

---

## 5. Realloc 5 级策略

按优先级判断，命中即返回。所有路径保证数据正确。

```
mm_realloc(ptr, size):
    if ptr == NULL: return mm_malloc(size)
    if size == 0:   mm_free(ptr); return NULL
    if ptr not in heap range:
        # 防御非法指针：当作新分配
        return mm_malloc(size)

    old_size = GET_SIZE(HDRP(ptr))
    old_payload = old_size - WSIZE  # header 大小
    asize = max(16, ALIGN(size + WSIZE))

    # ----- Level 1: 缩小或不变 -----
    if asize <= old_size:
        if old_size - asize >= 32:
            split 当前块，剩余入 free list
        return ptr  # 数据不动

    # ----- Level 2: 后块 free 且合并够 → 原地扩展 -----
    next = NEXT_BLKP(ptr)
    if next 不是 epilogue && GET_ALLOC(next) == 0:
        next_size = GET_SIZE(next)
        if old_size + next_size >= asize:
            remove_free(next)
            合并 next 到当前块
            视情况 split 剩余
            return ptr  # 数据不动

    # ----- Level 3: 后块是 epilogue（堆末）→ sbrk 扩展 -----
    if next 是 epilogue:
        need = asize - old_size
        if mem_sbrk(need) != -1:
            更新当前块 header size
            写新 epilogue
            return ptr

    # ----- Level 4: 前块 free 且合并够 → 迁移到前块 -----
    if GET_PREV_ALLOC(HDRP(ptr)) == 0:
        prev = PREV_BLKP(ptr)
        prev_size = GET_SIZE(prev)
        if prev_size + old_size >= asize:
            remove_free(prev)
            memcpy(prev + WSIZE, ptr, old_payload)  # 搬数据
            合并 prev + 当前块
            视情况 split
            return prev + WSIZE  # 新 payload 起点

    # ----- Level 5: 前后都 free 且三块合并够 → 迁移+合并 -----
    if GET_PREV_ALLOC(HDRP(ptr)) == 0
       && next 不是 epilogue && GET_ALLOC(next) == 0:
        prev = PREV_BLKP(ptr)
        prev_size = GET_SIZE(prev)
        next_size = GET_SIZE(next)
        if prev_size + old_size + next_size >= asize:
            remove_free(prev); remove_free(next)
            memcpy(prev + WSIZE, ptr, old_payload)
            合并三块
            视情况 split
            return prev + WSIZE

    # ----- Fallback: malloc + memcpy + free -----
    newptr = mm_malloc(size)
    if newptr == NULL: return NULL
    copy_size = min(old_payload, size)
    memcpy(newptr, ptr, copy_size)
    mm_free(ptr)
    return newptr
```

**关键陷阱**：
- memcpy 拷 `min(old_payload, size)`，**不要拷整个 old_size**
- "后块是 epilogue" 判定：`GET_SIZE(HDRP(next)) == 0`

---

## 6. 边界情况与错误处理

| 情况 | 处理 |
|------|------|
| `malloc(0)` | 返回 NULL |
| `malloc()` 后 `mem_sbrk` 失败 | 返回 NULL |
| `free(NULL)` | no-op |
| `free(非法指针)` | 检查 `[mem_heap_lo(), mem_heap_hi()]`，越界则 no-op |
| `realloc(NULL, size)` | 等价 `malloc(size)` |
| `realloc(ptr, 0)` | 等价 `free(ptr)`，返回 NULL |
| `realloc(非法指针, ...)` | 等价 `malloc(size)`，不读 ptr-8 |

---

## 7. 分阶段迭代开发计划

| 阶段 | 目标 | 关键修改 | 预期分数 |
|------|------|----------|----------|
| **S1** | 隐式空闲链表 | header+footer, 全堆扫描 first-fit, immediate coalesce | ~60 |
| **S2** | 显式空闲链表 | free 块加 prev/next，allocated 仍 header+footer | ~75 |
| **S3** | 分离空闲链表 | `free_lists[13]`, size class, LIFO 插入 | ~85 |
| **S4** | Header-only 优化 | 去 allocated footer, 加 prev_alloc 位 | ~90 |
| **S5** | Realloc 5 级策略 | 原地扩展 / 前后合并 | **95+** |

每阶段验证：`make && ./malloc -t traces`，分数应单调上升。
中途出 bug 回退到上一稳定版本（用 git 标签标记每阶段终点）。

---

## 8. 测试与调试

- **单 trace 详细**：`./malloc -V -t traces/xxx.rep`（打印每个 trace 的 util/perf）
- **全部 trace**：`./malloc -t traces`
- **重点 trace**：
  - `realloc*` — realloc 优化验证
  - `random-bal`, `random2-bal` — 综合负载
  - `coalescing-bal` — 合并逻辑验证
  - `binary-bal`, `binary2-bal` — 极端大小分布
- **调试辅助**：`mm_heapcheck()` 中可临时打印堆状态（mdriver 会调用此函数）

---

## 9. 提交

完成后将 `mm.c` 重命名为 `mm_<学号>.c`，提交到 educoder。仅提交 `mm.c` 一个文件。
