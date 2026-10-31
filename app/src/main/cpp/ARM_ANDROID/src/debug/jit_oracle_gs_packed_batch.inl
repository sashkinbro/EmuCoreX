// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Include after jit_oracle_gs_vertices.inl; both paths consume identical GIF streams.
extern "C" void EmuCoreXOracleSetGSPackedBatch(bool enabled, bool observe = true);
extern "C" void EmuCoreXOracleGSPackedBatchStats(u64* values);

namespace
{
struct GSPackedBatchOracle final : GSState
{
    GSPrivRegSet regs{};
    GSDrawingEnvironment initial;
    std::vector<u8> draws;
    u32 drawCount = 0;
    u32 vertexFlushes = 0;
    bool IsCoverageAlphaSupported() override { return true; }

    GSPackedBatchOracle()
    {
        SetRegsMem(reinterpret_cast<u8*>(&regs));
        initial = m_env;
    }

    template<class T> static void Append(std::vector<u8>& out, const T& value)
    {
        const auto* bytes = reinterpret_cast<const u8*>(&value);
        out.insert(out.end(), bytes, bytes + sizeof(value));
    }

    std::vector<u8> Snapshot() const
    {
        std::vector<u8> out;
        Append(out, m_v); Append(out, m_q);
        Append(out, m_vertex.head); Append(out, m_vertex.tail); Append(out, m_vertex.next);
        Append(out, m_vertex.xy_tail); Append(out, m_vertex.xy); Append(out, m_vertex.xyhead);
        Append(out, m_index.tail); Append(out, temp_draw_rect);
        Append(out, m_dirty_gs_regs); Append(out, m_backed_up_ctx);
        // Register bytes and scissor state are observable. Context offset structs
        // contain internal pointers and are not serialized as an oracle result.
        const auto* env = reinterpret_cast<const u8*>(&m_prev_env);
        out.insert(out.end(), env, env + 88);
        for (const auto& ctx : m_prev_env.CTXT)
        {
            const auto* bytes = reinterpret_cast<const u8*>(&ctx);
            out.insert(out.end(), bytes, bytes + 96);
            Append(out, ctx.scissor);
        }
        const auto* vertices = reinterpret_cast<const u8*>(m_vertex.buff);
        out.insert(out.end(), vertices, vertices + m_vertex.tail * sizeof(GSVertex));
        const auto* indices = reinterpret_cast<const u8*>(m_index.buff);
        out.insert(out.end(), indices, indices + m_index.tail * sizeof(u16));
        return out;
    }

    void Draw() override
    {
        const auto snapshot = Snapshot();
        Append(draws, m_state_flush_reason);
        Append(draws, snapshot.size());
        draws.insert(draws.end(), snapshot.begin(), snapshot.end());
        ++drawCount;
        vertexFlushes += m_state_flush_reason == GSFlushReason::VERTEXCOUNT;
    }

    void Prepare(u32 prim, bool native, bool offset, bool aa1, bool smallBuffer, bool invalidScissor = false)
    {
        draws.clear(); drawCount = vertexFlushes = 0;
        m_vertex.head = m_vertex.tail = m_vertex.next = m_vertex.xy_tail = 0;
        m_index.tail = 0;
        for (auto& xy : m_vertex.xy) xy = GSVector4i::zero();
        m_vertex.xyhead = GSVector4i::zero();
        // Only lower the logical capacity; the allocated buffer is always larger.
        // Both differential arms then execute the real GrowVertexBuffer seam.
        if (smallBuffer) m_vertex.maxcount = 16;
        m_v = {}; m_v.UV = 0x24681357; m_v.FOG = 0x89abcdef; m_q = 1.0f;
        temp_draw_rect = GSVector4i::zero();
        m_env = initial;
        m_env.PRIM.U64 = prim;
        m_env.PRIM.AA1 = aa1;
        m_env.PRMODECONT.AC = 1;
        for (auto& ctx : m_env.CTXT)
        {
            ctx.XYOFFSET.U64 = 0;
            ctx.XYOFFSET.OFX = offset ? 0x8000 : 0;
            ctx.XYOFFSET.OFY = offset ? 0x8000 : 0;
            ctx.ZBUF.PSM = 0x30;
            ctx.SCISSOR.U64 = 0;
            ctx.SCISSOR.SCAX1 = ctx.SCISSOR.SCAY1 = 1023;
            if (invalidScissor) { ctx.SCISSOR.SCAX0 = 600; ctx.SCISSOR.SCAX1 = 400; }
            ctx.UpdateScissor();
        }
        m_prev_env = m_env;
        m_dirty_gs_regs = 0; m_backed_up_ctx = 0;
        m_nativeres = native;
        ResetHandlers(); UpdateContext(); UpdateScissor(); UpdateVertexKick();
    }

    bool RawCarryMatches(const GIFPackedReg* last, bool fog) const
    {
        u32 rawQ;
        std::memcpy(&rawQ, &m_q, sizeof(rawQ));
        return rawQ == last[0].U32[2] && m_v.UV == 0x13572468 &&
            m_v.XYZ.X == last[2].XYZ2.X && m_v.XYZ.Y == last[2].XYZ2.Y &&
            (!fog || m_v.FOG == last[2].XYZF2.F);
    }

    void Submit(const std::vector<GIFPackedReg>& packet, u32 chunk)
    {
        for (u32 at = 0; at < packet.size();)
        {
            const u32 count = chunk ? std::min<u32>(chunk, packet.size() - at) : packet.size() - at;
            Transfer<3>(reinterpret_cast<const u8*>(packet.data() + at), count);
            at += count;
        }
    }
};

std::vector<GIFPackedReg> GSPackedBatchChanges(u32 prim)
{
    std::vector<GIFPackedReg> packet(6);
    packet[0].U64[0] = 5 | (1ull << 15) | (1ull << 60);
    packet[0].U64[1] = GIF_REG_A_D;
    packet[1].A_D.ADDR = GIF_A_D_REG_XYOFFSET_1;
    packet[1].r.XYOFFSET.OFX = 0x10;
    packet[1].r.XYOFFSET.OFY = 0x20;
    packet[2].A_D.ADDR = GIF_A_D_REG_SCISSOR_1;
    packet[2].r.SCISSOR.SCAX0 = 2;
    packet[2].r.SCISSOR.SCAX1 = 700;
    packet[2].r.SCISSOR.SCAY0 = 3;
    packet[2].r.SCISSOR.SCAY1 = 500;
    packet[3].A_D.ADDR = GIF_A_D_REG_PRIM;
    packet[3].r.PRIM.PRIM = prim;
    packet[3].r.PRIM.CTXT = 1;
    packet[4].A_D.ADDR = GIF_A_D_REG_UV;
    packet[4].r.UV.U32[0] = 0x13572468;
    packet[5].A_D.ADDR = GIF_A_D_REG_FOG;
    packet[5].r.FOG.F = 0x67;
    return packet;
}
}

void GSPackedBatchTests()
{
    const auto saved = GSConfig;
    const ScopedGuard restore([&]() { GSConfig = saved; EmuCoreXOracleSetGSPackedBatch(true, false); });
    GSConfig.UserHacks_AutoFlush = GSHWAutoFlushLevel::Disabled;
    GSConfig.AutoFlushSW = false;
    auto state = std::make_unique<GSPackedBatchOracle>();
    u64 direct = 0, batches = 0, publications = 0, vertices = 0;
    bool carry = true;
    for (u32 prim = 0; prim < 7; ++prim)
    for (bool fog : {false, true})
    for (bool native : {false, true})
    for (bool skips : {false, true})
    for (u32 mode = 0; mode < 6; ++mode)
    {
        GSConfig.UserHacks_Limit24BitDepth = mode == 2 ? GSLimit24BitDepth::PrioritizeUpper :
            mode == 3 ? GSLimit24BitDepth::PrioritizeLower : GSLimit24BitDepth::Disabled;
        bool exact = true;
        for (u32 chunk : {0u, 1u, 2u, 5u})
        {
            auto first = GSVertexPacket(73, fog, skips);
            auto last = GSVertexPacket(29, !fog, !skips);
            constexpr u32 q[] = {0, 0x80000000, 0x7fc12345, 0x7f800000, 0xff800000, 0x3f800000};
            first[first.size() - 3].U32[2] = q[(prim + mode) % std::size(q)];
            last[last.size() - 3].U32[2] = q[(prim + mode + 1) % std::size(q)];
            if (mode == 5)
            {
                constexpr u16 edges[] = {0, 1, 15, 16, 32767, 32768, 65534, 65535};
                for (auto* packet : {&first, &last})
                    for (u32 i = 0; i < (packet->size() - 1) / 3; ++i)
                    {
                        auto& xyz = (*packet)[1 + i * 3 + 2];
                        xyz.U32[0] = (xyz.U32[0] & 0xffff0000) | edges[i % 8];
                        xyz.U32[1] = (xyz.U32[1] & 0xffff0000) | edges[(i / 8) % 8];
                    }
            }
            const auto changes = GSPackedBatchChanges((prim + 3) % 7);
            std::vector<u8> expected, expectedDraws;
            for (bool candidate : {false, true})
            {
                EmuCoreXOracleSetGSPackedBatch(candidate);
                state->Prepare(prim, native, mode == 1, mode == 1, true, mode == 4);
                state->Submit(first, chunk);
                state->Submit(changes, chunk);
                state->Submit(last, chunk);
                const auto result = state->Snapshot();
                if (!candidate) { expected = result; expectedDraws = state->draws; }
                else
                {
                    exact &= result == expected && state->draws == expectedDraws;
                    if (chunk == 0)
                    {
                        carry &= state->RawCarryMatches(last.data() + last.size() - 3, !fog);
                        u64 stats[3]{}; EmuCoreXOracleGSPackedBatchStats(stats);
                        direct += stats[0]; batches += stats[1]; publications += stats[2]; vertices += 102;
                    }
                }
            }
        }
        char label[160];
        std::snprintf(label, sizeof(label), "GS direct batch prim=%u fog=%u native=%u ADC=%u mode=%u exact split/state/flush/carry", prim, fog, native, skips, mode);
        Check(exact, label);
    }
    Check(carry, "GS direct batches preserve known last raw Q, XYZ, UV and FOG homes");

    GSConfig.UserHacks_Limit24BitDepth = GSLimit24BitDepth::Disabled;
    for (u32 prim = 0; prim < 7; ++prim)
    for (bool fog : {false, true})
    {
        const auto first = GSVertexPacket(17, fog, true);
        const auto last = GSVertexPacket(11, !fog, false);
        std::vector<GIFPackedReg> reglist(2);
        reglist[0].U64[0] = 1 | (1ull << 15) | (1ull << 58) | (2ull << 60);
        reglist[0].U64[1] = GIF_REG_RGBA | (u64(GIF_REG_XYZ2) << 4);
        reglist[1].U64[0] = 0xabcdef01ull | (0x3f000000ull << 32);
        reglist[1].U64[1] = 200ull | (300ull << 16) | (0x12345678ull << 32);
        bool exact = true;
        for (u32 chunk : {0u, 1u})
        {
            std::vector<u8> expected, expectedDraws;
            for (bool candidate : {false, true})
            {
                EmuCoreXOracleSetGSPackedBatch(candidate);
                state->Prepare(prim, true, false, false, true);
                state->Submit(first, 0);
                state->Submit(reglist, chunk);
                state->Submit(last, 0);
                if (!candidate) { expected = state->Snapshot(); expectedDraws = state->draws; }
                else exact &= state->Snapshot() == expected && state->draws == expectedDraws;
            }
        }
        Check(exact, "GS fused batch publishes state across complete and resumed reglist packets");
    }

    for (auto level : {GSHWAutoFlushLevel::SpritesOnly, GSHWAutoFlushLevel::Enabled})
    for (u32 prim : {GS_TRIANGLESTRIP, GS_SPRITE})
    {
        GSConfig.UserHacks_AutoFlush = level;
        GSConfig.AutoFlushSW = true;
        auto packet = GSVertexPacket(73, true, false);
        std::vector<u8> expected, expectedDraws;
        bool exact = true;
        u64 stats[3]{};
        for (bool candidate : {false, true})
        {
            EmuCoreXOracleSetGSPackedBatch(candidate);
            state->Prepare(prim, true, false, false, true);
            state->Submit(packet, 0);
            if (!candidate) { expected = state->Snapshot(); expectedDraws = state->draws; }
            else
            {
                exact &= state->Snapshot() == expected && state->draws == expectedDraws;
                EmuCoreXOracleGSPackedBatchStats(stats);
            }
        }
        Check(exact, "GS packed auto-flush policy retains staged state and draw ordering");
        if (level == GSHWAutoFlushLevel::Enabled)
            Check(stats[0] == 0, "GS fully enabled auto-flush retains the staged kick path");
    }
    GSConfig.UserHacks_AutoFlush = GSHWAutoFlushLevel::Disabled;
    GSConfig.AutoFlushSW = false;

    // Bounded real flush cases: point list flushes at16379vertices; the fan
    // keeps its provoking head and the previous vertex across its65532 seam.
    GSConfig.UserHacks_Limit24BitDepth = GSLimit24BitDepth::Disabled;
    for (u32 prim : {GS_POINTLIST, GS_TRIANGLEFAN})
    {
        const u32 count = prim == GS_POINTLIST ? 17000 : 22000;
        auto packet = GSVertexPacket(count, true, false);
        constexpr u32 x[] = {100, 400, 100, 400};
        constexpr u32 y[] = {100, 100, 400, 400};
        for (u32 i = 0; i < count; ++i)
        {
            auto* r = packet.data() + 1 + i * 3;
            r[0].U32[2] = 0x3f800000;
            r[2].U32[0] = (r[2].U32[0] & 0xffff0000) | x[i % 4];
            r[2].U32[1] = (r[2].U32[1] & 0xffff0000) | y[i % 4];
        }
        std::vector<u8> expected, expectedDraws;
        bool exact = true, flushed = true;
        for (bool candidate : {false, true})
        {
            EmuCoreXOracleSetGSPackedBatch(candidate);
            state->Prepare(prim, true, false, false, true);
            const u32 tags = prim == GS_POINTLIST ? 1 : 3;
            for (u32 i = 0; i < tags; ++i) state->Submit(packet, 0);
            flushed &= state->vertexFlushes != 0;
            if (!candidate) { expected = state->Snapshot(); expectedDraws = state->draws; }
            else exact &= state->Snapshot() == expected && state->draws == expectedDraws;
        }
        Check(exact, "GS batch real vertex-count flush preserves draw-time state and strip/fan carry");
        Check(flushed, "GS batch fixture reaches an actual vertex-count Flush in both paths");
    }
    Check(direct != 0 && batches != 0, "GS fused packets enter the direct batch path");
    Check(publications < vertices, "GS direct batches avoid per-vertex current-state publication");
    std::printf("GS_PACKED_BATCH direct=%llu batches=%llu currentWrites=%llu vertices=%llu\n",
        static_cast<unsigned long long>(direct), static_cast<unsigned long long>(batches),
        static_cast<unsigned long long>(publications), static_cast<unsigned long long>(vertices));
}
