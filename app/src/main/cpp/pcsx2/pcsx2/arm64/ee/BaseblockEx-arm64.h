// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

#include "arm64/OaknutHelpers-arm64.h"
#include "common/Assertions.h"

// Every potential jump point in the PS2's addressable memory has a BASEBLOCK
// associated with it. So that means a BASEBLOCK for every 4 bytes of PS2
// addressable memory.  Yay!
struct BASEBLOCK
{
	uptr m_pFnptr;

	__inline uptr GetFnptr() const { return m_pFnptr; }
	void __inline SetFnptr(uptr ptr) { m_pFnptr = ptr; }
};

// extra block info (only valid for start of fn)
struct BASEBLOCKEX
{
	uptr fnptr;
	u32 startpc;
	u32 size;    // The size in dwords (equivalent to the number of instructions)
	u32 x86size; // The size in byte of the translated x86 instructions
	u64 execution_count;

#ifdef PCSX2_DEVBUILD
	// Could be useful to instrument the block
	//u32 visited; // number of times called
	//u64 ltime; // regs it assumes to have set already
#endif
};

class BaseBlockArray
{
	s32 mReserved;
	s32 mSize;
	BASEBLOCKEX* blocks;

	__fi void resize(s32 size)
	{
		pxAssert(size > 0);
		auto* newMem = new BASEBLOCKEX[size];
		if (blocks)
		{
			memcpy(newMem, blocks, mSize * sizeof(BASEBLOCKEX));
			delete[] blocks;
            blocks = nullptr;
		}
		blocks = newMem;
		pxAssert(blocks != NULL);
	}

	void reserve(s32 size)
	{
		resize(size);
        mReserved = size;
	}

public:
	~BaseBlockArray()
	{
		if (blocks) {
            delete[] blocks;
            blocks = nullptr;
        }
	}

	explicit BaseBlockArray(s32 size)
		: mReserved(0)
		, mSize(0)
		, blocks(nullptr)
	{
		reserve(size);
	}

	BASEBLOCKEX* insert(u32 startpc, uptr fnptr)
	{
		if (mSize + 1 >= mReserved)
		{
			reserve(mReserved > 0 ? mReserved * 2 : 0x2000); // some games requires even more!
		}

		// Insert the the new BASEBLOCKEX by startpc order
		int imin = 0, imax = mSize, imid;

		while (imin < imax)
		{
			imid = (imin + imax) >> 1;

			if (blocks[imid].startpc > startpc)
				imax = imid;
			else
				imin = imid + 1;
		}

		pxAssert(imin == mSize || blocks[imin].startpc > startpc);

		if (imin < mSize)
		{
			// make a hole for a new block.
			memmove(blocks + imin + 1, blocks + imin, (mSize - imin) * sizeof(BASEBLOCKEX));
		}

		memset((blocks + imin), 0, sizeof(BASEBLOCKEX));
		blocks[imin].startpc = startpc;
		blocks[imin].fnptr = fnptr;

		mSize++;
		return &blocks[imin];
	}

	__fi BASEBLOCKEX& operator[](int idx) const
	{
		return *(blocks + idx);
	}

	void clear()
	{
		mSize = 0;
	}

	[[nodiscard]] __fi u32 size() const
	{
		return mSize;
	}

	__fi void erase(s32 first, s32 last)
	{
		int range = last - first;

		if (last < mSize)
		{
			memmove(blocks + first, blocks + last, (mSize - last) * sizeof(BASEBLOCKEX));
		}

		mSize -= range;
	}
};

class BaseBlocks
{
protected:
	typedef std::multimap<u32, uptr>::iterator linkiter_t;

	// switch to a hash map later?
	std::multimap<u32, uptr> links;
	uptr recompiler;
	BaseBlockArray blocks;

public:
	BaseBlocks()
		: recompiler(0)
		, blocks(0x4000)
	{
	}

	void SetJITCompile(const void *recompiler_)
	{
		recompiler = reinterpret_cast<uptr>(recompiler_);
	}

	BASEBLOCKEX* New(u32 startpc, uptr fnptr);
	[[nodiscard]] int LastIndex(u32 startpc) const;
	//BASEBLOCKEX* GetByX86(uptr ip);

	[[nodiscard]] __fi int Index(u32 startpc) const
	{
		const int idx = LastIndex(startpc);
		if (idx < 0)
			return -1;

		const u32 block_startpc = blocks[idx].startpc;
		const u32 block_size = blocks[idx].size;

		if ((startpc < block_startpc) ||
			(block_size && (startpc >= block_startpc + (block_size << 2))))
			return -1;
		else
			return idx;
	}

	[[nodiscard]] __fi u32 GetBlockCount() const
	{
		return blocks.size();
	}

	__fi BASEBLOCKEX* operator[](int idx)
	{
		if (idx < 0 || idx >= (int)blocks.size())
			return nullptr;

		return &blocks[idx];
	}

	__fi BASEBLOCKEX* Get(u32 startpc)
	{
		return (*this)[Index(startpc)];
	}

	__fi void Remove(int first, int last)
	{
		pxAssert(first <= last);
		int idx = first;
		do
		{
			pxAssert(idx <= last);

			//u32 startpc = blocks[idx].startpc;
			auto range = links.equal_range(blocks[idx].startpc);
			for (auto i = range.first; i != range.second; ++i) {
//                *(u32 *) i->second = recompiler - (i->second + 4);
                oakEmitJmpPtr((void*)i->second, (void*)recompiler, true);
            }

			if (IsDevBuild)
			{
				// Clear the first instruction to 0xcc (breakpoint), as a way to assert if some
				// static jumps get left behind to this block.  Note: Do not clear more than the
				// first byte, since this code is called during exception handlers and event handlers
				// both of which expect to be able to return to the recompiled code.

				BASEBLOCKEX effu(blocks[idx]);
				memset((void*)effu.fnptr, 0xcc, 1);
			}
		} while (idx++ < last);

		// Link values are patch addresses inside the source block's generated code. Remove
		// outgoing records owned by invalidated blocks so later relinks never walk or patch
		// dead JIT code and the multimap cannot grow for the whole session.
		std::vector<std::pair<uptr, uptr>> removed_host_ranges;
		removed_host_ranges.reserve(last - first + 1);
		for (int block_index = first; block_index <= last; block_index++)
		{
			const uptr source_begin = blocks[block_index].fnptr;
			removed_host_ranges.emplace_back(source_begin, source_begin + blocks[block_index].x86size);
		}
		std::sort(removed_host_ranges.begin(), removed_host_ranges.end());

		for (auto link = links.begin(); link != links.end();)
		{
			const auto upper = std::upper_bound(removed_host_ranges.begin(), removed_host_ranges.end(), link->second,
				[](uptr address, const auto& range) { return address < range.first; });
			const bool source_was_removed = upper != removed_host_ranges.begin() &&
				link->second < std::prev(upper)->second;

			if (source_was_removed)
				link = links.erase(link);
			else
				++link;
		}
		blocks.erase(first, last + 1);
	}

	void Link(u32 pc, s32* jumpptr);

	__fi void Reset()
	{
		blocks.clear();
		links.clear();
	}
};

#define PC_GETBLOCK_(x, reclut) ((BASEBLOCK*)(reclut[((u32)(x)) >> 16] + (x) * (sizeof(BASEBLOCK) >> 2))) // (sizeof(BASEBLOCK) / 4)

/**
 * Add a page to the recompiler lookup table
 *
 * Will associate `reclut[pagebase + pageidx]` with `mapbase[mappage << 14]`
 * Will associate `hwlut[pagebase + pageidx]` with `pageidx << 16`
 */
static inline void recLUT_SetPage(uptr reclut[0x10000], u32 hwlut[0x10000],
                                  BASEBLOCK* mapbase, uint pagebase, uint pageidx, uint mappage)
{
	// this value is in 64k pages!
	uint page = pagebase + pageidx;

	pxAssert(page < 0x10000);
	reclut[page] = (uptr)&mapbase[((s32)mappage - (s32)page) << 14];
	if (hwlut)
		hwlut[page] = 0u - (pagebase << 16);
}

static_assert(sizeof(BASEBLOCK) == 8, "BASEBLOCK is not 8 bytes");
