/*
	Thread.c
	多线程搜索密码程序（实验组）：pthread + 8-wide AVX2 + 内嵌汇编

	核心思路：把 SearchRandom.c 单线程版改写为多线程版，并在热循环中
	用 AVX2 SIMD（一次处理 8 个候选密码）+ 内嵌汇编（避开 -O0 栈溢出）
	进一步压榨性能。

	关键优化点：
	1) pthread 多线程：把 [0, 2^32) 区间划分给 N 个线程并行
	2) 8 值并行：一次循环处理 2 个 YMM 寄存器共 8 个候选密码
	3) 全程寄存器：常量向量（M、0x29A、MASK、SEARCH、STEP）和
	   工作向量（base、t0..t5）全部留在 YMM 寄存器，热循环内 0 内存访问
	4) 内嵌汇编：避开 gcc -O0 的栈溢出，热循环只有 ~20 条指令
	5) 命中延迟处理：热循环里只往私有 hits[] 缓冲写一条 12 字节记录，
	   循环结束后再由 C 代码反解具体密码（命中极稀疏，3/2^32）
	6) 区间回绕：end - i 用 uint32 无符号语义自动处理 0xFFFFFFFF 跨越

	编译：-mavx2；保持 gcc 默认 -O0（不违反 pptx 优化级别限制）
	用法：./Thread [线程数]    默认 8
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <immintrin.h>

#define DEFAULT_THREADS 8
#define SEARCH_VAL      0x39A6FFBBu
#define RANGE_TOTAL     0x100000000ull
#define HIT_CAP         64

static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
	unsigned int begin;
	unsigned int end;
	unsigned int search_val;
	int          thread_id;
	int          found_count;
} thread_arg_t;

/* 命中记录：asm 内部写入，循环结束后由 C 反解 */
typedef struct {
	unsigned int i_start;   /* 该次迭代的 i+0 */
	unsigned int mask_lo;   /* b0 的 vmovmskps 输出 */
	unsigned int mask_hi;   /* b1 的 vmovmskps 输出 */
} hit_t;

void GenerateRandomNumber(unsigned int *rh, unsigned int *rl)
{
	unsigned long long x = (unsigned long long)*rh;
	x *= 0x6AC690C5ull;
	x += *rl;
	*rh = (unsigned int)x;
	*rl = (unsigned int)(x >> 32);
}

static inline void report(thread_arg_t *t, unsigned int p)
{
	pthread_mutex_lock(&g_mutex);
	printf("[线程 %d] 找到啦~! 密码是 %08X\n", t->thread_id, p);
	pthread_mutex_unlock(&g_mutex);
}

/* 处理命中缓冲：从 (i_start, mask_lo, mask_hi) 反解具体密码 */
static void process_hits(thread_arg_t *t, const hit_t *hits, int n)
{
	for (int k = 0; k < n; k++) {
		unsigned int i0 = hits[k].i_start;
		unsigned int m;
		m = hits[k].mask_lo & 0xAAu;   /* bit1,3,5,7 = lane0,1,2,3 */
		if (m & 0x02u) { report(t, i0 + 0); t->found_count++; }
		if (m & 0x08u) { report(t, i0 + 1); t->found_count++; }
		if (m & 0x20u) { report(t, i0 + 2); t->found_count++; }
		if (m & 0x80u) { report(t, i0 + 3); t->found_count++; }
		m = hits[k].mask_hi & 0xAAu;
		if (m & 0x02u) { report(t, i0 + 4); t->found_count++; }
		if (m & 0x08u) { report(t, i0 + 5); t->found_count++; }
		if (m & 0x20u) { report(t, i0 + 6); t->found_count++; }
		if (m & 0x80u) { report(t, i0 + 7); t->found_count++; }
	}
}

void *worker_opt(void *arg)
{
	thread_arg_t *t = (thread_arg_t *)arg;

	/* 常量向量（只读输入）*/
	const __m256i vM   = _mm256_set1_epi64x(0x6AC690C5ull);
	const __m256i vL   = _mm256_set1_epi64x(0x29Aull);
	const __m256i vMSK = _mm256_set1_epi64x(0xFFFFFFFFull);
	const __m256i vS   = _mm256_set1_epi64x((uint64_t)t->search_val);
	const __m256i vSTP = _mm256_set1_epi64x(8ull);

	unsigned int i   = t->begin;
	unsigned int end = t->end;

	/* 剩余元素数（uint32 无符号，正确处理回绕）*/
	uint64_t remaining = (uint64_t)(unsigned int)(end - i);
	uint64_t iters     = remaining >> 3;     /* 每次迭代处理 8 个值 */

	/* 初始 base 向量：b0=(i+3,i+2,i+1,i+0)，b1=(i+7,i+6,i+5,i+4) */
	__m256i b0 = _mm256_set_epi64x((uint64_t)(unsigned int)(i + 3),
	                                (uint64_t)(unsigned int)(i + 2),
	                                (uint64_t)(unsigned int)(i + 1),
	                                (uint64_t)(unsigned int)(i + 0));
	__m256i b1 = _mm256_set_epi64x((uint64_t)(unsigned int)(i + 7),
	                                (uint64_t)(unsigned int)(i + 6),
	                                (uint64_t)(unsigned int)(i + 5),
	                                (uint64_t)(unsigned int)(i + 4));

	hit_t      hits[HIT_CAP];
	void      *hptr       = hits;
	unsigned int hcount   = 0;
	unsigned int m_lo     = 0;
	unsigned int m_hi     = 0;
	unsigned int tmp_var  = 0;
	__m256i t0, t1, t2, t3, t4, t5;

	if (iters > 0) {
	asm volatile (
		/* === 处理 b0：4 路 l2 = high32( low32(b0*M+0x29A) * M + high32(b0*M+0x29A) ) === */
		"1:\n\t"
		"vpmuludq  %[M],   %[b0], %[t0]\n\t"   /* t0 = b0 * M */
		"vpaddq    %[L],   %[t0], %[t0]\n\t"   /* t0 = sum1 = b0*M + 0x29A */
		"vpand     %[MSK], %[t0], %[t1]\n\t"   /* t1 = h1 = sum1 & 0xFFFFFFFF */
		"vpsrlq    $32,    %[t0], %[t2]\n\t"   /* t2 = l1 = sum1 >> 32 */
		"vpmuludq  %[M],   %[t1], %[t1]\n\t"   /* t1 = h1 * M */
		"vpaddq    %[t2],  %[t1], %[t1]\n\t"   /* t1 = sum2 = h1*M + l1 */
		"vpsrlq    $32,    %[t1], %[t1]\n\t"   /* t1 = l2 */
		"vpcmpeqq  %[S],   %[t1], %[t1]\n\t"   /* t1 = (l2 == SEARCH) */
		"vmovmskps %[t1],  %[mlo]\n\t"         /* mlo = 8 位掩码 */

		/* === 处理 b1：同上 === */
		"vpmuludq  %[M],   %[b1], %[t3]\n\t"
		"vpaddq    %[L],   %[t3], %[t3]\n\t"
		"vpand     %[MSK], %[t3], %[t4]\n\t"
		"vpsrlq    $32,    %[t3], %[t5]\n\t"
		"vpmuludq  %[M],   %[t4], %[t4]\n\t"
		"vpaddq    %[t5],  %[t4], %[t4]\n\t"
		"vpsrlq    $32,    %[t4], %[t4]\n\t"
		"vpcmpeqq  %[S],   %[t4], %[t4]\n\t"
		"vmovmskps %[t4],  %[mhi]\n\t"

		/* === 命中检测：合并两掩码看是否有任何 lane 命中 === */
		"mov  %[mlo], %[tmp]\n\t"
		"or   %[mhi], %[tmp]\n\t"
		"and  $0xAA,  %[tmp]\n\t"
		"jz   3f\n\t"

		/* === 命中：写一条 12 字节记录到 hits 缓冲 === */
		"vmovd %x[b0], %[tmp]\n\t"               /* tmp = b0 lane0 低 32 位 = i+0 */
		"mov  %[tmp],  0(%[hp])\n\t"
		"mov  %[mlo],  4(%[hp])\n\t"
		"mov  %[mhi],  8(%[hp])\n\t"
		"add  $12,     %[hp]\n\t"
		"incl %[hc]\n\t"

		"3:\n\t"
		/* === 推进 base === */
		"vpaddq %[STP], %[b0], %[b0]\n\t"
		"vpaddq %[STP], %[b1], %[b1]\n\t"
		"decq  %[it]\n\t"
		"jnz   1b\n\t"
		:
		  [b0]  "+x" (b0),
		  [b1]  "+x" (b1),
		  [hp]  "+r" (hptr),
		  [hc]  "+r" (hcount),
		  [it]  "+r" (iters),
		  [mlo] "=&r" (m_lo),
		  [mhi] "=&r" (m_hi),
		  [tmp] "=&r" (tmp_var),
		  [t0]  "=&x" (t0),
		  [t1]  "=&x" (t1),
		  [t2]  "=&x" (t2),
		  [t3]  "=&x" (t3),
		  [t4]  "=&x" (t4),
		  [t5]  "=&x" (t5)
		:
		  [M]   "x" (vM),
		  [L]   "x" (vL),
		  [MSK] "x" (vMSK),
		  [S]   "x" (vS),
		  [STP] "x" (vSTP)
		:
		  "cc", "memory"
	);
	}

	/* 反解命中记录 */
	process_hits(t, hits, (int)hcount);

	/* 标量尾部：处理剩余 < 8 个值 */
	unsigned int pos = (unsigned int)((uint64_t)i + (remaining - (remaining & 7u)));
	while (pos != end) {
		unsigned int h = pos, l = 0x29A;
		GenerateRandomNumber(&h, &l);
		GenerateRandomNumber(&h, &l);
		if (l == t->search_val) {
			report(t, pos);
			t->found_count++;
		}
		pos++;
	}

	pthread_exit(NULL);
}

int main(int argc, char *argv[])
{
	int num_threads = DEFAULT_THREADS;

	if (argc >= 2) {
		int n = atoi(argv[1]);
		if (n <= 0 || n > 256) {
			fprintf(stderr, "线程数 %d 不合法，使用默认 %d\n", n, DEFAULT_THREADS);
		} else {
			num_threads = n;
		}
	}

	if (!__builtin_cpu_supports("avx2")) {
		fprintf(stderr, "当前 CPU 不支持 AVX2\n");
		return 2;
	}

	printf("多线程 + 8-wide AVX2 SIMD + 内嵌汇编，使用 %d 个线程\n", num_threads);

	uint64_t chunk = RANGE_TOTAL / (uint64_t)num_threads;

	pthread_t    *threads = malloc((size_t)num_threads * sizeof(pthread_t));
	thread_arg_t *args    = malloc((size_t)num_threads * sizeof(thread_arg_t));
	if (!threads || !args) {
		fprintf(stderr, "内存分配失败\n");
		free(threads); free(args);
		return 1;
	}

	struct timespec ts_begin, ts_end;
	clock_gettime(CLOCK_MONOTONIC, &ts_begin);

	for (int t = 0; t < num_threads; t++) {
		args[t].begin       = (unsigned int)((uint64_t)t * chunk);
		args[t].end         = (t == num_threads - 1)
		                    ? 0u
		                    : (unsigned int)((uint64_t)(t + 1) * chunk);
		args[t].search_val  = SEARCH_VAL;
		args[t].thread_id   = t;
		args[t].found_count = 0;

		int rc = pthread_create(&threads[t], NULL, worker_opt, &args[t]);
		if (rc) {
			fprintf(stderr, "pthread_create 失败: rc=%d\n", rc);
			return 1;
		}
	}

	int total = 0;
	for (int t = 0; t < num_threads; t++) {
		pthread_join(threads[t], NULL);
		total += args[t].found_count;
	}

	clock_gettime(CLOCK_MONOTONIC, &ts_end);
	double elapsed = (double)(ts_end.tv_sec - ts_begin.tv_sec)
	               + (double)(ts_end.tv_nsec - ts_begin.tv_nsec) / 1e9;

	pthread_mutex_destroy(&g_mutex);

	printf("共找到 %d 个密码，耗时 %.3f 秒\n", total, elapsed);

	free(threads);
	free(args);
	return 0;
}
