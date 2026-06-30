#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm.h"
#include "memlib.h"

extern void mm_print_stats(void);

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s trace.rep\n", argv[0]); return 1; }
    FILE* f = fopen(argv[1], "r");
    if (!f) { perror("fopen"); return 1; }

    char line[256];
    int weight, num_ops, num_ids, ignore;
    fgets(line, sizeof(line), f); sscanf(line, "%d", &weight);
    fgets(line, sizeof(line), f); sscanf(line, "%d", &num_ops);
    fgets(line, sizeof(line), f); sscanf(line, "%d", &num_ids);
    fgets(line, sizeof(line), f); sscanf(line, "%d", &ignore);

    mem_init();
    mm_init();

    static void* ptrs[1 << 20];
    static size_t payload[1 << 20];
    size_t peak_in_use = 0, cur_in_use = 0;
    size_t peak_heap = mem_heapsize();
    int opn = 0;
    long alloc_count = 0, free_count = 0;

    while (fgets(line, sizeof(line), f)) {
        opn++;
        char op; int idx; size_t sz;
        if (sscanf(line, " %c %d %zu", &op, &idx, &sz) < 2) continue;
        if (op == 'a') {
            ptrs[idx] = mm_malloc(sz);
            payload[idx] = sz;
            if (!ptrs[idx]) { printf("alloc fail at op %d idx %d sz %zu\n", opn, idx, sz); return 1; }
            cur_in_use += sz;
            alloc_count++;
            if (cur_in_use > peak_in_use) peak_in_use = cur_in_use;
        } else if (op == 'f') {
            if (ptrs[idx]) {
                cur_in_use -= payload[idx];
                mm_free(ptrs[idx]);
                ptrs[idx] = NULL;
                free_count++;
            }
        } else if (op == 'r') {
            void* np = mm_realloc(ptrs[idx], sz);
            if (!np) { printf("realloc fail\n"); return 1; }
            ptrs[idx] = np;
        }
        if (mem_heapsize() > peak_heap) peak_heap = mem_heapsize();
    }

    printf("ops=%d allocs=%ld frees=%ld\n", opn, alloc_count, free_count);
    printf("peak_in_use(payload)=%zu  peak_heap=%zu\n", peak_in_use, peak_heap);
    printf("util_estimate=%.2f%%\n", 100.0 * peak_in_use / peak_heap);
    printf("cur_heap=%zu\n", mem_heapsize());

    mm_print_stats();
    return 0;
}
