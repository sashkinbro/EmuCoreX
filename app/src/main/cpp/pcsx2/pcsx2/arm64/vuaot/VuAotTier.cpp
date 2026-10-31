// SPDX-FileCopyrightText: 2026 ARMSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// Adapted for EmuCoreX oaknut from ARMSX2 aa52a1fdc7cd03fb02bd404ebaa9940944e7fc60.

#include "VuAotTier.h"

#include "VuAotOaknut.h"

#include <cstdio>
#include <cstring>

namespace vuaot
{
	bool BodyTier::Decline(const std::string& why, std::string* out)
	{
		declined_[why]++;
		if (out)
			*out = why;
		return false;
	}

	bool BodyTier::TryLower(const MvuBlockTrace& b, const MvuEmitEnv& env, std::string* why)
	{
		if (b.vuIndex != vu_)
			return false;
		blocks_++;
		if (!an_)
		{
			PipeConfig cfg;
			cfg.vuIndex = vu_;
			cfg.flagHack = env.flagHack;
			cfg.xgkickHack = env.xgkickHack;
			cfg.ibitHack = env.ibitHack;
			an_ = std::make_unique<PipeAnalyzer>(micro_, cfg);
		}
		// Every block goes through the port, declined or not, so its per-slot
		// history follows microVU's compile order.
		PipeState st;
		std::memcpy(st.bytes, b.entryState, 96);
		block_ = an_->Analyze(b.startPC, st);
		const BlockResult& br = block_;
		MvuBlockTrace ours;
		br.ToTrace(ours, vu_);
		if (!CompareBlockTraces(b, ours).empty())
			return Decline("timing port disagrees", why);
		if (env.vuIndex != 1)
			return Decline("VU0", why);
		if (env.clampExact)
			return Decline("clamp mode 4", why);
		if (env.xgkickHack)
			return Decline("XGKickHack", why);
		if (env.ibitHack)
			return Decline("IbitHack", why);
		if (env.overflowHack)
			return Decline("VUOverflowHack", why);
		if (env.vuSyncHack)
			return Decline("VU sync gamefix", why);
		if (env.addSubHack)
			return Decline("VuAddSubHack", why);
		const s32 index = static_cast<s32>(blocks_) - 1;
		if ((only >= 0 && index != only) || (limit >= 0 && index >= limit) || skip.count(index))
			return Decline("bisect", why);

        if (regionEnds)
        {
            if (br.entry.blockType || br.tBitOnBranch || !br.tBitExits.empty())
                return Decline("body requires baseline runtime boundary", why);
            for (const auto& op : br.ops)
                if (op.uOp.tBit || op.uOp.dBit || op.uOp.mBit || op.lOp.badBranch || op.lOp.evilBranch ||
                    (op.uOp.eBit && br.endKind != BlockEnd::EBit))
                    return Decline("body requires baseline runtime boundary", why);
        }

		ir::BuildOptions bo;
		bo.vuIndex = vu_;
		bo.clampIImmediate = env.clampOverflow;
		bo.remapAtLeave = remapAtLeave;
		bo.regionEnds = regionEnds;
		bo.clampExtra = env.clampExtra;
		bo.clampSign = env.clampSign;
		const ir::Function fn = ir::BuildNodeLocal(br, bo);
		end_ = fn.end;
		LowerEnv le;
		le.vuIndex = env.vuIndex;
		le.offDivFlag = env.offDivFlag;
		le.offVIbackup = env.offVIbackup;
		le.offVIxgkick = env.offVIxgkick;
		le.offBranch = env.offBranch;
		le.offNeonBackup = env.offNeonBackup;
		le.poolLimitV = poolLimitV;
		le.poolLimitG = poolLimitG;
		le.xgkickFn = env.xgkickFn;
		le.vifTop = env.vifTop;
		le.vifItop = env.vifItop;
		le.clampOverflow = env.clampOverflow;
		le.clampExtra = env.clampExtra;
		le.clampSign = env.clampSign;
		le.softwareFlush = env.softwareFlush;
		le.schedule = schedule;
		le.vuMemPinned = env.vuMemPinned;
		const u8* codeStart = armGetCurrentCodePointer();
		std::vector<std::pair<const u8*, ir::ValueId>> marks;
		if (dump)
			g_lowerMarks = &marks;
		const LowerResult r = LowerNodeBody(fn, le);
		g_lowerMarks = nullptr;
		if (!r.ok)
			return Decline(r.why, why);
		if (dump)
		{
			ProgramGraph g;
			g.nodes.emplace_back();
			g.nodes.back().pc = br.startPC;
			g.nodes.back().entry = br.entry;
			g.nodes.back().block = br;
			std::fputs(ir::Dump(fn, g).c_str(), stdout);
			std::fflush(stdout);
			for (u32 offset = 0; offset < r.bytes; offset += 4)
			{
				u32 word;
				std::memcpy(&word, codeStart + offset, sizeof(word));
				std::printf("  %04x: %08x\n", offset, word);
			}
			std::fflush(stdout);
		}
		lowered_++;
		bytes_ += r.bytes;
		return true;
	}
} // namespace vuaot
