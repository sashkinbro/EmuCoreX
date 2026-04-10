// SPDX-FileCopyrightText: 2002-2025 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "BaseblockEx.h"

BASEBLOCKEX* BaseBlocks::New(u32 startpc, uptr fnptr)
{
	std::pair<linkiter_t, linkiter_t> range = links.equal_range(startpc);
	for (auto i = range.first; i != range.second; ++i) {
//        *(u32 *) i->second = fnptr - (i->second + 4);
        armEmitJmpPtr((void*)(i->second), (void*)fnptr, true);
    }

	return blocks.insert(startpc, fnptr);
}

int BaseBlocks::LastIndex(u32 startpc) const
{
	if (0 == blocks.size())
		return -1;

	int imin = 0, imax = blocks.size() - 1;

	while (imin != imax)
	{
		const int imid = (imin + imax + 1) >> 1;

		if (blocks[imid].startpc > startpc)
			imax = imid - 1;
		else
			imin = imid;
	}

	return imin;
}

void BaseBlocks::Link(u32 pc, s32* jumpptr)
{
	BASEBLOCKEX* targetblock = Get(pc);
	if (targetblock && targetblock->startpc == pc) {
//        *jumpptr = (s32) (targetblock->fnptr - (sptr) (jumpptr + 1));
        armEmitJmpPtr(jumpptr, (void*)targetblock->fnptr);
    }
	else {
//        *jumpptr = (s32) (recompiler - (sptr) (jumpptr + 1));
        armEmitJmpPtr(jumpptr, (void*)recompiler);
    }
	links.insert(std::pair<u32, uptr>(pc, (uptr)jumpptr));
}
