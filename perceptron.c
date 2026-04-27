///////////////////////////////////////////////////////////////////////
////  Perceptron Branch Predictor (Jimenez & Lin, 2001)              //
////  在线学习，无需预训练                                            //
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

#define TAKEN		'T'
#define NOT_TAKEN	'N'

// ---- Perceptron 参数 ----
#define TABLE_SIZE    1024
#define TABLE_MASK    (TABLE_SIZE - 1)
#define HIST_LEN      20
#define HIST_MASK     ((1 << HIST_LEN) - 1)
#define WEIGHT_MAX    127
#define WEIGHT_MIN    (-128)
#define THETA         ((int)(1.93 * HIST_LEN + 14))

// 每个感知机: (HIST_LEN+1) 个 signed char 权重，index 0 是 bias
signed char *weights[TABLE_SIZE];
UINT32       ghr;

void PREDICTOR_init(void)
{
	ghr = 0;
	for (UINT32 i = 0; i < TABLE_SIZE; i++) {
		weights[i] = (signed char *)malloc(HIST_LEN + 1);
		memset(weights[i], 0, HIST_LEN + 1);
	}
}

static INT32 dot_product(UINT32 idx)
{
	INT32 y = weights[idx][0];
	for (UINT32 i = 0; i < HIST_LEN; i++) {
		INT32 bit = (ghr >> i) & 1 ? 1 : -1;
		y += weights[idx][i + 1] * bit;
	}
	return y;
}

static void clamp_weight(signed char *w, INT32 val)
{
	if (val > WEIGHT_MAX)      *w = WEIGHT_MAX;
	else if (val < WEIGHT_MIN) *w = WEIGHT_MIN;
	else                       *w = (signed char)val;
}

char GetPrediction(UINT64 PC)
{
	UINT32 idx = (PC >> 2) & TABLE_MASK;
	INT32  y   = dot_product(idx);
	return y >= 0 ? TAKEN : NOT_TAKEN;
}

void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
{
	(void)opType; (void)predDir; (void)branchTarget;

	UINT32 idx = (PC >> 2) & TABLE_MASK;
	INT32  y   = dot_product(idx);
	char   pred = y >= 0 ? TAKEN : NOT_TAKEN;

	if (pred != resolveDir || (y >= -THETA && y <= THETA)) {
		INT32 t = resolveDir == TAKEN ? 1 : -1;
		clamp_weight(&weights[idx][0], (INT32)weights[idx][0] + t);
		for (UINT32 i = 0; i < HIST_LEN; i++) {
			INT32 bit = (ghr >> i) & 1 ? 1 : -1;
			clamp_weight(&weights[idx][i + 1], (INT32)weights[idx][i + 1] + t * bit);
		}
	}

	ghr = (ghr << 1) & HIST_MASK;
	if (resolveDir == TAKEN)
		ghr |= 1;
}

void PREDICTOR_free(void)
{
	for (UINT32 i = 0; i < TABLE_SIZE; i++)
		free(weights[i]);
}
