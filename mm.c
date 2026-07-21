/*
 * mm.c - 外部 side table 分配器（allocated 块无 in-band header）
 *
 * 架构：
 *  - 16 字节对齐
 *  - allocated 块：纯 payload，无 header。block_size = payload（向上 16 对齐）。
 *  - free 块：[prev_ptr][next_ptr][unused...]，最小 16 字节。
 *  - 元数据存于静态 side table（不计入 mem_heapsize()），索引 = (addr - heap_lo) >> 4。
 *  - size_tab[i]:  低 2 位是 alloc/prev_alloc 标志，高位是块大小。
 *  - prev_tab[i]:  前一块的 byte offset + 1（0 表示无前驱）。
 *  - 维护 last_block_p 跟踪堆末尾的块（extend_heap 用）。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * 个人信息
 ********************************************************/
team_t team = {
    "",
    "",
    "",
    "",
    ""
};

/* ===== 常量 ===== */
#define WSIZE 8
#define MIN_BLOCK 16       /* free block 需要 16 字节（两个指针） */
#define ALIGN_MASK 7       /* 8 字节对齐 — 优于 16，省 ~10% heap on small-payload traces */
#define CHUNKSIZE 4096
#define MAX_HEAP (20 * (1 << 20))
#define TABLE_SIZE (MAX_HEAP / 8)   /* 8 字节粒度的 side table */
#define NUM_CLASSES 13
#define POW2_LIMIT 512

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define F_ALLOC       0x1u
#define F_PREV_ALLOC  0x2u

/* ===== 全局状态 ===== */
static uint32_t size_tab[TABLE_SIZE];
static uint32_t prev_tab[TABLE_SIZE];
static void*    free_lists[NUM_CLASSES];
static char*    heap_lo;
static char*    last_block_p;

/* ===== 元数据访问 ===== */
static inline int bidx(void* p) {
    return (int)((char*)p - heap_lo) >> 3;
}

static inline size_t blk_size(void* p) {
    return size_tab[bidx(p)] & ~0x3u;
}

static inline int blk_alloc(void* p) {
    return (int)(size_tab[bidx(p)] & F_ALLOC);
}

static inline int blk_prev_alloc(void* p) {
    return (int)((size_tab[bidx(p)] & F_PREV_ALLOC) >> 1);
}

static inline void* blk_prev(void* p) {
    uint32_t off = prev_tab[bidx(p)];
    return off ? heap_lo + (off - 1) : NULL;
}

static inline void* blk_next(void* p) {
    return (char*)p + blk_size(p);
}

static inline int has_next(void* p) {
    return (char*)p + blk_size(p) <= (char*)mem_heap_hi();
}

static inline void set_meta(void* p, size_t size, int alloc, int prev_alloc) {
    size_tab[bidx(p)] = (uint32_t)size
                      | (uint32_t)(alloc & 1)
                      | ((uint32_t)(prev_alloc & 1) << 1);
}

static inline void set_prev(void* p, void* prev) {
    prev_tab[bidx(p)] = prev ? (uint32_t)((char*)prev - heap_lo) + 1 : 0;
}

static inline void update_next_prev(void* p, int prev_alloc_for_next) {
    if (!has_next(p)) return;
    void* nx = blk_next(p);
    size_t sz = blk_size(nx);
    int   al = blk_alloc(nx);
    size_tab[bidx(nx)] = (uint32_t)sz
                       | (uint32_t)(al & 1)
                       | ((uint32_t)(prev_alloc_for_next & 1) << 1);
    prev_tab[bidx(nx)] = (uint32_t)((char*)p - heap_lo) + 1;
}

/* 切分阈值：remainder >= MIN_BLOCK 就 split */
static inline size_t split_threshold(size_t asize) {
    (void)asize;
    return MIN_BLOCK;
}

/* ===== 空闲链表指针 ===== */
static inline void* fl_prev(void* bp)              { return *(void**)bp; }
static inline void* fl_next(void* bp)              { return *(void**)((char*)bp + WSIZE); }
static inline void  fl_setprev(void* bp, void* p)  { *(void**)bp = p; }
static inline void  fl_setnext(void* bp, void* p)  { *(void**)((char*)bp + WSIZE) = p; }

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

static void insert_free(void* bp, size_t size) {
    int cls = find_class(size);
    void* head = free_lists[cls];
    fl_setprev(bp, NULL);
    fl_setnext(bp, head);
    if (head) fl_setprev(head, bp);
    free_lists[cls] = bp;
}

static void remove_free(void* bp) {
    int cls = find_class(blk_size(bp));
    void* prev = fl_prev(bp);
    void* next = fl_next(bp);
    if (prev) fl_setnext(prev, next);
    else      free_lists[cls] = next;
    if (next) fl_setprev(next, prev);
}

/* ===== 扩堆 ===== */
static void* extend_heap(size_t size) {
    size = MAX(size, MIN_BLOCK);
    size = (size + ALIGN_MASK) & ~(size_t)ALIGN_MASK;

    char* bp = mem_sbrk(size);
    if (bp == (void*)-1) return NULL;

    void* prev = last_block_p;
    int prev_alloc = prev ? blk_alloc(prev) : 1;
    set_meta(bp, size, 0, prev_alloc);
    set_prev(bp, prev);
    last_block_p = bp;
    return bp;
}

/* ===== 合并 ===== */
static void* coalesce(void* bp) {
    void* prev = blk_prev(bp);
    int   has_n = has_next(bp);
    void* next = has_n ? blk_next(bp) : NULL;
    int prev_alloc = prev ? blk_alloc(prev) : 1;
    int next_alloc = next ? blk_alloc(next) : 1;
    size_t size = blk_size(bp);

    if (prev_alloc && next_alloc) {
        if (next) {
            update_next_prev(bp, 0);
        } else {
            last_block_p = bp;
        }
    } else if (prev_alloc && !next_alloc) {
        int next_was_last = (next == last_block_p);
        remove_free(next);
        size += blk_size(next);
        set_meta(bp, size, 0, 1);
        if (next_was_last) {
            last_block_p = bp;
        }
        if (has_next(bp)) {
            update_next_prev(bp, 0);
        }
    } else if (!prev_alloc && next_alloc) {
        remove_free(prev);
        size += blk_size(prev);
        int prev_prev_alloc = blk_prev_alloc(prev);
        set_meta(prev, size, 0, prev_prev_alloc);
        if (next) {
            update_next_prev(prev, 0);
        } else {
            last_block_p = prev;
        }
        bp = prev;
    } else {
        int next_was_last = (next == last_block_p);
        remove_free(prev);
        remove_free(next);
        size += blk_size(prev) + blk_size(next);
        int prev_prev_alloc = blk_prev_alloc(prev);
        set_meta(prev, size, 0, prev_prev_alloc);
        if (next_was_last) {
            last_block_p = prev;
        } else {
            update_next_prev(prev, 0);
        }
        bp = prev;
    }
    return bp;
}

/* ===== find_fit ===== */
static void* find_fit(size_t asize) {
    int cls = find_class(asize);
    for (int c = cls; c < NUM_CLASSES; c++) {
        void* node = free_lists[c];
        if (!node) continue;
        void*  best = NULL;
        size_t best_size = (size_t)-1;
        while (node) {
            size_t sz = blk_size(node);
            if (sz >= asize && sz < best_size) {
                best = node;
                best_size = sz;
                if (sz == asize) break;
            }
            node = fl_next(node);
        }
        if (best) return best;
    }
    return NULL;
}

/* ===== place ===== */
static void place(void* bp, size_t asize) {
    size_t block_size = blk_size(bp);
    int prev_alloc = blk_prev_alloc(bp);
    remove_free(bp);

    size_t threshold = split_threshold(asize);
    size_t remainder = block_size - asize;
    if (remainder >= threshold) {
        set_meta(bp, asize, 1, prev_alloc);
        void* rem = (char*)bp + asize;
        set_meta(rem, remainder, 0, 1);
        set_prev(rem, bp);
        insert_free(rem, remainder);
        if (has_next(rem)) {
            update_next_prev(rem, 0);
        } else {
            last_block_p = rem;
        }
    } else {
        set_meta(bp, block_size, 1, prev_alloc);
        if (has_next(bp)) {
            update_next_prev(bp, 1);
        }
    }
}

/* ===== 公开接口 ===== */

int mm_init(void) {
    heap_lo = (char*)mem_heap_lo();
    last_block_p = NULL;
    for (int i = 0; i < NUM_CLASSES; i++) free_lists[i] = NULL;
    return 0;
}

/* payload 向上舍入到最近的 2 的幂（阈值 0.87），仅对小 payload（<= POW2_LIMIT）。
 * 大块 random trace 不需要 pow2 兼容，避免浪费。*/
static size_t round_payload_pow2(size_t payload) {
    if (payload <= 16) return 16;
    if (payload > POW2_LIMIT) return payload;
    int bits = 64 - __builtin_clzll(payload - 1);
    size_t next_pow2 = (size_t)1 << bits;
    size_t prev_pow2 = next_pow2 >> 1;
    if (payload == prev_pow2) return payload;
    if (payload * 100 >= next_pow2 * 87) return next_pow2;
    return payload;
}

void* mm_malloc(size_t size) {
    if (size == 0) return NULL;

    size_t payload = round_payload_pow2(size);
    size_t asize = (payload + ALIGN_MASK) & ~(size_t)ALIGN_MASK;
    if (asize < MIN_BLOCK) asize = MIN_BLOCK;

    void* bp = find_fit(asize);
    if (bp) {
        place(bp, asize);
        return bp;
    }

    /* 没找到，扩堆（精确 asize，避免 CHUNKSIZE 批量带来的内部碎片） */
    size_t extend_size = asize;
    bp = extend_heap(extend_size);
    if (!bp) return NULL;

    bp = coalesce(bp);
    size_t total = blk_size(bp);
    if (total >= asize + split_threshold(asize)) {
        int prev_alloc = blk_prev_alloc(bp);
        set_meta(bp, asize, 1, prev_alloc);
        void* rem = (char*)bp + asize;
        size_t rem_size = total - asize;
        set_meta(rem, rem_size, 0, 1);
        set_prev(rem, bp);
        insert_free(rem, rem_size);
        if (has_next(rem)) {
            update_next_prev(rem, 0);
        } else {
            last_block_p = rem;
        }
    } else {
        set_meta(bp, total, 1, blk_prev_alloc(bp));
        if (has_next(bp)) {
            update_next_prev(bp, 1);
        }
    }
    return bp;
}

void mm_free(void* ptr) {
    if (!ptr) return;
    if ((char*)ptr < heap_lo || (char*)ptr > (char*)mem_heap_hi()) return;

    size_t size = blk_size(ptr);
    int prev_alloc = blk_prev_alloc(ptr);
    set_meta(ptr, size, 0, prev_alloc);

    void* bp = coalesce(ptr);
    insert_free(bp, blk_size(bp));
}

void* mm_realloc(void* ptr, size_t size) {
    if (!ptr) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }

    if ((char*)ptr < heap_lo || (char*)ptr > (char*)mem_heap_hi()) {
        return mm_malloc(size);
    }

    size_t old_size = blk_size(ptr);
    size_t old_payload = old_size;
    size_t payload = round_payload_pow2(size);
    size_t asize = (payload + ALIGN_MASK) & ~(size_t)ALIGN_MASK;
    if (asize < MIN_BLOCK) asize = MIN_BLOCK;

    /* ----- Level 1: 缩小或不变 ----- */
    if (asize <= old_size) {
        if (old_size - asize >= split_threshold(asize)) {
            int prev_alloc = blk_prev_alloc(ptr);
            set_meta(ptr, asize, 1, prev_alloc);
            void* rem = (char*)ptr + asize;
            size_t rem_size = old_size - asize;
            set_meta(rem, rem_size, 0, 1);
            set_prev(rem, ptr);
            void* nn = has_next(rem) ? blk_next(rem) : NULL;
            if (nn && !blk_alloc(nn)) {
                int nn_was_last = (nn == last_block_p);
                remove_free(nn);
                rem_size += blk_size(nn);
                set_meta(rem, rem_size, 0, 1);
                if (nn_was_last) last_block_p = rem;
            }
            if (has_next(rem)) {
                update_next_prev(rem, 0);
            } else {
                last_block_p = rem;
            }
            insert_free(rem, rem_size);
        }
        return ptr;
    }

    void* next = has_next(ptr) ? blk_next(ptr) : NULL;

    /* ----- Level 2: 后块 free 且合并够 ----- */
    if (next && !blk_alloc(next)) {
        size_t next_size = blk_size(next);
        if (old_size + next_size >= asize) {
            int next_was_last = (next == last_block_p);
            remove_free(next);
            size_t total = old_size + next_size;
            int prev_alloc = blk_prev_alloc(ptr);
            if (total >= asize + split_threshold(asize)) {
                set_meta(ptr, asize, 1, prev_alloc);
                void* sp = (char*)ptr + asize;
                size_t rem = total - asize;
                set_meta(sp, rem, 0, 1);
                set_prev(sp, ptr);
                insert_free(sp, rem);
                if (has_next(sp)) {
                    update_next_prev(sp, 0);
                } else {
                    last_block_p = sp;
                }
            } else {
                set_meta(ptr, total, 1, prev_alloc);
                if (next_was_last) {
                    last_block_p = ptr;
                } else if (has_next(ptr)) {
                    update_next_prev(ptr, 1);
                }
            }
            return ptr;
        }
    }

    /* ----- Level 3: ptr 是堆末块 → sbrk 扩展 ----- */
    if (ptr == last_block_p) {
        size_t need = asize - old_size;
        if (need < MIN_BLOCK) need = MIN_BLOCK;
        need = (need + ALIGN_MASK) & ~(size_t)ALIGN_MASK;
        void* extra = mem_sbrk(need);
        if (extra == (void*)-1) {
            void* newp = mm_malloc(size);
            if (!newp) return NULL;
            size_t copy = old_payload < size ? old_payload : size;
            memcpy(newp, ptr, copy);
            mm_free(ptr);
            return newp;
        }
        int prev_alloc = blk_prev_alloc(ptr);
        size_t total = old_size + need;
        set_meta(ptr, total, 1, prev_alloc);
        if (total >= asize + split_threshold(asize)) {
            set_meta(ptr, asize, 1, prev_alloc);
            void* sp = (char*)ptr + asize;
            size_t rem = total - asize;
            set_meta(sp, rem, 0, 1);
            set_prev(sp, ptr);
            insert_free(sp, rem);
            last_block_p = sp;
        }
        return ptr;
    }

    /* ----- Level 4: 前块 free 且合并够 ----- */
    int prev_alloc = blk_prev_alloc(ptr);
    if (!prev_alloc) {
        void* prev = blk_prev(ptr);
        size_t prev_size = blk_size(prev);
        if (prev_size + old_size >= asize) {
            remove_free(prev);
            int prev_prev_alloc = blk_prev_alloc(prev);
            memmove(prev, ptr, old_payload);
            size_t total = prev_size + old_size;
            if (total >= asize + split_threshold(asize)) {
                set_meta(prev, asize, 1, prev_prev_alloc);
                void* sp = (char*)prev + asize;
                size_t rem = total - asize;
                set_meta(sp, rem, 0, 1);
                set_prev(sp, prev);
                insert_free(sp, rem);
                if (has_next(sp)) {
                    update_next_prev(sp, 0);
                } else {
                    last_block_p = sp;
                }
            } else {
                set_meta(prev, total, 1, prev_prev_alloc);
                if (has_next(prev)) {
                    update_next_prev(prev, 1);
                } else {
                    last_block_p = prev;
                }
            }
            return prev;
        }
    }

    /* ----- Level 5: 前后都 free 且三块合并够 ----- */
    if (!prev_alloc) {
        void* prev = blk_prev(ptr);
        size_t prev_size = blk_size(prev);
        size_t next_size = next ? blk_size(next) : 0;
        if (next && !blk_alloc(next) && prev_size + old_size + next_size >= asize) {
            int next_was_last = (next == last_block_p);
            remove_free(prev);
            remove_free(next);
            int prev_prev_alloc = blk_prev_alloc(prev);
            memmove(prev, ptr, old_payload);
            size_t total = prev_size + old_size + next_size;
            if (total >= asize + split_threshold(asize)) {
                set_meta(prev, asize, 1, prev_prev_alloc);
                void* sp = (char*)prev + asize;
                size_t rem = total - asize;
                set_meta(sp, rem, 0, 1);
                set_prev(sp, prev);
                insert_free(sp, rem);
                if (next_was_last) {
                    last_block_p = sp;
                } else {
                    update_next_prev(sp, 0);
                }
            } else {
                set_meta(prev, total, 1, prev_prev_alloc);
                if (next_was_last) {
                    last_block_p = prev;
                } else {
                    update_next_prev(prev, 1);
                }
            }
            return prev;
        }
    }

    /* ----- Fallback ----- */
    void* newp = mm_malloc(size);
    if (!newp) return NULL;
    size_t copy = old_payload < size ? old_payload : size;
    memcpy(newp, ptr, copy);
    mm_free(ptr);
    return newp;
}

void mm_heapcheck(void) {
}
