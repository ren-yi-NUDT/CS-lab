///////////////////////////////////////////////////////////////////////
////  TAGE Branch Predictor (Seznec, 2006)                          //
////  TAgged GEometric history length predictor                      //
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

#define TAKEN    'T'
#define NOT_TAKEN 'N'

// ---- 参数 ----
// 存储预算: ~256KB 量级，与之前实验中 gshare 的 131072 entries 可比
#define NUM_TABLES  8
#define BASE_BITS   13            // base bimodal: 8192 entries
#define BASE_SIZE   (1 << BASE_BITS)
#define BASE_MASK   (BASE_SIZE - 1)
#define TBL_BITS    12            // 每张 tagged table: 4096 entries
#define TBL_SIZE    (1 << TBL_BITS)
#define TBL_MASK    (TBL_SIZE - 1)
#define TAG_BITS    12
#define TAG_MASK    ((1 << TAG_BITS) - 1)
#define CTR_MAX     3
#define USE_MAX     3

// 几何递增历史长度: 经典 Seznec 参数
static const int hist_len[NUM_TABLES] = {4, 7, 12, 20, 34, 58, 99, 168};

// ---- 数据结构 ----
typedef struct {
	UINT32 tag;
	UINT32 ctr;     // 2-bit saturating counter (0-3)
	UINT32 useful;  // 2-bit useful counter (0-3)
} TageEntry;

static UINT32    *base_ctr;
static TageEntry *tables[NUM_TABLES];
static UINT64    ghr;             // 只需保留最近 MAX_HIST 位
static UINT32    tick;

// GetPrediction → UpdatePredictor 的通信
static int provider_num;  // provider 表编号 (-1 = none)
static int alt_num;       // alternate 表编号 (-1 = base)

// ---- 辅助 ----
static inline UINT32 SatInc(UINT32 x, UINT32 m) { return x < m ? x + 1 : x; }
static inline UINT32 SatDec(UINT32 x)           { return x > 0 ? x - 1 : x; }
static char ctr_pred(UINT32 ctr) { return ctr > (CTR_MAX / 2) ? TAKEN : NOT_TAKEN; }

// 将 ghr 低 len 位折叠到 width 位 (XOR folding)
static UINT32 fold_hist(int len, int width)
{
	UINT32 h = (UINT32)(ghr & ((1ULL << len) - 1));
	UINT32 result = h & ((1u << width) - 1);
	h >>= width;
	int remaining = len - width;
	while (remaining > 0) {
		int chunk = remaining < width ? remaining : width;
		result ^= h & ((1u << chunk) - 1);
		h >>= chunk;
		remaining -= chunk;
	}
	return result & ((1u << width) - 1);
}

// index: PC XOR folded_history，每张表用不同的折叠方式
static UINT32 get_index(int t, UINT64 PC)
{
	UINT32 pc = (UINT32)(PC >> 2);
	UINT32 h  = fold_hist(hist_len[t], TBL_BITS);
	// 每张表额外偏移，避免所有表 index 一样
	h = (h + t * 2654435761u) & TBL_MASK;
	return (pc ^ h) & TBL_MASK;
}

// tag: 独立于 index 的 hash，用不同的折叠
static UINT32 get_tag(int t, UINT64 PC)
{
	UINT32 pc = (UINT32)(PC >> 2);
	// tag 用 PC 的高位 + 不同的 GHR 折叠
	UINT32 h = fold_hist(hist_len[t], TAG_BITS);
	h = (h ^ (pc >> TBL_BITS)) & TAG_MASK;
	return h;
}

// ---- 接口 ----

void PREDICTOR_init(void)
{
	ghr = 0;
	tick = 0;
	provider_num = -1;
	alt_num = -1;

	base_ctr = (UINT32 *)malloc(BASE_SIZE * sizeof(UINT32));
	for (UINT32 i = 0; i < BASE_SIZE; i++)
		base_ctr[i] = CTR_MAX / 2 + 1;

	for (int t = 0; t < NUM_TABLES; t++) {
		tables[t] = (TageEntry *)malloc(TBL_SIZE * sizeof(TageEntry));
		memset(tables[t], 0, TBL_SIZE * sizeof(TageEntry));
	}
}

char GetPrediction(UINT64 PC)
{
	UINT32 base_i = (UINT32)(PC >> 2) & BASE_MASK;
	char   base_pred = ctr_pred(base_ctr[base_i]);

	// 从最长历史到最短，找命中的 tagged table
	provider_num = -1;
	alt_num = -1;

	for (int t = NUM_TABLES - 1; t >= 0; t--) {
		UINT32 idx = get_index(t, PC);
		UINT32 tag = get_tag(t, PC);
		if (tables[t][idx].tag == tag) {
			if (provider_num == -1)
				provider_num = t;
			else if (alt_num == -1)
				alt_num = t;
		}
	}

	// 无命中 → base
	if (provider_num == -1)
		return base_pred;

	char prov_pred = ctr_pred(tables[provider_num][get_index(provider_num, PC)].ctr);

	// alternate prediction
	char alt_pred;
	if (alt_num != -1)
		alt_pred = ctr_pred(tables[alt_num][get_index(alt_num, PC)].ctr);
	else
		alt_pred = base_pred;

	// TAGE 核心逻辑: provider useful==0 时不一定信任
	UINT32 useful = tables[provider_num][get_index(provider_num, PC)].useful;
	if (useful == 0) {
		// provider 没被证明有用，用 weaker of (provider, alt)
		// 标准 TAGE: 如果 alt 的 counter 更强(更偏离中间值)，用 alt
		return alt_pred;
	}

	return prov_pred;
}

void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
{
	(void)opType; (void)branchTarget;

	// 确定 alternate prediction
	char alt_pred;
	if (alt_num != -1)
		alt_pred = ctr_pred(tables[alt_num][get_index(alt_num, PC)].ctr);
	else {
		UINT32 base_i = (UINT32)(PC >> 2) & BASE_MASK;
		alt_pred = ctr_pred(base_ctr[base_i]);
	}

	char provider_pred;
	if (provider_num != -1)
		provider_pred = ctr_pred(tables[provider_num][get_index(provider_num, PC)].ctr);
	else {
		UINT32 base_i = (UINT32)(PC >> 2) & BASE_MASK;
		provider_pred = ctr_pred(base_ctr[base_i]);
	}

	// 更新 base bimodal
	UINT32 base_i = (UINT32)(PC >> 2) & BASE_MASK;
	if (resolveDir == TAKEN)
		base_ctr[base_i] = SatInc(base_ctr[base_i], CTR_MAX);
	else
		base_ctr[base_i] = SatDec(base_ctr[base_i]);

	if (provider_num != -1) {
		UINT32 pidx = get_index(provider_num, PC);

		// 更新 provider counter
		if (resolveDir == TAKEN)
			tables[provider_num][pidx].ctr = SatInc(tables[provider_num][pidx].ctr, CTR_MAX);
		else
			tables[provider_num][pidx].ctr = SatDec(tables[provider_num][pidx].ctr);

		// 更新 useful: 仅当 provider 和 alt 预测不一致时
		if (provider_pred != alt_pred) {
			if (resolveDir == provider_pred)
				tables[provider_num][pidx].useful = SatInc(tables[provider_num][pidx].useful, USE_MAX);
			else
				tables[provider_num][pidx].useful = SatDec(tables[provider_num][pidx].useful);
		}
	}

	// 预测错误 → 尝试分配
	if (resolveDir != predDir) {
		int start = (provider_num == -1) ? 0 : provider_num + 1;
		int allocated = 0;

		for (int t = start; t < NUM_TABLES && allocated < 2; t++) {
			UINT32 idx = get_index(t, PC);
			UINT32 tag = get_tag(t, PC);

			// 已经命中就不分配
			if (tables[t][idx].tag == tag)
				continue;

			if (tables[t][idx].useful == 0) {
				tables[t][idx].tag = tag;
				tables[t][idx].ctr = CTR_MAX / 2 + 1;
				tables[t][idx].useful = 0;
				allocated++;
			}
		}
	}

	// 周期性衰减 (经典 TAGE: 每 256K 分支)
	tick++;
	if (tick >= 256000) {
		tick = 0;
		for (int t = 0; t < NUM_TABLES; t++)
			for (UINT32 i = 0; i < TBL_SIZE; i++)
				tables[t][i].useful >>= 1;
	}

	// 更新 GHR
	ghr = (ghr << 1);
	if (resolveDir == TAKEN)
		ghr |= 1;
}

void PREDICTOR_free(void)
{
	free(base_ctr);
	for (int t = 0; t < NUM_TABLES; t++)
		free(tables[t]);
}
