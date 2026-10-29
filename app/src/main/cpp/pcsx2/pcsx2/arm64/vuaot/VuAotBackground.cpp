// SPDX-FileCopyrightText: 2026 ARMSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+
// Adapted for EmuCoreX oaknut from ARMSX2 aa52a1fdc7cd03fb02bd404ebaa9940944e7fc60.

#include "VuAotBackground.h"

#include "VuAotOaknut.h"
#include "common/Assertions.h"
#include "common/Threading.h"

#include <cstring>

#if defined(__linux__)
#include <sched.h>
#include <unistd.h>
#include <sys/resource.h>
#endif

namespace vuaot::bg
{


	class CodeSink::Recorder final : public ArmAddressRecorder
	{
	public:
		explicit Recorder(CodeSink& s)
			: s_(s)
		{
		}

		// An address inside the buffer (code or placeholder) gets a full four-halfword MOVZ/MOVK run,
		// the shape the copy and the patcher look for. Any other address means the same wherever the
		// code lands. The adapter records every pointer at the same fixed width.

		void OnMovRun(u8* at, u32 width, const void* addr) override
		{
			if (const s32 id = s_.PlaceholderId(addr); id >= 0)
				s_.fixups.push_back({Off(at), Fixup::SiteAbs, static_cast<u32>(id), 0});
			else if (s_.InCode(addr))
				s_.fixups.push_back({Off(at), Fixup::IntraAbs, 0, static_cast<u64>(static_cast<const u8*>(addr) - s_.base_)});
			else
				s_.fixups.push_back({Off(at), Fixup::OutsideAbs, width, reinterpret_cast<u64>(addr)});
		}


		void OnBranch(u8* at, const void* target, BranchForm form) override
		{
			// Only B and BL are relocatable: a B.cond or CBZ would need a reach check the copy cannot make.
			if (form != BranchForm::Rel26)
			{
				Fail("conditional direct branch emitted");
				return;
			}
			const s32 id = s_.PlaceholderId(target);
			if (id >= 0)
				s_.fixups.push_back({Off(at), Fixup::Site26, static_cast<u32>(id), 0});
			else if (!s_.InCode(target))
				s_.fixups.push_back({Off(at), Fixup::External26, 0, reinterpret_cast<u64>(target)});
			// (a branch to a place in the buffer moves with it)
		}

	private:
		CodeSink& s_;
		u32 Off(const u8* at) const { return static_cast<u32>(at - s_.base_); }
		void Fail(const char* why)
		{
			if (s_.failed.empty())
				s_.failed = why;
		}
	};

	CodeSink::CodeSink()
		: base_(new u8[kCapacity])
		, rec_(std::make_unique<Recorder>(*this))
	{
	}

	CodeSink::~CodeSink()
	{
		pxAssert(!masm_);
		delete[] base_;
	}

	s32 CodeSink::PlaceholderId(const void* p) const
	{
		const u8* b = static_cast<const u8*>(p);
		const u8* z = base_ + kCapacity - kPlaceholderZone;
		if (b < z || b >= base_ + kCapacity)
			return -1;
		return static_cast<s32>((b - z) / 4);
	}

	bool CodeSink::InCode(const void* p) const
	{
		const u8* b = static_cast<const u8*>(p);
		return b >= base_ && b < base_ + kCapacity - kPlaceholderZone;
	}

	void CodeSink::NoteData64(const u8* at, u64 value)
	{
		pxAssert(InCode(at));
		fixups.push_back({static_cast<u32>(at - base_), Fixup::Data64, 0, value});
	}

	void CodeSink::Begin()
	{
		pxAssertRel(!armAsm && !masm_, "vu-aot worker: assembler already bound");
		fixups.clear();
		failed.clear();
		// A private oaknut cursor; the adapter only borrows reserved x17.
		auto* m = new a64::MacroAssembler(base_, kCapacity - kPlaceholderZone);
		masm_ = m;
		armAsm = m;
		armAddressRecorder = rec_.get();
	}

	u32 CodeSink::End()
	{
		auto* m = static_cast<a64::MacroAssembler*>(masm_);
		pxAssertRel(m && armAsm == m, "vu-aot worker: assembler not bound");
		const u32 size = static_cast<u32>(m->GetSizeOfCodeGenerated());
		delete m;
		masm_ = nullptr;
		armAsm = nullptr;
		armAddressRecorder = nullptr;
		return size;
	}

	bool PatchAbsMov(u32* insn, u64 value)
	{
		constexpr u32 kOpMask = 0xFF800000u, kMovz = 0xD2800000u, kMovk = 0xF2800000u;
		for (int i = 0; i < 4; i++)
		{
			if ((insn[i] & kOpMask) != (i == 0 ? kMovz : kMovk) || (insn[i] & 0x00600000u) != (static_cast<u32>(i) << 21) ||
				(insn[i] & 31u) != (insn[0] & 31u))
				return false;
		}
		for (int i = 0; i < 4; i++)
			insn[i] = (insn[i] & ~(0xFFFFu << 5)) | (static_cast<u32>((value >> (16 * i)) & 0xFFFFu) << 5);
		return true;
	}

	bool PatchRel26(u32* insn, const u8* at, const void* target)
	{
		// B = 0x14000000, BL = 0x94000000
		if ((*insn & 0x7C000000u) != 0x14000000u)
			return false;
		const s64 bytes = static_cast<s64>(reinterpret_cast<uintptr_t>(target)) - static_cast<s64>(reinterpret_cast<uintptr_t>(at));
		const s64 disp = bytes / 4;
		if ((bytes & 3) || disp < -(1LL << 25) || disp >= (1LL << 25))
			return false;
		*insn = (*insn & 0xFC000000u) | (static_cast<u32>(disp) & 0x03FFFFFFu);
		return true;
	}

	void Worker::Start()
	{
		if (thread_.joinable())
			return;
		{
			std::lock_guard lk(mu_);
			stop_ = false;
		}
		thread_ = std::thread([this] { Loop(); });
	}

	void Worker::Stop()
	{
		if (!thread_.joinable())
			return;
		{
			std::lock_guard lk(mu_);
			stop_ = true;
			queue_.clear();
		}
		cv_.notify_all();
		thread_.join();
		std::lock_guard lk(mu_);
		finished_.clear();
		completed_.store(0, std::memory_order_relaxed);
		running_.reset();
	}

	bool Worker::Submit(std::shared_ptr<Job> job)
	{
		{
			std::lock_guard lk(mu_);
			if (stop_ || !thread_.joinable() || queue_.size() >= kMaxQueued)
				return false;
			queue_.push_back(std::move(job));
		}
		cv_.notify_one();
		return true;
	}

	void Worker::Wait(const std::shared_ptr<Job>& job)
	{
		std::unique_lock lk(mu_);
		doneCv_.wait(lk, [&] { return job->done.load(std::memory_order_acquire) || stop_; });
	}

	void Worker::RemoveQueued(const std::function<bool(const Job&)>& drop)
	{
		std::lock_guard lk(mu_);
		for (auto it = queue_.begin(); it != queue_.end();)
			it = drop(**it) ? queue_.erase(it) : std::next(it);
	}

	void Worker::TakeCompleted(std::vector<std::shared_ptr<Job>>& out)
	{
		std::lock_guard lk(mu_);
		for (auto& j : finished_)
			out.push_back(std::move(j));
		finished_.clear();
		completed_.store(0, std::memory_order_relaxed);
	}

	void Worker::Hold(bool on)
	{
		{
			std::lock_guard lk(mu_);
			hold_ = on;
		}
		cv_.notify_all();
	}

	void Worker::SetGate(std::function<void()> gate)
	{
		std::lock_guard lk(mu_);
		gate_ = std::move(gate);
	}

	void Worker::WaitIdle()
	{
		std::unique_lock lk(mu_);
		doneCv_.wait(lk, [&] { return stop_ || !thread_.joinable() || (queue_.empty() && !running_); });
	}

	size_t Worker::Queued()
	{
		std::lock_guard lk(mu_);
		return queue_.size();
	}

	void Worker::Loop()
	{
		Threading::SetNameOfCurrentThread("VU1 vu-aot compile");
		// Below the emulator's threads: a tier-up may wait, the VU must not.

#if defined(__linux__)
		setpriority(PRIO_PROCESS, 0, 10);
		// A thread inherits the affinity mask of its creator. If the tier is switched on in a running
		// game, the creator is the CPU thread, which is already pinned to one core or to the performance
		// cores, so the worker would compete with the emulation threads. Use the process's first thread's
		// mask instead: nothing pins it, and taskset/cpuset still limit it.
		cpu_set_t mask;
		if (sched_getaffinity(getpid(), sizeof(mask), &mask) == 0)
			sched_setaffinity(0, sizeof(mask), &mask);
#endif
		for (;;)
		{
			std::function<void()> gate;
			{
				std::unique_lock lk(mu_);
				cv_.wait(lk, [&] { return stop_ || (!hold_ && !queue_.empty()); });
				if (stop_)
					break;
				running_ = std::move(queue_.front());
				queue_.pop_front();
				gate = gate_;
			}
			if (gate)
				gate();
			running_->Run();
			{
				std::lock_guard lk(mu_);
				running_->done.store(true, std::memory_order_release);
				if (!stop_)
				{
					finished_.push_back(std::move(running_));
					completed_.store(static_cast<u32>(finished_.size()), std::memory_order_relaxed);
				}
				running_.reset();
			}
			doneCv_.notify_all();
		}
		doneCv_.notify_all();
	}
} // namespace vuaot::bg
