#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

/* Replay binary2-bal.rep pattern */
int main(void) {
    mem_init();
    mm_init();

    static void* ptrs[4000];
    int n = 0;

    /* Phase 1: 2400 allocs alternating 16/112 */
    for (int i = 0; i < 2400; i++) {
        size_t sz = (i % 2 == 0) ? 16 : 112;
        ptrs[i] = mm_malloc(sz);
        if (!ptrs[i]) { printf("alloc fail at %d\n", i); return 1; }
    }
    printf("After phase1: heap=%zu\n", mem_heapsize());

    /* Phase 2: free odd indices (112-byte) */
    for (int i = 1; i < 2400; i += 2) {
        mm_free(ptrs[i]);
        ptrs[i] = NULL;
    }
    printf("After phase2: heap=%zu\n", mem_heapsize());

    /* Phase 3: alloc 1200 x 128-payload */
    for (int i = 0; i < 1200; i++) {
        ptrs[2400 + i] = mm_malloc(128);
        if (!ptrs[2400 + i]) { printf("alloc fail at %d\n", 2400+i); return 1; }
    }
    printf("After phase3: heap=%zu\n", mem_heapsize());

    /* Phase 4: free everything remaining */
    for (int i = 0; i < 3600; i++) {
        if (ptrs[i]) mm_free(ptrs[i]);
    }
    printf("After phase4: heap=%zu\n", mem_heapsize());

    mm_heapcheck();
    return 0;
}
