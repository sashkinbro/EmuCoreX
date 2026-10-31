// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Exercises the real path ring and XGKICK helper, without a VM/GS consumer.
namespace
{
struct GifCopyProbe
{
    u32 copies = 0, zero_copies = 0, waits = 0;
    std::vector<u32> sizes;
    std::vector<u8> consumed;
    std::vector<u64> ad;
    bool queue_ok = true;
    bool threaded = false;
    static inline GifCopyProbe* current = nullptr;

    static void Copy(Gif_Path&, const u8*, u32 bytes)
    {
        ++current->copies;
        current->zero_copies += bytes == 0;
        current->sizes.push_back(bytes);
    }
    static void AD(const u8* bytes)
    {
        u64 value;
        std::memcpy(&value, bytes, 8);
        current->ad.push_back(value);
        current->ad.push_back(bytes[8] & 0x7f);
    }
    void Consume(Gif_Path& path)
    {
        if (!path.GetPendingGSPackets()) { queue_ok = false; return; }
        const GS_Packet packet = path.GetGSPacketMTVU();
        if (static_cast<u64>(packet.offset) + packet.size > path.buffSize || (packet.size & 15))
            queue_ok = false;
        else
            consumed.insert(consumed.end(), path.buffer + packet.offset, path.buffer + packet.offset + packet.size);
        const s32 amount = static_cast<s32>(packet.size) + packet.readAmount;
        if (path.readAmount.fetch_sub(amount, std::memory_order_release) < amount) queue_ok = false;
        path.PopGSPacketMTVU(); // Match MTGS: free queue entry last.
    }
    static void Wait(Gif_Path& path)
    {
        ++current->waits;
        if (current->threaded) std::this_thread::yield();
        else current->Consume(path);
    }
};

std::vector<u8> GifCopyImage(u32 bytes, u32 seed = 1)
{
    std::vector<u8> result(bytes);
    for (u32 i = 0; i < bytes; ++i) result[i] = static_cast<u8>((i * 37) ^ (seed * 11));
    if (bytes >= 16)
    {
        const u64 tag[2] = {static_cast<u64>(bytes / 16 - 1) | (1ull << 15) | (2ull << 58), 0};
        std::memcpy(result.data(), tag, 16);
    }
    return result;
}

void GifCopyPutVU(const std::vector<u8>& packet, u32 address)
{
    std::memset(VU1.Mem, 0xcd, 0x4000);
    for (u32 i = 0; i < packet.size(); ++i) VU1.Mem[(address + i) & 0x3fff] = packet[i];
}

void GifCopyAppendLegacy(u32 address)
{
    address = (address & 0x3ff) * 16;
    u32 diff = 0x4000 - address;
    u32 bytes = gifUnit.GetGSPacketSize(GIF_PATH_1, VU1.Mem, address, ~0u, true);
    if (!bytes) return;
    const u32 first = std::min<u32>(bytes, 16);
    if (first < bytes) gifUnit.gifPath[0].CopyGSPacketData(VU1.Mem + address, first, true);
    else gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, VU1.Mem + address, first, true);
    address = (address + first) & 0x3fff;
    diff -= first;
    bytes -= first;
    if (!bytes) return;
    if (bytes > diff)
    {
        gifUnit.gifPath[0].CopyGSPacketData(VU1.Mem + address, diff, true);
        gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, VU1.Mem, bytes - diff, true);
    }
    else gifUnit.TransferGSPacketData(GIF_TRANS_XGKICK, VU1.Mem + address, bytes, true);
}
}

void GifCopyTests()
{
    Check(!MTGS::IsOpen(), "GIF copy fixture runs without an active GS consumer");
    if (MTGS::IsOpen()) return;
    const auto saved = EmuConfig;
    EmuConfig.Cpu.Recompiler.EnableVU1 = true;
    EmuConfig.Speedhacks.vuThread = true;
    EmuConfig.Gamefixes.XgKickHack = false;
    const bool allocated = SysMemory::Allocate();
    Check(allocated && VU1.Mem, "GIF copy fixture allocates actual VU memory");
    if (!allocated || !VU1.Mem) { EmuConfig = saved; return; }
    auto& path = gifUnit.gifPath[0];
    const u32 saved_size = path.buffSize, saved_limit = path.buffLimit;
    GifCopyProbe probe;
    GifCopyTestHooks hooks{GifCopyProbe::Copy, GifCopyProbe::Wait, GifCopyProbe::AD};
    auto* saved_hooks = g_gif_copy_test_hooks;
    const auto signal = vu1Thread.gsSignal.load(), label = vu1Thread.gsLabel.load();
    const auto interrupts = vu1Thread.mtvuInterrupts.load();
    const ScopedGuard restore([&]() {
        path.Reset(); path.buffSize = saved_size; path.buffLimit = saved_limit;
        vu1Thread.gsSignal.store(signal); vu1Thread.gsLabel.store(label); vu1Thread.mtvuInterrupts.store(interrupts);
        g_gif_copy_test_hooks = saved_hooks; GifCopyProbe::current = nullptr;
        EmuConfig = saved;
    });
    g_gif_copy_test_hooks = &hooks;
    GifCopyProbe::current = &probe;
    const auto reset = [&]() {
        path.Reset(); probe = {};
        vu1Thread.gsSignal.store(0); vu1Thread.gsLabel.store(0); vu1Thread.mtvuInterrupts.store(0);
    };
    const auto consume_all = [&]() { while (path.GetPendingGSPackets()) probe.Consume(path); };
    bool exact = true, queue = true, minimal = true, legacy_count = true, no_zero = true;
    // All source QWC addresses, with exact-end, wrapped and bounded-long packets.
    for (u32 address = 0; address < 0x4000; address += 16)
        for (u32 bytes : {16u, 32u, 48u, 256u, 4096u, 16368u})
        {
            const auto expected = GifCopyImage(bytes, address + bytes);
            for (bool legacy : {true, false})
            {
                reset();
                GifCopyPutVU(expected, address);
                if (legacy) GifCopyAppendLegacy(address / 16);
                else mVU_XGKICK_(address / 16);
                path.FinishGSPacketMTVU();
                // Source mutation precedes the delayed consumer.
                std::memset(VU1.Mem, 0x5a, 0x4000);
                queue &= path.GetPendingGSPackets() == 1 && path.readAmount.load() == static_cast<int>(bytes);
                consume_all();
                exact &= probe.consumed == expected;
                queue &= probe.queue_ok && path.readAmount.load() == 0 && !path.GetPendingGSPackets();
                const u32 expected_calls = address + bytes > 0x4000 ? 2 : 1;
                if (!legacy)
                {
                    minimal &= probe.copies == expected_calls;
                    no_zero &= probe.zero_copies == 0;
                }
                else legacy_count &= probe.copies >= expected_calls && (bytes == 16 || probe.copies >= 2);
            }
        }
    Check(exact, "GIF real XGKICK and legacy split preserve every source-wrap byte after source overwrite");
    Check(queue, "GIF real XGKICK queue and readAmount retain snapshot ownership until consumer completion");
    Check(legacy_count, "GIF legacy control observes the artificial first-qword append");
    Check(minimal, "GIF real XGKICK appends once per contiguous source span with no artificial first-qword split");
    Check(no_zero, "GIF real XGKICK never makes a zero-byte append at the VU wrap");

    reset();
    const auto too_large = GifCopyImage(0x4000);
    GifCopyPutVU(too_large, 0);
    mVU_XGKICK_(0);
    Check(probe.copies == 0 && path.curSize == 0 && path.GetPendingGSPackets() == 0,
        "GIF existing full-VU packet bound rejects a 16KiB packet without copying");
    // Raw address bits are qword addresses, masked by the real helper.
    reset();
    const auto masked = GifCopyImage(48);
    GifCopyPutVU(masked, 16);
    mVU_XGKICK_(0xfffffc01u);
    path.FinishGSPacketMTVU(); consume_all();
    Check(probe.consumed == masked, "GIF XGKICK masks high address bits and preserves nonzero QWC address");

    bool byte_boundaries = true;
    for (u32 bytes : {0u, 1u, 15u, 16u, 17u, 31u, 32u, 63u})
    {
        reset();
        std::vector<u8> source(bytes + 1);
        for (u32 i = 0; i < source.size(); ++i) source[i] = static_cast<u8>(i * 17 + bytes);
        std::memset(path.buffer, 0xa5, bytes + 32);
        path.CopyGSPacketData(source.data() + 1, bytes, false);
        byte_boundaries &= path.curSize == bytes && path.curOffset == 0 && path.readAmount.load() == 0 &&
            std::memcmp(path.buffer, source.data() + 1, bytes) == 0;
        for (u32 i = bytes; i < bytes + 32; ++i) byte_boundaries &= path.buffer[i] == 0xa5;
    }
    Check(byte_boundaries, "GIF generic copy preserves zero/non-QWC byte sizes and destination canaries without parsing them");

    bool unaligned = true;
    for (u32 offset : {1u, 3u, 7u, 15u})
    {
        reset();
        const auto expected = GifCopyImage(128, offset);
        std::vector<u8> source(expected.size() + 16);
        std::memcpy(source.data() + offset, expected.data(), expected.size());
        path.CopyGSPacketData(source.data() + offset, static_cast<u32>(expected.size()), false);
        path.ExecuteGSPacketMTVU(); path.FinishGSPacketMTVU();
        std::fill(source.begin(), source.end(), 0);
        consume_all();
        unaligned &= probe.consumed == expected && probe.queue_ok && path.readAmount.load() == 0;
    }
    Check(unaligned, "GIF memcpy retains unaligned source bytes and owned lifetime without changing alignment policy");

    // A+D order and repeated masked LABEL composition must be unchanged by coalescing.
    std::vector<u8> ad_packet(16 * 6);
    const u64 ad_tag[2] = {5ull | (1ull << 15) | (1ull << 60), GIF_REG_A_D};
    std::memcpy(ad_packet.data(), ad_tag, 16);
    const u32 regs[] = {GIF_A_D_REG_LABEL, GIF_A_D_REG_SIGNAL, GIF_A_D_REG_LABEL, GIF_A_D_REG_FINISH, GIF_A_D_REG_LABEL};
    for (u32 i = 0; i < 5; ++i)
    {
        const u32 words[4] = {0x12345670u + i, (i & 1) ? 0xffff0000u : 0x0000ffffu, regs[i], 0};
        std::memcpy(ad_packet.data() + (i + 1) * 16, words, 16);
    }
    std::vector<u64> reference_ad, expected_ad;
    for (u32 i = 0; i < 5; ++i)
    {
        u64 value; std::memcpy(&value, ad_packet.data() + (i + 1) * 16, 8);
        expected_ad.push_back(value); expected_ad.push_back(regs[i]);
    }
    u64 reference_signal = 0, reference_label = 0; u32 reference_interrupts = 0;
    bool ad_exact = true;
    for (bool legacy : {true, false})
    {
        reset(); GifCopyPutVU(ad_packet, 0x3fd0);
        if (legacy) GifCopyAppendLegacy(0x3fd);
        else mVU_XGKICK_(0x3fd);
        path.FinishGSPacketMTVU(); consume_all();
        ad_exact &= probe.consumed == ad_packet && probe.ad == expected_ad && probe.queue_ok;
        if (legacy)
        {
            reference_ad = probe.ad;
            reference_signal = vu1Thread.gsSignal.load(); reference_label = vu1Thread.gsLabel.load();
            reference_interrupts = vu1Thread.mtvuInterrupts.load();
        }
        else ad_exact &= probe.ad == reference_ad && vu1Thread.gsSignal.load() == reference_signal &&
            vu1Thread.gsLabel.load() == reference_label && vu1Thread.mtvuInterrupts.load() == reference_interrupts;
    }
    Check(ad_exact && reference_interrupts == (VU_Thread::InterruptFlagSignal | VU_Thread::InterruptFlagFinish | VU_Thread::InterruptFlagLabel),
        "GIF wrapped A+D stream preserves SIGNAL/FINISH/LABEL order, values and coalesced masks");

    // Realignment/free-space pressure with real queued owners and a delayed consumer.
    bool pressure = true; u32 observed_waits = 0;
    for (bool legacy : {true, false})
    {
        reset(); path.buffSize = 1024; path.buffLimit = 768;
        std::vector<u8> expected;
        for (u32 i = 0; i < 48; ++i)
        {
            const auto packet = GifCopyImage((i % 3 + 1) * 128, i);
            expected.insert(expected.end(), packet.begin(), packet.end());
            GifCopyPutVU(packet, (i & 1) ? 0x3ff0 : 0x100);
            if (legacy) GifCopyAppendLegacy((i & 1) ? 0x3ff : 0x10);
            else mVU_XGKICK_((i & 1) ? 0x3ff : 0x10);
            path.FinishGSPacketMTVU();
            std::memset(VU1.Mem, 0x7c, 0x4000);
        }
        consume_all(); observed_waits += probe.waits;
        pressure &= probe.queue_ok && probe.consumed == expected && path.readAmount.load() == 0;
    }
    Check(pressure && observed_waits > 0, "GIF ring pressure drains real queued owners before reuse and preserves repeated wraps");
    path.buffSize = saved_size; path.buffLimit = saved_limit;

    // Multiple tags are parsed only after the final append, including zero-loop tags.
    reset();
    auto multiple = GifCopyImage(64);
    u64 empty_tag[2] = {0, 0};
    std::vector<u8> stream(16); std::memcpy(stream.data(), empty_tag, 16);
    stream.insert(stream.end(), multiple.begin(), multiple.end());
    GifCopyPutVU(stream, 0x3ff0); mVU_XGKICK_(0x3ff);
    path.FinishGSPacketMTVU(); consume_all();
    Check(probe.consumed == stream && probe.queue_ok && path.curOffset == path.curSize,
        "GIF wrapped multiple-tag packet retains zero-loop padding and final parse boundary");
}
