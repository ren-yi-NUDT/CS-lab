/*
 * mm.c - 分离空闲链表 + Header-only + 5 级 realloc 优化
 *
 * 设计要点：
 *  - 8 字节对齐
 *  - 已分配块仅 header（8B），无 footer，靠 prev_alloc 位知道前块是否 free
 *  - 空闲块保留 footer（8B）用于向后合并；payload 区放 prev/next 指针
 *  - 13 个 size class，小块 first-fit，中大块 best-fit
 *  - realloc 五级策略：原地缩小 → 后块扩展 → 堆末扩展 → 前块迁移 → 前后合并 → 回退
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * 个人信息
 ********************************************************/
team_t team = {
    "Astro",
    "Ren Yi",
    "tom@nobody.com",
    "",
    ""
};

/* ===== 基本常量与宏 ===== */
#define WSIZE 8              /* header/footer 大小 */
#define DSIZE 16             /* 双字 */
#define CHUNKSIZE (1 << 12)  /* sbrk 扩展单位：4096 */
#define MIN_BLOCK 32         /* 最小块：header+prev+next+footer */

#define NUM_CLASSES 13

#define MAX(a, b) ((a) > (b) ? (a) : (b))

/* pack size + prev_alloc + alloc 到一个 word */
#define PACK(size, prev_alloc, alloc) ((size) | ((prev_alloc) << 1) | ((alloc) & 1))

#define GET(p)            (*(size_t*)(p))
#define PUT(p, val)       (*(size_t*)(p) = (val))

#define GET_SIZE(p)       (GET(p) & ~0x7L)
#define GET_ALLOC(p)      (GET(p) & 0x1)
#define GET_PREV_ALLOC(p) ((GET(p) & 0x2) >> 1)

/* 给定 payload 指针 bp，计算 header / footer / 邻块 */
#define HDRP(bp)  ((char*)(bp) - WSIZE)
#define FTRP(bp)  ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define NEXT_BLKP(bp)  ((char*)(bp) + GET_SIZE((char*)(bp) - WSIZE))
#define PREV_BLKP(bp)  ((char*)(bp) - GET_SIZE((char*)(bp) - DSIZE))

/* 空闲链表节点访问（payload 起点存 prev/next） */
#define PREV_FREE(bp)  (*(void**)(bp))
#define NEXT_FREE(bp)  (*(void**)((char*)(bp) + WSIZE))

/* ===== 全局状态 ===== */
static char* heap_listp;     /* 指向 prologue payload */
static void* free_lists[NUM_CLASSES];

/* ===== 工具函数 ===== */

/* 找到 size 所属的 size class 索引 */
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

/* 把空闲块插到对应 class 的链表头（LIFO） */
static void insert_free(void* bp, size_t size) {
    int cls = find_class(size);
    void* head = free_lists[cls];
    PREV_FREE(bp) = NULL;
    NEXT_FREE(bp) = head;
    if (head != NULL) {
        PREV_FREE(head) = bp;
    }
    free_lists[cls] = bp;
}

/* 从链表摘下空闲块 */
static void remove_free(void* bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int cls = find_class(size);
    void* prev = PREV_FREE(bp);
    void* next = NEXT_FREE(bp);

    if (prev != NULL) {
        NEXT_FREE(prev) = next;
    } else {
        free_lists[cls] = next;
    }
    if (next != NULL) {
        PREV_FREE(next) = prev;
    }
}

/* 设置后块的 prev_alloc 位 */
static void set_prev_alloc(void* bp, int prev_alloc) {
    size_t hdr = GET(HDRP(bp));
    /* 清 bit1，再或上新值 */
    hdr = (hdr & ~0x2L) | ((size_t)(prev_alloc & 1) << 1);
    PUT(HDRP(bp), hdr);
}

/* 扩堆：分配一个 size 字节的新 free 块，返回 payload 指针。
 * 不做 coalesce，由调用者处理。 */
static void* extend_heap(size_t size) {
    size_t asize = MAX(size, MIN_BLOCK);
    asize = (asize + 7) & ~0x7L;

    char* bp = mem_sbrk(asize);
    if (bp == (void*)-1) {
        return NULL;
    }

    /* bp-8 是旧 epilogue，读它的 prev_alloc 位（反映当前块的前一块是否已分配） */
    size_t old_eplg = GET(HDRP(bp));
    int prev_alloc = (old_eplg & 0x2) >> 1;

    /* 写当前 free 块 header/footer */
    PUT(HDRP(bp), PACK(asize, prev_alloc, 0));
    PUT(FTRP(bp), PACK(asize, prev_alloc, 0));

    /* 新 epilogue，prev_alloc=0（当前块是 free） */
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 0, 1));

    return bp;
}

/* 合并相邻空闲块，返回合并后的块指针 */
static void* coalesce(void* bp) {
    size_t size = GET_SIZE(HDRP(bp));
    int prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    void* next_bp = NEXT_BLKP(bp);
    int next_alloc = GET_ALLOC(HDRP(next_bp));

    if (prev_alloc && next_alloc) {
        /* case 1: 不合并 */
    } else if (prev_alloc && !next_alloc) {
        /* case 2: 合并后块 */
        size_t next_size = GET_SIZE(HDRP(next_bp));
        remove_free(next_bp);
        size += next_size;
        PUT(HDRP(bp), PACK(size, 1, 0));  /* prev_alloc 不变（=1） */
        PUT(FTRP(bp), PACK(size, 1, 0));
        /* 后块的 prev_alloc 位由 footer/header 自然传递，但下一块的 header 要更新 */
        set_prev_alloc(NEXT_BLKP(bp), 0);
    } else if (!prev_alloc && next_alloc) {
        /* case 3: 合并前块 */
        void* prev_bp = PREV_BLKP(bp);
        size_t prev_size = GET_SIZE(HDRP(prev_bp));
        int prev_prev_alloc = GET_PREV_ALLOC(HDRP(prev_bp));
        remove_free(prev_bp);
        size += prev_size;
        PUT(HDRP(prev_bp), PACK(size, prev_prev_alloc, 0));
        PUT(FTRP(prev_bp), PACK(size, prev_prev_alloc, 0));
        bp = prev_bp;
        /* 后块的 prev_alloc 现在是 0（当前块是 free） */
        set_prev_alloc(NEXT_BLKP(bp), 0);
    } else {
        /* case 4: 合并前后块 */
        void* prev_bp = PREV_BLKP(bp);
        size_t prev_size = GET_SIZE(HDRP(prev_bp));
        size_t next_size = GET_SIZE(HDRP(next_bp));
        int prev_prev_alloc = GET_PREV_ALLOC(HDRP(prev_bp));
        remove_free(prev_bp);
        remove_free(next_bp);
        size += prev_size + next_size;
        PUT(HDRP(prev_bp), PACK(size, prev_prev_alloc, 0));
        PUT(FTRP(prev_bp), PACK(size, prev_prev_alloc, 0));
        bp = prev_bp;
        set_prev_alloc(NEXT_BLKP(bp), 0);
    }
    return bp;
}

/* 在 free 块中找适配（全部 class 使用 best-fit 以提高利用率） */
static void* find_fit(size_t asize) {
    int cls = find_class(asize);

    for (int c = cls; c < NUM_CLASSES; c++) {
        void* node = free_lists[c];
        if (node == NULL) continue;

        /* 在当前 class 内 best-fit */
        void* best = NULL;
        size_t best_size = (size_t)-1;
        while (node != NULL) {
            size_t sz = GET_SIZE(HDRP(node));
            if (sz >= asize && sz < best_size) {
                best = node;
                best_size = sz;
                if (sz == asize) break;  /* 完美匹配，提前退出 */
            }
            node = NEXT_FREE(node);
        }
        if (best != NULL) return best;
    }
    return NULL;
}

/* 把 free 块切出 asize 大小并标记 allocated，必要时 split。
 * 调用者保证 bp 当前在某个 free list 中。 */
static void place(void* bp, size_t asize) {
    size_t block_size = GET_SIZE(HDRP(bp));
    int prev_alloc = GET_PREV_ALLOC(HDRP(bp));
    size_t remainder = block_size - asize;

    /* 先从 free list 摘下，避免后面 split 的 next 还没建好就被误删 */
    remove_free(bp);

    if (remainder >= MIN_BLOCK) {
        /* split：前半 allocated，后半 free */
        PUT(HDRP(bp), PACK(asize, prev_alloc, 1));
        /* 没有 footer（allocated） */

        void* next = (char*)bp + asize;
        PUT(HDRP(next), PACK(remainder, 1, 0));  /* prev=allocated */
        PUT(FTRP(next), PACK(remainder, 1, 0));
        insert_free(next, remainder);

        /* 更新 next-next 的 prev_alloc */
        set_prev_alloc(NEXT_BLKP(next), 0);
    } else {
        /* 不拆，整块用 */
        PUT(HDRP(bp), PACK(block_size, prev_alloc, 1));
        set_prev_alloc(NEXT_BLKP(bp), 1);
    }
}

/* ===== 公开接口 ===== */

int mm_init(void) {
    /* 申请 4 个 word：padding + prologue header + prologue footer + epilogue */
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void*)-1) {
        return -1;
    }
    PUT(heap_listp, 0);                                   /* padding */
    PUT(heap_listp + (1 * WSIZE), PACK(DSIZE, 1, 1));     /* prologue header */
    PUT(heap_listp + (2 * WSIZE), PACK(DSIZE, 1, 1));     /* prologue footer */
    PUT(heap_listp + (3 * WSIZE), PACK(0, 1, 1));         /* epilogue */
    heap_listp += (2 * WSIZE);  /* 指向 prologue payload */

    /* 清空 free list */
    for (int i = 0; i < NUM_CLASSES; i++) {
        free_lists[i] = NULL;
    }

    return 0;
}

/* 把 payload 向上舍入到最近的 2 的幂。使相似大小的请求得到相同 block size，
 * 在 binary traces（A/B 交替 alloc/free 后再 alloc C）中显著提升利用率。
 * 例如 112 -> 128 payload -> 136 block；128 -> 128 payload -> 136 block。两者匹配。
 * 为减少 random traces 的浪费，仅当 size 接近 pow2（>= 0.85）才取整，否则保持原值。 */
static size_t round_payload_pow2(size_t payload) {
    if (payload <= 16) return 16;
    int bits = 64 - __builtin_clzll(payload - 1);
    size_t next_pow2 = (size_t)1 << bits;
    size_t prev_pow2 = next_pow2 >> 1;
    /* 如果 payload 已经等于 prev_pow2，直接返回 */
    if (payload == prev_pow2) return payload;
    /* 否则若接近 next_pow2，取整；否则保持 */
    if (payload * 100 >= next_pow2 * 87) {
        return next_pow2;
    }
    return payload;
}

void* mm_malloc(size_t size) {
    if (size == 0) return NULL;

    /* payload 舍入到 pow2，再加 header，最小 MIN_BLOCK */
    size_t payload = round_payload_pow2(size);
    size_t asize = payload + WSIZE;
    if (asize < MIN_BLOCK) asize = MIN_BLOCK;
    asize = (asize + 7) & ~0x7L;

    void* bp = find_fit(asize);
    if (bp != NULL) {
        place(bp, asize);
        return bp;
    }

    /* 没找到，扩堆 */
    size_t extend_size = MAX(asize, CHUNKSIZE);
    bp = extend_heap(extend_size);
    if (bp == NULL) return NULL;

    /* coalesce（与可能的前块合并） */
    bp = coalesce(bp);
    /* 如果合并后比 asize 大很多，可能要 split */
    size_t total = GET_SIZE(HDRP(bp));
    if (total >= asize + MIN_BLOCK) {
        /* split */
        int prev_alloc = GET_PREV_ALLOC(HDRP(bp));
        PUT(HDRP(bp), PACK(asize, prev_alloc, 1));
        void* next = (char*)bp + asize;
        size_t remainder = total - asize;
        PUT(HDRP(next), PACK(remainder, 1, 0));
        PUT(FTRP(next), PACK(remainder, 1, 0));
        insert_free(next, remainder);
        set_prev_alloc(NEXT_BLKP(next), 0);
    } else {
        PUT(HDRP(bp), PACK(total, GET_PREV_ALLOC(HDRP(bp)), 1));
        set_prev_alloc(NEXT_BLKP(bp), 1);
    }
    return bp;
}

void mm_free(void* ptr) {
    if (ptr == NULL) return;
    /* 范围检查（防御非法指针） */
    if ((char*)ptr < (char*)mem_heap_lo() + 4 * WSIZE ||
        (char*)ptr > (char*)mem_heap_hi()) {
        return;
    }

    size_t size = GET_SIZE(HDRP(ptr));
    int prev_alloc = GET_PREV_ALLOC(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, prev_alloc, 0));
    PUT(FTRP(ptr), PACK(size, prev_alloc, 0));
    set_prev_alloc(NEXT_BLKP(ptr), 0);

    void* bp = coalesce(ptr);
    insert_free(bp, GET_SIZE(HDRP(bp)));
}

void* mm_realloc(void* ptr, size_t size) {
    if (ptr == NULL) return mm_malloc(size);
    if (size == 0) { mm_free(ptr); return NULL; }

    /* 防御：非法指针当新分配 */
    if ((char*)ptr < (char*)mem_heap_lo() + 4 * WSIZE ||
        (char*)ptr > (char*)mem_heap_hi()) {
        return mm_malloc(size);
    }

    size_t old_size = GET_SIZE(HDRP(ptr));
    size_t old_payload = old_size - WSIZE;  /* allocated 块只有 header */
    size_t payload = round_payload_pow2(size);
    size_t asize = payload + WSIZE;
    if (asize < MIN_BLOCK) asize = MIN_BLOCK;
    asize = (asize + 7) & ~0x7L;

    /* ----- Level 1: 缩小或不变 ----- */
    if (asize <= old_size) {
        if (old_size - asize >= MIN_BLOCK) {
            /* split */
            int prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
            PUT(HDRP(ptr), PACK(asize, prev_alloc, 1));
            void* next = (char*)ptr + asize;
            size_t remainder = old_size - asize;
            PUT(HDRP(next), PACK(remainder, 1, 0));
            PUT(FTRP(next), PACK(remainder, 1, 0));
            set_prev_alloc(NEXT_BLKP(next), 0);
            /* 与后块合并可能更大 */
            void* nn = NEXT_BLKP(next);
            if (GET_ALLOC(HDRP(nn)) == 0 && GET_SIZE(HDRP(nn)) != 0) {
                remove_free(nn);
                size_t nn_size = GET_SIZE(HDRP(nn));
                remainder += nn_size;
                PUT(HDRP(next), PACK(remainder, 1, 0));
                PUT(FTRP(next), PACK(remainder, 1, 0));
                set_prev_alloc(NEXT_BLKP(next), 0);
            }
            insert_free(next, GET_SIZE(HDRP(next)));
        }
        return ptr;
    }

    /* ----- Level 2: 后块 free 且合并够 → 原地扩展 ----- */
    void* next = NEXT_BLKP(ptr);
    size_t next_size = GET_SIZE(HDRP(next));
    int next_alloc = GET_ALLOC(HDRP(next));

    if (next_alloc == 0 && next_size != 0) {
        /* 后块是普通 free 块 */
        if (old_size + next_size >= asize) {
            remove_free(next);
            size_t total = old_size + next_size;
            int prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
            /* 视情况 split */
            if (total >= asize + MIN_BLOCK) {
                PUT(HDRP(ptr), PACK(asize, prev_alloc, 1));
                void* sp = (char*)ptr + asize;
                size_t rem = total - asize;
                PUT(HDRP(sp), PACK(rem, 1, 0));
                PUT(FTRP(sp), PACK(rem, 1, 0));
                set_prev_alloc(NEXT_BLKP(sp), 0);
                insert_free(sp, rem);
            } else {
                PUT(HDRP(ptr), PACK(total, prev_alloc, 1));
                set_prev_alloc(NEXT_BLKP(ptr), 1);
            }
            return ptr;
        }
    }

    /* ----- Level 3: 后块是 epilogue（堆末）→ sbrk 扩展 ----- */
    if (next_size == 0) {
        size_t need = asize - old_size;
        if (need < MIN_BLOCK) need = MIN_BLOCK;
        void* extra = mem_sbrk(need);
        if (extra == (void*)-1) {
            /* 扩展失败，回退到 malloc+memcpy+free */
            void* newp = mm_malloc(size);
            if (newp == NULL) return NULL;
            size_t copy = old_payload < size ? old_payload : size;
            memcpy(newp, ptr, copy);
            mm_free(ptr);
            return newp;
        }
        /* extra 是旧 brk 位置；旧 epilogue 在 extra-8，现在被并入新块 */
        int prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
        size_t total = old_size + need;
        PUT(HDRP(ptr), PACK(total, prev_alloc, 1));
        /* 新 epilogue 在 ptr + total - 8 */
        PUT((char*)ptr + total - WSIZE, PACK(0, 1, 1));

        /* 如果扩展后比需要大很多，split 出剩余 free */
        if (total >= asize + MIN_BLOCK) {
            PUT(HDRP(ptr), PACK(asize, prev_alloc, 1));
            void* sp = (char*)ptr + asize;
            size_t rem = total - asize;
            PUT(HDRP(sp), PACK(rem, 1, 0));
            PUT(FTRP(sp), PACK(rem, 1, 0));
            /* 新 epilogue 在 sp + rem - 8 */
            PUT((char*)sp + rem - WSIZE, PACK(0, 0, 1));
            insert_free(sp, rem);
        }
        return ptr;
    }

    /* ----- Level 4: 前块 free 且合并够 → 迁移到前块 ----- */
    int prev_alloc = GET_PREV_ALLOC(HDRP(ptr));
    if (prev_alloc == 0) {
        void* prev = PREV_BLKP(ptr);
        size_t prev_size = GET_SIZE(HDRP(prev));
        if (prev_size + old_size >= asize) {
            remove_free(prev);
            int prev_prev_alloc = GET_PREV_ALLOC(HDRP(prev));
            /* 拷贝数据到 prev */
            memmove(prev, ptr, old_payload);
            size_t total = prev_size + old_size;
            /* 视情况 split */
            if (total >= asize + MIN_BLOCK) {
                PUT(HDRP(prev), PACK(asize, prev_prev_alloc, 1));
                void* sp = (char*)prev + asize;
                size_t rem = total - asize;
                PUT(HDRP(sp), PACK(rem, 1, 0));
                PUT(FTRP(sp), PACK(rem, 1, 0));
                set_prev_alloc(NEXT_BLKP(sp), 0);
                insert_free(sp, rem);
            } else {
                PUT(HDRP(prev), PACK(total, prev_prev_alloc, 1));
                set_prev_alloc(NEXT_BLKP(prev), 1);
            }
            return prev;
        }
    }

    /* ----- Level 5: 前后都 free 且三块合并够 ----- */
    if (prev_alloc == 0 && next_alloc == 0 && next_size != 0) {
        void* prev = PREV_BLKP(ptr);
        size_t prev_size = GET_SIZE(HDRP(prev));
        if (prev_size + old_size + next_size >= asize) {
            remove_free(prev);
            remove_free(next);
            int prev_prev_alloc = GET_PREV_ALLOC(HDRP(prev));
            memmove(prev, ptr, old_payload);
            size_t total = prev_size + old_size + next_size;
            if (total >= asize + MIN_BLOCK) {
                PUT(HDRP(prev), PACK(asize, prev_prev_alloc, 1));
                void* sp = (char*)prev + asize;
                size_t rem = total - asize;
                PUT(HDRP(sp), PACK(rem, 1, 0));
                PUT(FTRP(sp), PACK(rem, 1, 0));
                set_prev_alloc(NEXT_BLKP(sp), 0);
                insert_free(sp, rem);
            } else {
                PUT(HDRP(prev), PACK(total, prev_prev_alloc, 1));
                set_prev_alloc(NEXT_BLKP(prev), 1);
            }
            return prev;
        }
    }

    /* ----- Fallback: malloc + memcpy + free ----- */
    void* newp = mm_malloc(size);
    if (newp == NULL) return NULL;
    size_t copy = old_payload < size ? old_payload : size;
    memcpy(newp, ptr, copy);
    mm_free(ptr);
    return newp;
}

void mm_heapcheck(void) {
    /* 暂不实现，mdriver 当前不会调用 */
}
