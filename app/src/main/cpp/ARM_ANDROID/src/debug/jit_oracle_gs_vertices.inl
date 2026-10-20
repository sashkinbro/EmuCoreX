// SPDX-License-Identifier: GPL-3.0+
namespace
{
struct GSVertexPacketOracle final : GSState
{
    GSPrivRegSet regs{};
    void Draw() override {}
    u32 CurrentZ() const { return m_v.XYZ.Z; }
    GSVertexPacketOracle() { SetRegsMem(reinterpret_cast<u8*>(&regs)); }

    void Prepare(u32 prim, bool native, bool offset = false)
    {
        // Isolate submission/culling from the renderer. No pending draw is dropped
        // in production: this reset belongs only to this standalone fixture.
        m_index.tail = 0;
        m_vertex.head = m_vertex.tail = m_vertex.next = m_vertex.xy_tail = 0;
        for (auto& xy : m_vertex.xy) xy = GSVector4i::zero();
        m_vertex.xyhead = GSVector4i::zero();
        m_v = {};
        m_v.UV = 0x24681357;
        m_v.FOG = 0x89abcdef;
        m_q = 1.0f;
        temp_draw_rect = GSVector4i::zero();
        m_nativeres = native;
        m_env.PRIM.U64 = prim;
        m_env.PRMODECONT.AC = 1;
        m_env.CTXT[0].XYOFFSET.U64 = 0;
        m_env.CTXT[0].XYOFFSET.OFX = offset ? 0x8000 : 0;
        m_env.CTXT[0].XYOFFSET.OFY = offset ? 0x8000 : 0;
        m_env.CTXT[0].ZBUF.PSM = 0x30;
        m_env.CTXT[0].SCISSOR.U64 = 0;
        m_env.CTXT[0].SCISSOR.SCAX1 = 1023;
        m_env.CTXT[0].SCISSOR.SCAY1 = 1023;
        m_env.CTXT[0].UpdateScissor();
        m_prev_env = m_env;
        m_dirty_gs_regs = 0;
        m_backed_up_ctx = 0;
        UpdateContext();
        UpdateScissor();
        UpdateVertexKick();
    }

    std::vector<u8> Snapshot() const
    {
        std::vector<u8> result;
        const auto append = [&](const void* data, size_t size) {
            const auto* bytes = static_cast<const u8*>(data);
            result.insert(result.end(), bytes, bytes + size);
        };
        append(&m_v, sizeof(m_v));
        append(&m_q, sizeof(m_q));
        append(&m_vertex.head, sizeof(u32) * 3);
        append(&m_vertex.xy_tail, sizeof(m_vertex.xy_tail));
        append(m_vertex.xy, sizeof(m_vertex.xy));
        append(&m_vertex.xyhead, sizeof(m_vertex.xyhead));
        append(&m_index.tail, sizeof(m_index.tail));
        append(m_vertex.buff, sizeof(GSVertex) * m_vertex.tail);
        append(m_index.buff, sizeof(u16) * m_index.tail);
        append(&temp_draw_rect, sizeof(temp_draw_rect));
        return result;
    }
};

std::vector<GIFPackedReg> GSVertexPacket(u32 vertices, bool fog, bool skips)
{
    std::vector<GIFPackedReg> packet(1 + vertices * 3);
    packet[0].U64[0] = vertices | (1ull << 15) | (3ull << 60);
    packet[0].U64[1] = fog ? 0x412 : 0x512; // STQ, RGBA, XYZF2/XYZ2.
    u32 random = 0xd4398a75;
    const auto next = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    for (u32 i = 0; i < vertices; ++i)
    {
        auto* r = packet.data() + 1 + i * 3;
        for (u32 j = 0; j < 3; ++j)
            for (u32& word : r[j].U32) word = next();
        r[0].U32[0] = 0x3f000000 | (next() & 0x7fffff);
        r[0].U32[1] = 0x3f000000 | (next() & 0x7fffff);
        r[0].U32[2] = (i % 7) ? 0x3f800000 : 0;
        r[2].U32[0] = (next() & 0xffff0000) | ((i * 379) % 16000);
        r[2].U32[1] = (next() & 0xffff0000) | ((i * 1031) % 16000);
        r[2].U32[3] = (r[2].U32[3] & ~0x8000u) | ((skips && i % 5 < 2) ? 0x8000 : 0);
    }
    return packet;
}
}

void GSVertexPacketTests()
{
    const auto saved = GSConfig;
    const ScopedGuard restore([&]() { GSConfig = saved; });
    GSConfig.UserHacks_AutoFlush = GSHWAutoFlushLevel::Disabled;
    GSConfig.AutoFlushSW = false;
    GSConfig.UserHacks_Limit24BitDepth = GSLimit24BitDepth::Disabled;
    auto state = std::make_unique<GSVertexPacketOracle>();
    for (auto mode : {GSLimit24BitDepth::Disabled, GSLimit24BitDepth::PrioritizeUpper, GSLimit24BitDepth::PrioritizeLower})
    {
        GSConfig.UserHacks_Limit24BitDepth = mode;
        auto packet = GSVertexPacket(1, false, false);
        packet[1].U32[2] = 0x3f800000;
        packet[3].U32[2] = 0x12345678;
        state->Prepare(GS_POINTLIST, true);
        state->Transfer<3>(reinterpret_cast<const u8*>(packet.data()), static_cast<u32>(packet.size()));
        const u32 expected = mode == GSLimit24BitDepth::PrioritizeUpper ? 0x00123478 :
            mode == GSLimit24BitDepth::PrioritizeLower ? 0x00345678 : 0x12345678;
        Check(state->CurrentZ() == expected, "GS vertex depth variant executes the requested known-Z transformation");
    }
    for (u32 prim = 0; prim < 7; ++prim)
        for (bool native : {false, true})
            for (bool fog : {false, true})
                for (bool skips : {false, true})
                for (u32 variant = 0; variant < 4; ++variant)
                {
                    GSConfig.UserHacks_Limit24BitDepth = variant == 2 ? GSLimit24BitDepth::PrioritizeUpper :
                        variant == 3 ? GSLimit24BitDepth::PrioritizeLower : GSLimit24BitDepth::Disabled;
                    bool exact = true;
                    for (u32 vertices : {1u, 2u, 3u, 4u, 17u, 257u, 10003u})
                    {
                        auto packet = GSVertexPacket(vertices, fog, skips);
                        // The fused handler intentionally retains the raw last Q in
                        // m_q, while the scalar STQ handler normalizes it. Use a
                        // finite last Q for this differential; zero Q is exercised
                        // in the intermediate vertices and in gs-stq-pack.
                        packet[packet.size() - 3].U32[2] = 0x3f800000;
                        if (variant == 1)
                        {
                            constexpr u16 edges[] = {0, 1, 15, 16, 32767, 32768, 65534, 65535};
                            for (u32 i = 0; i < vertices; ++i)
                            {
                                auto& xyz = packet[1 + i * 3 + 2];
                                xyz.U32[0] = (xyz.U32[0] & 0xffff0000) | edges[i % 8];
                                xyz.U32[1] = (xyz.U32[1] & 0xffff0000) | edges[(i / 8) % 8];
                            }
                        }
                        const auto* bytes = reinterpret_cast<const u8*>(packet.data());
                        state->Prepare(prim, native, variant == 1);
                        state->Transfer<3>(bytes, static_cast<u32>(packet.size()));
                        const auto expected = state->Snapshot();
                        state->Prepare(prim, native, variant == 1);
                        for (u32 qwc = 0; qwc < packet.size(); ++qwc)
                            state->Transfer<3>(bytes + qwc * 16, 1);
                        exact &= state->Snapshot() == expected;
                    }
                    char label[192];
                    std::snprintf(label, sizeof(label),
                        "GS vertices prim=%u native=%u fog=%u skips=%u variant=%u: bulk/one-QWC preserve state, edges, depth and buffer growth",
                        prim, native, fog, skips, variant);
                    Check(exact, label);
                }
    GSConfig.UserHacks_Limit24BitDepth = GSLimit24BitDepth::Disabled;
    for (u32 vertices : {4u, 16u, 256u, 4096u})
    {
        const auto packet = GSVertexPacket(vertices, true, true);
        const auto* bytes = reinterpret_cast<const u8*>(packet.data());
        constexpr u32 rounds = 7;
        double samples[rounds]{};
        const u32 repeats = (1u << 18) / vertices;
        for (u32 round = 0; round < rounds; ++round)
        {
            const auto start = std::chrono::steady_clock::now();
            for (u32 rep = 0; rep < repeats; ++rep)
            {
                state->Prepare(GS_TRIANGLESTRIP, true);
                state->Transfer<3>(bytes, static_cast<u32>(packet.size()));
            }
            samples[round] = std::chrono::duration<double, std::nano>(
                std::chrono::steady_clock::now() - start).count() / (vertices * repeats);
        }
        std::sort(std::begin(samples), std::end(samples));
        u64 hash = 14695981039346656037ull;
        for (u8 byte : state->Snapshot()) hash = (hash ^ byte) * 1099511628211ull;
        std::printf("GS_VERTICES vertices=%u ns=%.4f hash=%016llx\n", vertices, samples[rounds / 2],
            static_cast<unsigned long long>(hash));
    }
}
