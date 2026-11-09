// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Literal transfer fixtures. Included only by the native cache oracle.
static void RunGSLocalMemoryParity(GSRendererHW& renderer, void (*check)(bool, const char*))
{
    g_texture_cache->RemoveAll(true, true, true);
    renderer.m_draw_transfers.clear();
    const auto saved_environment = renderer.m_env;
    const auto saved_mode = GSConfig.HWDownloadMode;
    const ScopedGuard restore_memory_fixture([&]() {
        renderer.m_env = saved_environment;
        renderer.m_draw_transfers.clear();
        GSConfig.HWDownloadMode = saved_mode;
    });
    GSConfig.HWDownloadMode = GSHardwareDownloadMode::Enabled;
    auto& memory = renderer.m_mem;
    const auto start_upload = [&]() {
        // PACKED A+D writes TRXDIR through the actual GIF parser/handler,
        // which initializes the private upload cursor from the public registers.
        alignas(16) const u64 packet[4] = {1ull | (1ull << 15) | (1ull << 60), GIF_REG_A_D, 0, GIF_A_D_REG_TRXDIR};
        renderer.Transfer<3>(reinterpret_cast<const u8*>(packet), 2);
    };

    // Across a 128-pixel boundary, a one-pixel right shift observes the last
    // pixel written by the preceding chunk. This is a literal console result,
    // at every pixel depth: its boundary is pixels, never bytes.
    for (const u32 psm : {u32(PSMCT32), u32(PSMCT16), u32(PSMT8), u32(PSMT4)})
    {
        const auto& format = GSLocalMemory::m_psm[psm];
        const auto off = memory.GetOffset(0x1000, 8, psm);
        const auto row = off.paMulti(0, 0);
        const u32 mask = psm == PSMCT32 ? 0xffffffffu : psm == PSMCT16 ? 0xffffu : psm == PSMT8 ? 0xffu : 0xfu;
        for (u32 x = 0; x <= 257; ++x)
            (memory.*format.wpa)(row.value(x), x);
        renderer.m_env.BITBLTBUF = GIFRegBITBLTBUF{};
        renderer.m_env.BITBLTBUF.SBP = renderer.m_env.BITBLTBUF.DBP = 0x1000;
        renderer.m_env.BITBLTBUF.SBW = renderer.m_env.BITBLTBUF.DBW = 8;
        renderer.m_env.BITBLTBUF.SPSM = renderer.m_env.BITBLTBUF.DPSM = psm;
        renderer.m_env.TRXPOS = GIFRegTRXPOS{};
        renderer.m_env.TRXPOS.DSAX = 1;
        renderer.m_env.TRXREG = GIFRegTRXREG{};
        renderer.m_env.TRXREG.RRW = 256;
        renderer.m_env.TRXREG.RRH = 1;
        renderer.m_env.TRXDIR.XDIR = 2;
        const u64 before = memory.WriteSeq();
        renderer.GSState::Move();
        bool literal = true;
        for (u32 x = 1; x <= 256; ++x)
            literal &= (memory.*format.rpa)(row.value(x)) == ((x == 129 ? 127 : x - 1) & mask);
        literal &= (memory.*format.rpa)(row.value(257)) == (257 & mask);
        check(literal, psm == PSMCT32 ? "GS actual 32-bit transfer uses a 128-pixel overlap staging boundary" :
            psm == PSMCT16 ? "GS actual 16-bit transfer uses a 128-pixel overlap staging boundary" :
            psm == PSMT8 ? "GS actual 8-bit transfer uses a 128-pixel overlap staging boundary" :
                          "GS actual 4-bit transfer uses a 128-pixel overlap staging boundary");
        check(memory.WriteSeq() > before, "GS actual local move invalidates destination writer stamps");
    }
    {
        for (u32 x = 0; x < 260; ++x)
            memory.WritePixel32(x, 0, x, 0x1000, 8);
        renderer.m_env.BITBLTBUF.SPSM = renderer.m_env.BITBLTBUF.DPSM = PSMCT32;
        renderer.m_env.TRXPOS.SSAX = 2;
        renderer.m_env.TRXPOS.DSAX = 1;
        renderer.m_env.TRXPOS.DIRX = 1;
        renderer.m_env.TRXDIR.XDIR = 2;
        renderer.GSState::Move();
        check(memory.ReadPixel32(128, 0, 0x1000, 8) == 130 && memory.ReadPixel32(1, 0, 0x1000, 8) == 2,
            "GS actual reverse transfer honors DIRX literally across the 128-pixel boundary");
    }
    {
        renderer.m_env.BITBLTBUF = GIFRegBITBLTBUF{};
        auto& blit = renderer.m_env.BITBLTBUF;
        blit.SBP = 0x1000;
        blit.DBP = 0x2000;
        blit.SBW = blit.DBW = 1;
        blit.SPSM = PSMCT16;
        blit.DPSM = PSMCT32;
        renderer.m_env.TRXPOS = GIFRegTRXPOS{};
        renderer.m_env.TRXREG.RRW = renderer.m_env.TRXREG.RRH = 1;
        renderer.m_env.TRXDIR.XDIR = 2;
        memory.WritePixel16(0, 0, 0x1234, blit.SBP, 1);
        memory.WritePixel32(0, 0, 0xa5a5dead, blit.DBP, 1);
        renderer.GSState::Move();
        check(memory.ReadPixel32(0, 0, blit.DBP, 1) == 0xa5a51234,
            "GS actual narrower-to-wider transfer preserves the destination high bits");
    }

    // Each piece is byte-aligned, but three-pixel rows are not. The first
    // two bytes end at row 2 pixel 1; the third piece's spare nibble is dropped.
    for (const u32 psm : {u32(PSMT4), u32(PSMT4HL), u32(PSMT4HH)})
    {
        renderer.m_env.BITBLTBUF = GIFRegBITBLTBUF{};
        auto& blit = renderer.m_env.BITBLTBUF;
        blit.DBP = 0x2800;
        blit.DBW = 2;
        blit.DPSM = psm;
        renderer.m_env.TRXPOS = GIFRegTRXPOS{};
        renderer.m_env.TRXPOS.DSAX = renderer.m_env.TRXPOS.DSAY = 1;
        renderer.m_env.TRXREG.RRW = renderer.m_env.TRXREG.RRH = 3;
        renderer.m_env.TRXDIR.XDIR = 0;
        const auto& format = GSLocalMemory::m_psm[psm];
        const auto off = memory.GetOffset(blit.DBP, blit.DBW, psm);
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 6; ++x)
                if (psm == PSMT4)
                    memory.WritePixel4(x, y, 10, blit.DBP, blit.DBW);
                else
                    memory.WritePixel32(x, y, 0xaa123456, blit.DBP, blit.DBW);
        start_upload();
        const u8 first[16] = {0x21, 0x43};
        const u8 second[16] = {0x65, 0x87};
        const u8 last[16] = {0xa9};
        renderer.Write(first, 2);
        renderer.FlushWrite();
        check((memory.*format.rpa)(off.paMulti(0, 1).value(1)) == 1 &&
            (memory.*format.rpa)(off.paMulti(0, 1).value(3)) == 3 &&
            (memory.*format.rpa)(off.paMulti(0, 2).value(1)) == 4 &&
            (memory.*format.rpa)(off.paMulti(0, 2).value(2)) == 10,
            "GS actual partial nibble upload retains its cursor across an odd-width row");
        renderer.Write(second, 2);
        renderer.FlushWrite();
        renderer.Write(last, 1);
        bool literal = true;
        for (int y = 1; y <= 3; ++y)
            for (int x = 1; x <= 3; ++x)
            {
                literal &= (memory.*format.rpa)(off.paMulti(0, y).value(x)) == static_cast<u32>((y - 1) * 3 + x);
                if (psm != PSMT4)
                {
                    const u32 c = memory.ReadPixel32(x, y, blit.DBP, blit.DBW);
                    literal &= (c & 0xffffff) == 0x123456;
                    literal &= psm == PSMT4HL ? (c >> 28) == 10 : ((c >> 24) & 15) == 10;
                }
            }
        literal &= (memory.*format.rpa)(off.paMulti(0, 3).value(4)) == 10;
        literal &= renderer.m_env.TRXDIR.XDIR == 3;
        check(literal, psm == PSMT4 ? "GS actual sliced PSMT4 transfer writes literal row-spanning nibbles only" :
            psm == PSMT4HL ? "GS actual sliced PSMT4HL transfer preserves RGB and the held high nibble" :
                            "GS actual sliced PSMT4HH transfer preserves RGB and the held low nibble");
    }
    {
        renderer.m_env.BITBLTBUF = GIFRegBITBLTBUF{};
        auto& blit = renderer.m_env.BITBLTBUF;
        blit.DBP = 0x2800;
        blit.DBW = 2;
        blit.DPSM = PSMT4;
        renderer.m_env.TRXPOS = GIFRegTRXPOS{};
        renderer.m_env.TRXPOS.DSAX = 1;
        renderer.m_env.TRXREG.RRW = 4;
        renderer.m_env.TRXREG.RRH = 2;
        renderer.m_env.TRXDIR.XDIR = 0;
        start_upload();
        const u8 guarded[] = {0xee, 0x21, 0x43, 0x65, 0x87, 0xee};
        renderer.Write(guarded + 1, 4);
        bool literal = true;
        for (int y = 0; y < 2; ++y)
            for (int x = 1; x <= 4; ++x)
                literal &= memory.ReadPixel4(x, y, blit.DBP, blit.DBW) == static_cast<u32>(y * 4 + x);
        check(literal, "GS actual even-width nibble upload at odd X reads its own first nibble");
    }

    {
        renderer.m_env.BITBLTBUF = GIFRegBITBLTBUF{};
        auto& blit = renderer.m_env.BITBLTBUF;
        blit.DBP = 0x2800;
        blit.DBW = 1;
        blit.DPSM = PSMCT24;
        renderer.m_env.TRXPOS = GIFRegTRXPOS{};
        renderer.m_env.TRXREG.RRW = renderer.m_env.TRXREG.RRH = 1;
        renderer.m_env.TRXDIR.XDIR = 0;
        start_upload();
        memory.WritePixel32(0, 0, 0xa5000000, blit.DBP, 1);
        const u8 bytes[3] = {0x12, 0x34, 0x56};
        renderer.Write(bytes, 3);
        check(memory.ReadPixel32(0, 0, blit.DBP, 1) == 0xa5563412 && renderer.m_env.TRXDIR.XDIR == 3,
            "GS actual three-byte CT24 upload preserves the held alpha byte and exact cursor");
    }

    // Compare physical block membership with the overlap envelope, allowing
    // conservative extra overlap but never a missed real wrapped intersection.
    struct Window { u32 bp; GSVector4i rect; std::array<u64, GS_MAX_BLOCKS / 64> blocks{}; };
    std::vector<Window> windows;
    for (u32 bp : {0u, 32u, GS_MAX_BLOCKS - 64u, GS_MAX_BLOCKS - 32u})
        for (int top : {0, 32, 64})
            for (int height : {1, 32, 64, 96})
            {
                Window window{bp, GSVector4i(0, top, 64, top + height)};
                memory.GetOffset(bp, 1, PSMCT32).loopBlocks(window.rect, [&](u32 block) {
                    window.blocks[block >> 6] |= u64(1) << (block & 63);
                });
                windows.push_back(window);
            }
    bool no_missed_overlap = true;
    for (const Window& a : windows)
        for (const Window& b : windows)
        {
            bool physically_overlaps = false;
            for (size_t word = 0; word < a.blocks.size(); ++word)
                physically_overlaps |= (a.blocks[word] & b.blocks[word]) != 0;
            no_missed_overlap &= !physically_overlaps || GSLocalMemory::HasOverlap(a.bp, 1, PSMCT32, a.rect, b.bp, 1, PSMCT32, b.rect);
        }
    check(no_missed_overlap, "GS actual overlap envelope never misses a real physical wrapped-page intersection");
    check(!GSLocalMemory::HasOverlap(0, 1, PSMCT32, GSVector4i(0, 0, 64, 32), 32, 1, PSMCT32, GSVector4i(0, 0, 64, 32)),
        "GS actual overlap envelope keeps adjacent pages disjoint");
}
