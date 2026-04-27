///////////////////////////////////////////////////////////////////////
////  Copyright 2020 by mars.                                        //
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>

#include "common.h"

#define TAKEN		'T'
#define NOT_TAKEN	'N'

// ==================== 实验10: local (3位局部历史+10位PC) ====================

// #define BHT_BITS     10
// #define HIST_BITS    3
// #define BHT_SIZE     (1 << BHT_BITS)
// #define BHT_MASK     (BHT_SIZE - 1)
// #define HIST_MASK    ((1 << HIST_BITS) - 1)
// #define PHT_SIZE     (1 << (BHT_BITS + HIST_BITS))
// #define CTR_MAX      3
// #define CTR_INIT     2

// UINT32 *pht;
// UINT32 *bht;

// static inline UINT32 SatIncrement(UINT32 x, UINT32 max) { return x < max ? x + 1 : x; }
// static inline UINT32 SatDecrement(UINT32 x)             { return x > 0 ? x - 1 : x; }

// void PREDICTOR_init(void)
// {
// 	pht = (UINT32 *)malloc(PHT_SIZE * sizeof(UINT32));
// 	bht = (UINT32 *)malloc(BHT_SIZE * sizeof(UINT32));
// 	for (UINT32 i = 0; i < PHT_SIZE; i++)
// 		pht[i] = CTR_INIT;
// 	for (UINT32 i = 0; i < BHT_SIZE; i++)
// 		bht[i] = 0;
// }

// char GetPrediction(UINT64 PC)
// {
// 	UINT32 bhtIndex = (PC >> 2) & BHT_MASK;
// 	UINT32 hist     = bht[bhtIndex];
// 	UINT32 phtIndex = (bhtIndex << HIST_BITS) | hist;
// 	return pht[phtIndex] > (CTR_MAX / 2) ? TAKEN : NOT_TAKEN;
// }

// void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
// {
// 	(void)opType; (void)predDir; (void)branchTarget;
// 	UINT32 bhtIndex = (PC >> 2) & BHT_MASK;
// 	UINT32 hist     = bht[bhtIndex];
// 	UINT32 phtIndex = (bhtIndex << HIST_BITS) | hist;
// 	if (resolveDir == TAKEN)
// 		pht[phtIndex] = SatIncrement(pht[phtIndex], CTR_MAX);
// 	else
// 		pht[phtIndex] = SatDecrement(pht[phtIndex]);
// 	bht[bhtIndex] = ((bht[bhtIndex] << 1) & HIST_MASK);
// 	if (resolveDir == TAKEN)
// 		bht[bhtIndex] |= 1;
// }

// void PREDICTOR_free(void)
// {
// 	free(pht);
// 	free(bht);
// }

// ==================== 实验9: global (10位全局历史) ====================
// 结果已写入 Result.xlsx Row 9

// #define HIST_LEN     10
// #define PHT_SIZE     (1 << HIST_LEN)
// #define HIST_MASK    (PHT_SIZE - 1)
// #define CTR_MAX      3
// #define CTR_INIT     2

// UINT32 ghr;
// UINT32 *pht;

// static inline UINT32 SatIncrement(UINT32 x, UINT32 max) { return x < max ? x + 1 : x; }
// static inline UINT32 SatDecrement(UINT32 x)             { return x > 0 ? x - 1 : x; }

// void PREDICTOR_init(void)
// {
// 	ghr = 0;
// 	pht = (UINT32 *)malloc(PHT_SIZE * sizeof(UINT32));
// 	for (UINT32 i = 0; i < PHT_SIZE; i++)
// 		pht[i] = CTR_INIT;
// }

// char GetPrediction(UINT64 PC)
// {
// 	(void)PC;
// 	return pht[ghr] > (CTR_MAX / 2) ? TAKEN : NOT_TAKEN;
// }

// void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
// {
// 	(void)PC; (void)opType; (void)predDir; (void)branchTarget;
// 	if (resolveDir == TAKEN)
// 		pht[ghr] = SatIncrement(pht[ghr], CTR_MAX);
// 	else
// 		pht[ghr] = SatDecrement(pht[ghr]);
// 	ghr = ((ghr << 1) & HIST_MASK);
// 	if (resolveDir == TAKEN)
// 		ghr |= 1;
// }

// void PREDICTOR_free(void)
// {
// 	free(pht);
// }

// ==================== 实验8: gshare PC<<1 ====================
// 结果已写入 Result.xlsx Row 8

// #define HIST_LEN     17
// #define PHT_SIZE     (1 << HIST_LEN)
// #define CTR_MAX      3
// #define CTR_INIT     2

// UINT32 ghr;
// UINT32 *pht;

// static inline UINT32 SatIncrement(UINT32 x, UINT32 max) { return x < max ? x + 1 : x; }
// static inline UINT32 SatDecrement(UINT32 x)             { return x > 0 ? x - 1 : x; }

// void PREDICTOR_init(void)
// {
// 	ghr = 0;
// 	pht = (UINT32 *)malloc(PHT_SIZE * sizeof(UINT32));
// 	for (UINT32 i = 0; i < PHT_SIZE; i++)
// 		pht[i] = CTR_INIT;
// }

// char GetPrediction(UINT64 PC)
// {
// 	UINT32 index = ((PC << 1) ^ ghr) % PHT_SIZE;
// 	return pht[index] > (CTR_MAX / 2) ? TAKEN : NOT_TAKEN;
// }

// void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
// {
// 	(void)opType; (void)predDir; (void)branchTarget;
// 	UINT32 index = ((PC << 1) ^ ghr) % PHT_SIZE;
// 	if (resolveDir == TAKEN)
// 		pht[index] = SatIncrement(pht[index], CTR_MAX);
// 	else
// 		pht[index] = SatDecrement(pht[index]);
// 	ghr = (ghr << 1);
// 	if (resolveDir == TAKEN)
// 		ghr |= 1;
// }

// void PREDICTOR_free(void)
// {
// 	free(pht);
// }

// ==================== 实验7: gshare (PC XOR ghr, 17位历史) ====================
// 结果已写入 Result.xlsx Row 7

// #define HIST_LEN     17
// #define PHT_SIZE     (1 << HIST_LEN)
// #define CTR_MAX      3
// #define CTR_INIT     2

// UINT32 ghr;
// UINT32 *pht;

// static inline UINT32 SatIncrement(UINT32 x, UINT32 max) { return x < max ? x + 1 : x; }
// static inline UINT32 SatDecrement(UINT32 x)             { return x > 0 ? x - 1 : x; }

// void PREDICTOR_init(void)
// {
// 	ghr = 0;
// 	pht = (UINT32 *)malloc(PHT_SIZE * sizeof(UINT32));
// 	for (UINT32 i = 0; i < PHT_SIZE; i++)
// 		pht[i] = CTR_INIT;
// }

// char GetPrediction(UINT64 PC)
// {
// 	UINT32 index = (PC ^ ghr) % PHT_SIZE;
// 	return pht[index] > (CTR_MAX / 2) ? TAKEN : NOT_TAKEN;
// }

// void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
// {
// 	(void)opType; (void)predDir; (void)branchTarget;
// 	UINT32 index = (PC ^ ghr) % PHT_SIZE;
// 	if (resolveDir == TAKEN)
// 		pht[index] = SatIncrement(pht[index], CTR_MAX);
// 	else
// 		pht[index] = SatDecrement(pht[index]);
// 	ghr = (ghr << 1);
// 	if (resolveDir == TAKEN)
// 		ghr |= 1;
// }

// void PREDICTOR_free(void)
// {
// 	free(pht);
// }

// ==================== 实验6: gshare PC>>1 ====================
// 结果已写入 Result.xlsx Row 6

// #define HIST_LEN     17
// #define PHT_SIZE     (1 << HIST_LEN)
// #define CTR_MAX      3
// #define CTR_INIT     2

// UINT32 ghr;
// UINT32 *pht;

// static inline UINT32 SatIncrement(UINT32 x, UINT32 max) { return x < max ? x + 1 : x; }
// static inline UINT32 SatDecrement(UINT32 x)             { return x > 0 ? x - 1 : x; }

// void PREDICTOR_init(void)
// {
// 	ghr = 0;
// 	pht = (UINT32 *)malloc(PHT_SIZE * sizeof(UINT32));
// 	for (UINT32 i = 0; i < PHT_SIZE; i++)
// 		pht[i] = CTR_INIT;
// }

// char GetPrediction(UINT64 PC)
// {
// 	UINT32 index = ((PC >> 1) ^ ghr) % PHT_SIZE;
// 	return pht[index] > (CTR_MAX / 2) ? TAKEN : NOT_TAKEN;
// }

// void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
// {
// 	(void)opType; (void)predDir; (void)branchTarget;
// 	UINT32 index = ((PC >> 1) ^ ghr) % PHT_SIZE;
// 	if (resolveDir == TAKEN)
// 		pht[index] = SatIncrement(pht[index], CTR_MAX);
// 	else
// 		pht[index] = SatDecrement(pht[index]);
// 	ghr = (ghr << 1);
// 	if (resolveDir == TAKEN)
// 		ghr |= 1;
// }

// void PREDICTOR_free(void)
// {
// 	free(pht);
// }

// ==================== 实验5: gshare PC>>2 ====================
// 结果已写入 Result.xlsx Row 5

// #define HIST_LEN     17
// #define PHT_SIZE     (1 << HIST_LEN)
// #define CTR_MAX      3
// #define CTR_INIT     2

// UINT32 ghr;
// UINT32 *pht;

// static inline UINT32 SatIncrement(UINT32 x, UINT32 max) { return x < max ? x + 1 : x; }
// static inline UINT32 SatDecrement(UINT32 x)             { return x > 0 ? x - 1 : x; }

// void PREDICTOR_init(void)
// {
// 	ghr = 0;
// 	pht = (UINT32 *)malloc(PHT_SIZE * sizeof(UINT32));
// 	for (UINT32 i = 0; i < PHT_SIZE; i++)
// 		pht[i] = CTR_INIT;
// }

// char GetPrediction(UINT64 PC)
// {
// 	UINT32 index = ((PC >> 2) ^ ghr) % PHT_SIZE;
// 	return pht[index] > (CTR_MAX / 2) ? TAKEN : NOT_TAKEN;
// }

// void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
// {
// 	(void)opType; (void)predDir; (void)branchTarget;
// 	UINT32 index = ((PC >> 2) ^ ghr) % PHT_SIZE;
// 	if (resolveDir == TAKEN)
// 		pht[index] = SatIncrement(pht[index], CTR_MAX);
// 	else
// 		pht[index] = SatDecrement(pht[index]);
// 	ghr = (ghr << 1);
// 	if (resolveDir == TAKEN)
// 		ghr |= 1;
// }

// void PREDICTOR_free(void)
// {
// 	free(pht);
// }

// ==================== 实验4: local PC>>2 ====================
// 结果已写入 Result.xlsx Row 4

// #define BHT_SIZE  (1 << 17)
// #define HIST_LEN  10
// #define PHT_SIZE  (1 << HIST_LEN)
// #define HIST_MASK (PHT_SIZE - 1)
// #define CTR_MAX   3
// #define CTR_INIT  2

// UINT32 *pht;
// UINT32 *bht;

// static inline UINT32 SatIncrement(UINT32 x, UINT32 max) { return x < max ? x + 1 : x; }
// static inline UINT32 SatDecrement(UINT32 x)             { return x > 0 ? x - 1 : x; }

// void PREDICTOR_init(void)
// {
// 	pht = (UINT32 *)malloc(PHT_SIZE * sizeof(UINT32));
// 	bht = (UINT32 *)malloc(BHT_SIZE * sizeof(UINT32));
// 	for (UINT32 i = 0; i < PHT_SIZE; i++)
// 		pht[i] = CTR_INIT;
// 	for (UINT32 i = 0; i < BHT_SIZE; i++)
// 		bht[i] = 0;
// }

// char GetPrediction(UINT64 PC)
// {
// 	UINT32 bhtIndex  = (PC >> 2) % BHT_SIZE;
// 	UINT32 phtIndex  = bht[bhtIndex];
// 	return pht[phtIndex] > (CTR_MAX / 2) ? TAKEN : NOT_TAKEN;
// }

// void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
// {
// 	(void)opType; (void)predDir; (void)branchTarget;
// 	UINT32 bhtIndex  = (PC >> 2) % BHT_SIZE;
// 	UINT32 phtIndex  = bht[bhtIndex];
// 	if (resolveDir == TAKEN)
// 		pht[phtIndex] = SatIncrement(pht[phtIndex], CTR_MAX);
// 	else
// 		pht[phtIndex] = SatDecrement(pht[phtIndex]);
// 	bht[bhtIndex] = ((bht[bhtIndex] << 1) & HIST_MASK);
// 	if (resolveDir == TAKEN)
// 		bht[bhtIndex] |= 1;
// }

// void PREDICTOR_free(void)
// {
// 	free(pht);
// 	free(bht);
// }

// ==================== 实验3: 2bits PC>>2 ====================
// 结果已写入 Result.xlsx Row 3

// #define TABLE_SIZE (1 << 17)
// #define CTR_MAX   3
// #define CTR_INIT  2

// UINT32 *pht;

// static inline UINT32 SatIncrement(UINT32 x, UINT32 max) { return x < max ? x + 1 : x; }
// static inline UINT32 SatDecrement(UINT32 x)             { return x > 0 ? x - 1 : x; }

// void PREDICTOR_init(void)
// {
// 	pht = (UINT32 *)malloc(TABLE_SIZE * sizeof(UINT32));
// 	for (UINT32 i = 0; i < TABLE_SIZE; i++)
// 		pht[i] = CTR_INIT;
// }

// char GetPrediction(UINT64 PC)
// {
// 	UINT32 index = (PC >> 2) % TABLE_SIZE;
// 	return pht[index] > (CTR_MAX / 2) ? TAKEN : NOT_TAKEN;
// }

// void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
// {
// 	(void)opType; (void)predDir; (void)branchTarget;
// 	UINT32 index = (PC >> 2) % TABLE_SIZE;
// 	if (resolveDir == TAKEN)
// 		pht[index] = SatIncrement(pht[index], CTR_MAX);
// 	else
// 		pht[index] = SatDecrement(pht[index]);
// }

// void PREDICTOR_free(void)
// {
// 	free(pht);
// }

// ==================== 实验2: static (总是预测Taken) ====================
// 结果已写入 Result.xlsx Row 2

// void PREDICTOR_init(void)
// {
// }

// char GetPrediction(UINT64 PC)
// {
// 	(void)PC;
// 	return TAKEN;
// }

// void  UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
// {
// 	(void)PC; (void)opType; (void)resolveDir; (void)predDir; (void)branchTarget;
// }

// void PREDICTOR_free(void)
// {
// }
