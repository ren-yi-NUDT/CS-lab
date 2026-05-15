///////////////////////////////////////////////////////////////////////
////  Set-Associative Multi-Level Cache Implementation              //
////  L1D(16KB,4-way) + Victim(4KB,FA) + L2(1MB,8-way) + L1I(16KB) //
////  + Hardware Next-Line Prefetcher                               //
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

#define DEBUG 0

#define GET_POWER_OF_2(X)	(X == 0x00		? 0 : \
							X == 0x01		? 0 : \
							X == 0x02		? 1 : \
							X == 0x04		? 2 : \
							X == 0x08		? 3 : \
							X == 0x10		? 4 : \
							X == 0x20		? 5 : \
							X == 0x40		? 6 : \
							X == 0x80		? 7 : \
							X == 0x100		? 8 : \
							X == 0x200		? 9 : \
							X == 0x400		? 10 : \
							X == 0x800		? 11 : \
							X == 0x1000		? 12 : \
							X == 0x2000		? 13 : \
							X == 0x4000		? 14 : \
							X == 0x8000		? 15 : \
							X == 0x10000	? 16 : \
							X == 0x20000	? 17 : \
							X == 0x40000	? 18 : \
							X == 0x80000	? 19 : \
							X == 0x100000	? 20 : \
							X == 0x200000	? 21 : \
							X == 0x400000	? 22 : \
							X == 0x800000	? 23 : \
							X == 0x1000000	? 24 : \
							X == 0x2000000	? 25 : \
							X == 0x4000000	? 26 : \
							X == 0x8000000	? 27 : \
							X == 0x10000000	? 28 : \
							X == 0x20000000	? 29 : \
							X == 0x40000000	? 30 : \
							X == 0x80000000	? 31 : \
							X == 0x100000000? 32 : 0)

/* ============================================================
 * 所有缓存行统一为64字节
 * ============================================================ */
#define BLOCK_SIZE		64
#define BLOCK_BITS		GET_POWER_OF_2(BLOCK_SIZE)
#define BLOCK_MASK		((UINT64)(BLOCK_SIZE - 1))

/* ============================================================
 * L1 Data Cache: 16KB, 8路组相联, LRU替换
 * 32组 × 8路 = 256行 × 64字节 = 16KB
 * ============================================================ */
#define L1D_SIZE		16384
#define L1D_ASSOC		8
#define L1D_NUM_SETS	(L1D_SIZE / (BLOCK_SIZE * L1D_ASSOC))
#define L1D_SET_BITS	GET_POWER_OF_2(L1D_NUM_SETS)
#define L1D_SET_MASK	((UINT64)(L1D_NUM_SETS - 1))

struct L1DLine {
	UINT8	Valid;
	UINT8	Dirty;
	UINT64	Tag;
	UINT64	LastAccess;
	UINT8	Data[BLOCK_SIZE];
};

static struct L1DLine L1DCache[L1D_NUM_SETS][L1D_ASSOC];
static UINT64 L1DCounter;

/* ============================================================
 * Victim Cache: 4KB, 全相联, LRU替换
 * 64个条目 × 64字节 = 4KB
 * ============================================================ */
#define VICTIM_SIZE		4096
#define VICTIM_ENTRIES	(VICTIM_SIZE / BLOCK_SIZE)

struct VictimLine {
	UINT8	Valid;
	UINT8	Dirty;
	UINT64	BlockAddr;	/* Address >> BLOCK_BITS */
	UINT64	LastAccess;
	UINT8	Data[BLOCK_SIZE];
};

static struct VictimLine VCache[VICTIM_ENTRIES];
static UINT64 VCounter;

/* ============================================================
 * L2 Unified Cache: 1MB, 8路组相联, LRU替换
 * 2048组 × 8路 = 16384行 × 64字节 = 1MB
 * ============================================================ */
#define L2_SIZE			1048576
#define L2_ASSOC		8
#define L2_NUM_SETS		(L2_SIZE / (BLOCK_SIZE * L2_ASSOC))
#define L2_SET_BITS		GET_POWER_OF_2(L2_NUM_SETS)
#define L2_SET_MASK		((UINT64)(L2_NUM_SETS - 1))

struct L2Line {
	UINT8	Valid;
	UINT8	Dirty;
	UINT64	Tag;
	UINT64	LastAccess;
	UINT8	Data[BLOCK_SIZE];
};

static struct L2Line L2Cache[L2_NUM_SETS][L2_ASSOC];
static UINT64 L2Counter;

/* ============================================================
 * L1 Instruction Cache: 16KB, 8路组相联, LRU替换
 * ============================================================ */
#define L1I_SIZE		16384
#define L1I_ASSOC		8
#define L1I_NUM_SETS	(L1I_SIZE / (BLOCK_SIZE * L1I_ASSOC))
#define L1I_SET_BITS	GET_POWER_OF_2(L1I_NUM_SETS)
#define L1I_SET_MASK	((UINT64)(L1I_NUM_SETS - 1))

struct L1ILine {
	UINT8	Valid;
	UINT64	Tag;
	UINT64	LastAccess;
	UINT8	Data[BLOCK_SIZE];
};

static struct L1ILine L1ICache[L1I_NUM_SETS][L1I_ASSOC];
static UINT64 L1ICounter;

/* ============================================================
 * 通用辅助函数
 * ============================================================ */

static void LoadLineFromMemory(UINT64 Address, UINT8 *Data)
{
	UINT64 AlignAddr = Address & ~BLOCK_MASK;
	UINT64 *pp = (UINT64 *)Data;
	UINT32 i;
	for (i = 0; i < BLOCK_SIZE / 8; i++)
		pp[i] = ReadMemory(AlignAddr + 8LL * i);
}

static void StoreLineToMemory(UINT64 Address, UINT8 *Data)
{
	UINT64 AlignAddr = Address & ~BLOCK_MASK;
	UINT64 *pp = (UINT64 *)Data;
	UINT32 i;
	for (i = 0; i < BLOCK_SIZE / 8; i++)
		WriteMemory(AlignAddr + 8LL * i, pp[i]);
}

static UINT64 ReadFromLine(const UINT8 *Data, UINT8 Offset, UINT8 Size)
{
	UINT64 val = 0;
	switch (Size)
	{
	case 1:
		val = Data[Offset];
		break;
	case 2:
		Offset &= 0xFE;
		val = Data[Offset + 1]; val <<= 8;
		val |= Data[Offset];
		break;
	case 4:
		Offset &= 0xFC;
		val = Data[Offset + 3]; val <<= 8;
		val |= Data[Offset + 2]; val <<= 8;
		val |= Data[Offset + 1]; val <<= 8;
		val |= Data[Offset];
		break;
	case 8:
		Offset &= 0xF8;
		val = Data[Offset + 7]; val <<= 8;
		val |= Data[Offset + 6]; val <<= 8;
		val |= Data[Offset + 5]; val <<= 8;
		val |= Data[Offset + 4]; val <<= 8;
		val |= Data[Offset + 3]; val <<= 8;
		val |= Data[Offset + 2]; val <<= 8;
		val |= Data[Offset + 1]; val <<= 8;
		val |= Data[Offset];
		break;
	}
	return val;
}

static void WriteToLine(UINT8 *Data, UINT8 Offset, UINT8 Size, UINT64 Value)
{
	switch (Size)
	{
	case 1:
		Data[Offset] = Value & 0xFF;
		break;
	case 2:
		Offset &= 0xFE;
		Data[Offset + 0] = Value & 0xFF; Value >>= 8;
		Data[Offset + 1] = Value & 0xFF;
		break;
	case 4:
		Offset &= 0xFC;
		Data[Offset + 0] = Value & 0xFF; Value >>= 8;
		Data[Offset + 1] = Value & 0xFF; Value >>= 8;
		Data[Offset + 2] = Value & 0xFF; Value >>= 8;
		Data[Offset + 3] = Value & 0xFF;
		break;
	case 8:
		Offset &= 0xF8;
		Data[Offset + 0] = Value & 0xFF; Value >>= 8;
		Data[Offset + 1] = Value & 0xFF; Value >>= 8;
		Data[Offset + 2] = Value & 0xFF; Value >>= 8;
		Data[Offset + 3] = Value & 0xFF; Value >>= 8;
		Data[Offset + 4] = Value & 0xFF; Value >>= 8;
		Data[Offset + 5] = Value & 0xFF; Value >>= 8;
		Data[Offset + 6] = Value & 0xFF; Value >>= 8;
		Data[Offset + 7] = Value & 0xFF;
		break;
	}
}

/* ============================================================
 * L2 Cache 操作
 * ============================================================ */

static void L2Insert(UINT64 Address, UINT8 Dirty, const UINT8 *Data)
{
	UINT32 set = (UINT32)((Address >> BLOCK_BITS) & L2_SET_MASK);
	UINT64 tag = Address >> (BLOCK_BITS + L2_SET_BITS);
	int way;
	UINT64 min_access;
	int w;

	/* 优先选择无效路 */
	for (w = 0; w < L2_ASSOC; w++)
	{
		if (!L2Cache[set][w].Valid)
		{
			way = w;
			goto l2_insert_place;
		}
	}

	/* 所有路均有效，选择LRU */
	way = 0;
	min_access = L2Cache[set][0].LastAccess;
	for (w = 1; w < L2_ASSOC; w++)
	{
		if (L2Cache[set][w].LastAccess < min_access)
		{
			min_access = L2Cache[set][w].LastAccess;
			way = w;
		}
	}

	/* 淘汰：脏行写回内存 */
	if (L2Cache[set][way].Dirty)
	{
		UINT64 old_addr = ((L2Cache[set][way].Tag << L2_SET_BITS) | (UINT64)set) << BLOCK_BITS;
		StoreLineToMemory(old_addr, L2Cache[set][way].Data);
	}

l2_insert_place:
	L2Cache[set][way].Valid = 1;
	L2Cache[set][way].Dirty = Dirty;
	L2Cache[set][way].Tag = tag;
	L2Cache[set][way].LastAccess = ++L2Counter;
	memcpy(L2Cache[set][way].Data, Data, BLOCK_SIZE);
}

/* 在L2中查找，命中时拷贝数据并使该条目无效（独占缓存模型） */
static int L2Lookup(UINT64 Address, UINT8 *OutData, UINT8 *OutDirty)
{
	UINT32 set = (UINT32)((Address >> BLOCK_BITS) & L2_SET_MASK);
	UINT64 tag = Address >> (BLOCK_BITS + L2_SET_BITS);
	int w;

	for (w = 0; w < L2_ASSOC; w++)
	{
		if (L2Cache[set][w].Valid && L2Cache[set][w].Tag == tag)
		{
			memcpy(OutData, L2Cache[set][w].Data, BLOCK_SIZE);
			if (OutDirty) *OutDirty = L2Cache[set][w].Dirty;
			L2Cache[set][w].Valid = 0;
			return 1;
		}
	}
	return 0;
}

/* ============================================================
 * Victim Cache 操作
 * ============================================================ */

static void VictimInsert(UINT64 Address, UINT8 Dirty, const UINT8 *Data)
{
	UINT64 block_addr = Address >> BLOCK_BITS;
	int entry;
	UINT64 min_access;
	int e;

	/* 优先选择无效条目 */
	for (e = 0; e < VICTIM_ENTRIES; e++)
	{
		if (!VCache[e].Valid)
		{
			entry = e;
			goto victim_insert_place;
		}
	}

	/* 全满，选择LRU */
	entry = 0;
	min_access = VCache[0].LastAccess;
	for (e = 1; e < VICTIM_ENTRIES; e++)
	{
		if (VCache[e].LastAccess < min_access)
		{
			min_access = VCache[e].LastAccess;
			entry = e;
		}
	}

	/* 淘汰：移入L2 */
	{
		UINT64 evict_addr = VCache[entry].BlockAddr << BLOCK_BITS;
		L2Insert(evict_addr, VCache[entry].Dirty, VCache[entry].Data);
	}

victim_insert_place:
	VCache[entry].Valid = 1;
	VCache[entry].Dirty = Dirty;
	VCache[entry].BlockAddr = block_addr;
	VCache[entry].LastAccess = ++VCounter;
	memcpy(VCache[entry].Data, Data, BLOCK_SIZE);
}

static int VictimLookup(UINT64 Address, UINT8 *OutData, UINT8 *OutDirty)
{
	UINT64 block_addr = Address >> BLOCK_BITS;
	int e;

	for (e = 0; e < VICTIM_ENTRIES; e++)
	{
		if (VCache[e].Valid && VCache[e].BlockAddr == block_addr)
		{
			memcpy(OutData, VCache[e].Data, BLOCK_SIZE);
			if (OutDirty) *OutDirty = VCache[e].Dirty;
			VCache[e].Valid = 0;
			return 1;
		}
	}
	return 0;
}

/* ============================================================
 * 硬件预取：Next-Line Prefetcher
 * 未命中时，提前把下一个缓存行载入 L1，标记最低 LRU 优先级
 * ============================================================ */

/* 检查某地址是否已存在于任一级缓存中 */
static int IsCachedAnywhere(UINT64 Address)
{
	UINT64 block_addr = Address >> BLOCK_BITS;
	UINT32 l1d_set = (UINT32)((Address >> BLOCK_BITS) & L1D_SET_MASK);
	UINT64 l1d_tag = Address >> (BLOCK_BITS + L1D_SET_BITS);
	UINT32 l2_set = (UINT32)((Address >> BLOCK_BITS) & L2_SET_MASK);
	UINT64 l2_tag = Address >> (BLOCK_BITS + L2_SET_BITS);
	int w, e;

	for (w = 0; w < L1D_ASSOC; w++)
		if (L1DCache[l1d_set][w].Valid && L1DCache[l1d_set][w].Tag == l1d_tag)
			return 1;
	for (e = 0; e < VICTIM_ENTRIES; e++)
		if (VCache[e].Valid && VCache[e].BlockAddr == block_addr)
			return 1;
	for (w = 0; w < L2_ASSOC; w++)
		if (L2Cache[l2_set][w].Valid && L2Cache[l2_set][w].Tag == l2_tag)
			return 1;
	return 0;
}

/* 将一行数据载入 L1D（预取专用），LastAccess=0 保证最低淘汰优先级 */
static void PrefetchIntoL1D(UINT64 Address)
{
	UINT32 set = (UINT32)((Address >> BLOCK_BITS) & L1D_SET_MASK);
	UINT64 tag = Address >> (BLOCK_BITS + L1D_SET_BITS);
	UINT8 TempData[BLOCK_SIZE];
	int w, evict_way;
	UINT64 min_access;

	/* 跳过已缓存的行 */
	if (IsCachedAnywhere(Address))
		return;

	/* 从内存加载 */
	LoadLineFromMemory(Address, TempData);

	/* 淘汰 LRU 行 */
	evict_way = -1;
	for (w = 0; w < L1D_ASSOC; w++)
	{
		if (!L1DCache[set][w].Valid)
		{
			evict_way = w;
			break;
		}
	}
	if (evict_way == -1)
	{
		evict_way = 0;
		min_access = L1DCache[set][0].LastAccess;
		for (w = 1; w < L1D_ASSOC; w++)
		{
			if (L1DCache[set][w].LastAccess < min_access)
			{
				min_access = L1DCache[set][w].LastAccess;
				evict_way = w;
			}
		}
		{
			UINT64 old_addr = ((L1DCache[set][evict_way].Tag << L1D_SET_BITS) | (UINT64)set) << BLOCK_BITS;
			VictimInsert(old_addr, L1DCache[set][evict_way].Dirty, L1DCache[set][evict_way].Data);
		}
	}

	/* 放入 L1D，LastAccess=0 使其优先被淘汰（如果没被使用的话） */
	memcpy(L1DCache[set][evict_way].Data, TempData, BLOCK_SIZE);
	L1DCache[set][evict_way].Valid = 1;
	L1DCache[set][evict_way].Dirty = 0;
	L1DCache[set][evict_way].Tag = tag;
	L1DCache[set][evict_way].LastAccess = 0;
}

/* 检查指令地址是否已在 L1I 或 L2 中 */
static int IsInstCached(UINT64 Address)
{
	UINT32 l1i_set = (UINT32)((Address >> BLOCK_BITS) & L1I_SET_MASK);
	UINT64 l1i_tag = Address >> (BLOCK_BITS + L1I_SET_BITS);
	UINT32 l2_set = (UINT32)((Address >> BLOCK_BITS) & L2_SET_MASK);
	UINT64 l2_tag = Address >> (BLOCK_BITS + L2_SET_BITS);
	int w;

	for (w = 0; w < L1I_ASSOC; w++)
		if (L1ICache[l1i_set][w].Valid && L1ICache[l1i_set][w].Tag == l1i_tag)
			return 1;
	for (w = 0; w < L2_ASSOC; w++)
		if (L2Cache[l2_set][w].Valid && L2Cache[l2_set][w].Tag == l2_tag)
			return 1;
	return 0;
}

/* 将一行数据载入 L1I（预取专用） */
static void PrefetchIntoL1I(UINT64 Address)
{
	UINT32 set = (UINT32)((Address >> BLOCK_BITS) & L1I_SET_MASK);
	UINT64 tag = Address >> (BLOCK_BITS + L1I_SET_BITS);
	UINT8 TempData[BLOCK_SIZE];
	int w, evict_way;
	UINT64 min_access;

	if (IsInstCached(Address))
		return;

	if (!L2Lookup(Address, TempData, NULL))
		LoadLineFromMemory(Address, TempData);

	evict_way = -1;
	for (w = 0; w < L1I_ASSOC; w++)
	{
		if (!L1ICache[set][w].Valid)
		{
			evict_way = w;
			break;
		}
	}
	if (evict_way == -1)
	{
		evict_way = 0;
		min_access = L1ICache[set][0].LastAccess;
		for (w = 1; w < L1I_ASSOC; w++)
		{
			if (L1ICache[set][w].LastAccess < min_access)
			{
				min_access = L1ICache[set][w].LastAccess;
				evict_way = w;
			}
		}
		{
			UINT64 old_addr = ((L1ICache[set][evict_way].Tag << L1I_SET_BITS) | (UINT64)set) << BLOCK_BITS;
			L2Insert(old_addr, 0, L1ICache[set][evict_way].Data);
		}
	}

	memcpy(L1ICache[set][evict_way].Data, TempData, BLOCK_SIZE);
	L1ICache[set][evict_way].Valid = 1;
	L1ICache[set][evict_way].Tag = tag;
	L1ICache[set][evict_way].LastAccess = 0;
}

/* ============================================================
 * 初始化函数
 * ============================================================ */

void InitDataCache()
{
	printf("[%s] +--------------------------------------------------+\n", __func__);
	printf("[%s] | L1D(16KB,4-way) + Victim(4KB,FA) + L2(1MB,8-way) |\n", __func__);
	printf("[%s] +--------------------------------------------------+\n", __func__);

	memset(L1DCache, 0, sizeof(L1DCache));
	memset(VCache, 0, sizeof(VCache));
	memset(L2Cache, 0, sizeof(L2Cache));
	L1DCounter = 0;
	VCounter = 0;
	L2Counter = 0;
}

void InitInstCache()
{
	memset(L1ICache, 0, sizeof(L1ICache));
	L1ICounter = 0;
}

/* ============================================================
 * Data Cache 访问接口
 * 层次: L1D -> Victim -> L2 -> Memory
 * ============================================================ */

UINT8 AccessDataCache(UINT64 Address, UINT8 Operation, UINT8 DataSize, UINT64 StoreValue, UINT64 *LoadResult)
{
	UINT8 BlockOffset = (UINT8)(Address & BLOCK_MASK);
	UINT32 set = (UINT32)((Address >> BLOCK_BITS) & L1D_SET_MASK);
	UINT64 tag = Address >> (BLOCK_BITS + L1D_SET_BITS);
	int w;

	*LoadResult = 0;

	/* --- Step 1: 查找L1D --- */
	for (w = 0; w < L1D_ASSOC; w++)
	{
		if (L1DCache[set][w].Valid && L1DCache[set][w].Tag == tag)
		{
			/* 命中 */
			L1DCache[set][w].LastAccess = ++L1DCounter;

			if (Operation == 'L')
			{
				*LoadResult = ReadFromLine(L1DCache[set][w].Data, BlockOffset, DataSize);
			}
			else
			{
				WriteToLine(L1DCache[set][w].Data, BlockOffset, DataSize, StoreValue);
				L1DCache[set][w].Dirty = 1;
			}
			return 'H';
		}
	}

	/* --- 未命中：从下级缓存加载数据 --- */
	UINT8 TempData[BLOCK_SIZE];
	UINT8 loaded_dirty = 0;

	if (VictimLookup(Address, TempData, &loaded_dirty))
	{
		/* Victim命中 */
	}
	else if (L2Lookup(Address, TempData, &loaded_dirty))
	{
		/* L2命中 */
	}
	else
	{
		/* 从内存加载 */
		LoadLineFromMemory(Address, TempData);
		loaded_dirty = 0;
	}

	/* --- 淘汰L1D的LRU行 --- */
	int evict_way = -1;
	for (w = 0; w < L1D_ASSOC; w++)
	{
		if (!L1DCache[set][w].Valid)
		{
			evict_way = w;
			break;
		}
	}
	if (evict_way == -1)
	{
		UINT64 min_access = L1DCache[set][0].LastAccess;
		evict_way = 0;
		for (w = 1; w < L1D_ASSOC; w++)
		{
			if (L1DCache[set][w].LastAccess < min_access)
			{
				min_access = L1DCache[set][w].LastAccess;
				evict_way = w;
			}
		}

		/* 被淘汰行移入Victim Cache */
		{
			UINT64 old_addr = ((L1DCache[set][evict_way].Tag << L1D_SET_BITS) | (UINT64)set) << BLOCK_BITS;
			VictimInsert(old_addr, L1DCache[set][evict_way].Dirty, L1DCache[set][evict_way].Data);
		}
	}

	/* --- 将新数据放入L1D --- */
	memcpy(L1DCache[set][evict_way].Data, TempData, BLOCK_SIZE);
	L1DCache[set][evict_way].Valid = 1;
	L1DCache[set][evict_way].Dirty = loaded_dirty;
	L1DCache[set][evict_way].Tag = tag;
	L1DCache[set][evict_way].LastAccess = ++L1DCounter;

	/* --- 执行访存操作 --- */
	if (Operation == 'L')
	{
		*LoadResult = ReadFromLine(L1DCache[set][evict_way].Data, BlockOffset, DataSize);
	}
	else
	{
		WriteToLine(L1DCache[set][evict_way].Data, BlockOffset, DataSize, StoreValue);
		L1DCache[set][evict_way].Dirty = 1;
	}

	/* --- 预取后续行 --- */
	PrefetchIntoL1D((Address & ~BLOCK_MASK) + BLOCK_SIZE);
	PrefetchIntoL1D((Address & ~BLOCK_MASK) + 2 * BLOCK_SIZE);

	return 'M';
}

/* ============================================================
 * Instruction Cache 访问接口
 * 层次: L1I -> L2 -> Memory (指令只读，无需Victim/脏位)
 * ============================================================ */

UINT8 AccessInstCache(UINT64 Address, UINT8 Operation, UINT8 InstSize, UINT64 *InstResult)
{
	UINT8 BlockOffset = (UINT8)(Address & BLOCK_MASK);
	UINT32 set = (UINT32)((Address >> BLOCK_BITS) & L1I_SET_MASK);
	UINT64 tag = Address >> (BLOCK_BITS + L1I_SET_BITS);
	int w;

	*InstResult = 0;

	/* --- Step 1: 查找L1I --- */
	for (w = 0; w < L1I_ASSOC; w++)
	{
		if (L1ICache[set][w].Valid && L1ICache[set][w].Tag == tag)
		{
			/* 命中 */
			L1ICache[set][w].LastAccess = ++L1ICounter;
			*InstResult = ReadFromLine(L1ICache[set][w].Data, BlockOffset, InstSize);
			return 'H';
		}
	}

	/* --- 未命中 --- */
	UINT8 TempData[BLOCK_SIZE];

	if (!L2Lookup(Address, TempData, NULL))
	{
		LoadLineFromMemory(Address, TempData);
	}

	/* --- 淘汰L1I的LRU行 --- */
	int evict_way = -1;
	for (w = 0; w < L1I_ASSOC; w++)
	{
		if (!L1ICache[set][w].Valid)
		{
			evict_way = w;
			break;
		}
	}
	if (evict_way == -1)
	{
		UINT64 min_access = L1ICache[set][0].LastAccess;
		evict_way = 0;
		for (w = 1; w < L1I_ASSOC; w++)
		{
			if (L1ICache[set][w].LastAccess < min_access)
			{
				min_access = L1ICache[set][w].LastAccess;
				evict_way = w;
			}
		}

		/* 指令淘汰行移入L2（指令只读，不脏） */
		{
			UINT64 old_addr = ((L1ICache[set][evict_way].Tag << L1I_SET_BITS) | (UINT64)set) << BLOCK_BITS;
			L2Insert(old_addr, 0, L1ICache[set][evict_way].Data);
		}
	}

	/* --- 将新数据放入L1I --- */
	memcpy(L1ICache[set][evict_way].Data, TempData, BLOCK_SIZE);
	L1ICache[set][evict_way].Valid = 1;
	L1ICache[set][evict_way].Tag = tag;
	L1ICache[set][evict_way].LastAccess = ++L1ICounter;

	/* --- 读取指令 --- */
	*InstResult = ReadFromLine(L1ICache[set][evict_way].Data, BlockOffset, InstSize);

	/* --- 预取后续指令行（指令几乎总是顺序执行） --- */
	{
		UINT64 base = Address & ~BLOCK_MASK;
		PrefetchIntoL1I(base + BLOCK_SIZE);
		PrefetchIntoL1I(base + 2 * BLOCK_SIZE);
	}

	return 'M';
}
