// SPDX-License-Identifier: GPL-3.0+
// Real ARM64 block-cache linking, invalidation and scaling checks.

class OracleBlockCache final : public BaseBlocks
{
public:
    size_t LinkCount() const { return links.size(); }
};

void BlockCacheAdd(OracleBlockCache& cache, u32 guest, u8* host, u32 bytes)
{
    BASEBLOCKEX* block = cache.New(guest, reinterpret_cast<uptr>(host));
    block->size = 16;
    block->x86size = bytes;
}

uptr BlockCacheBranchTarget(const u8* patch)
{
    u32 instruction;
    std::memcpy(&instruction, patch, 4);
    const s64 displacement = static_cast<s64>(static_cast<s32>((instruction & 0x03ffffffu) << 6)) >> 4;
    return reinterpret_cast<uptr>(patch) + displacement;
}

void BlockCacheLinkTests()
{
    std::vector<u32> arena(0x200000 / sizeof(u32));
    u8* const base = reinterpret_cast<u8*>(arena.data());
    u8* const removed = base + 0x100000;
    u8* const fallback = base + 0x180000;
    u8* const replacement = fallback + 32;
    u8* const foreign = fallback + 64;
    OracleBlockCache cache;
    cache.SetJITCompile(fallback);
    BlockCacheAdd(cache, 0x1000, base, 64);
    BlockCacheAdd(cache, 0x2000, removed, 64);
    cache.Link(0x2000, reinterpret_cast<s32*>(base));
    cache.Link(0x9000, reinterpret_cast<s32*>(base + 4));
    cache.Link(0x9000, reinterpret_cast<s32*>(removed));
    cache.Link(0x2000, reinterpret_cast<s32*>(removed + 4));
    cache.Link(0x9000, reinterpret_cast<s32*>(removed + 64));
    Check(cache.LinkCount() == 5, "block cache records incoming, outgoing, self and boundary links");
    cache.Remove(1, 1);
    Check(cache.LinkCount() == 3 && cache.GetBlockCount() == 1,
        "block invalidation removes only outgoing links in its half-open host range");
    Check(BlockCacheBranchTarget(base) == reinterpret_cast<uptr>(fallback),
        "block invalidation redirects live incoming links to the compiler");
    const u32 poison[] = {0xdeadbeefu, 0xcafebabeu};
    std::memcpy(removed, poison, sizeof(poison));
    BlockCacheAdd(cache, 0x2000, replacement, 8);
    BlockCacheAdd(cache, 0x9000, foreign, 8);
    Check(BlockCacheBranchTarget(base) == reinterpret_cast<uptr>(replacement) &&
        BlockCacheBranchTarget(base + 4) == reinterpret_cast<uptr>(foreign) &&
        BlockCacheBranchTarget(removed + 64) == reinterpret_cast<uptr>(foreign),
        "block recompilation relinks surviving incoming, foreign and boundary patches");
    Check(std::memcmp(removed, poison, sizeof(poison)) == 0,
        "block recompilation never patches retired source code or self links");
    cache.Reset();
    Check(cache.LinkCount() == 0 && cache.GetBlockCount() == 0, "block cache reset clears all link ownership");

    // Reuse host addresses after reset, including two records for one patch.
    BlockCacheAdd(cache, 0x1000, base, 64);
    BlockCacheAdd(cache, 0x2000, removed, 64);
    cache.Link(0x9000, reinterpret_cast<s32*>(base));
    cache.Link(0xa000, reinterpret_cast<s32*>(base));
    cache.Link(0x9000, reinterpret_cast<s32*>(removed));
    cache.Remove(0, 1);
    Check(cache.LinkCount() == 0 && cache.GetBlockCount() == 0,
        "multi-block invalidation handles reused addresses and duplicate patch records");

    // Guest-PC ordering need not match the order of generated host blocks.
    BlockCacheAdd(cache, 0x1000, base, 64);
    BlockCacheAdd(cache, 0x2000, removed, 64);
    BlockCacheAdd(cache, 0x3000, base + 0x80000, 64);
    for (u8* patch : {base, removed, base + 0x80000, removed + 64})
        cache.Link(0x9000, reinterpret_cast<s32*>(patch));
    cache.Remove(1, 2);
    const bool retained = cache.LinkCount() == 2 && cache.GetBlockCount() == 1;
    std::memcpy(removed, poison, sizeof(poison));
    std::memcpy(base + 0x80000, poison, sizeof(poison));
    BlockCacheAdd(cache, 0x9000, foreign, 8);
    Check(retained && std::memcmp(removed, poison, sizeof(poison)) == 0 &&
        std::memcmp(base + 0x80000, poison, sizeof(poison)) == 0 &&
        BlockCacheBranchTarget(base) == reinterpret_cast<uptr>(foreign) &&
        BlockCacheBranchTarget(removed + 64) == reinterpret_cast<uptr>(foreign),
        "multi-block invalidation handles noncontiguous host ranges in reversed address order");
}

double BlockCacheRemoveMedian(u32 unrelated_links)
{
    std::vector<u32> arena(0x200000 / sizeof(u32));
    u8* const base = reinterpret_cast<u8*>(arena.data());
    u8* const removed = base + 0x100000;
    OracleBlockCache cache;
    cache.SetJITCompile(base + 0x180000);
    BlockCacheAdd(cache, 0x1000, base, 0x100000);
    for (u32 i = 0; i < unrelated_links; ++i)
        cache.Link(0x9000, reinterpret_cast<s32*>(base + i * 4));
    std::array<double, 31> samples{};
    bool same = true;
    for (double& sample : samples)
    {
        BlockCacheAdd(cache, 0x2000, removed, 64);
        cache.Link(0x9000, reinterpret_cast<s32*>(removed));
        const auto start = std::chrono::steady_clock::now();
        cache.Remove(1, 1);
        sample = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count());
        same &= cache.LinkCount() == unrelated_links && cache.GetBlockCount() == 1;
    }
    Check(same, "block-cache removal benchmark preserves every unrelated link");
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

void BlockCacheTests()
{
    BlockCacheLinkTests();
    const double small = BlockCacheRemoveMedian(32);
    const double large = BlockCacheRemoveMedian(32768);
    std::printf("BLOCKCACHE_REMOVE unrelated32_median_ns=%.0f unrelated32768_median_ns=%.0f\n", small, large);
    // A generous ratio plus clock overhead tolerates CPU migration/frequency changes.
    // The invalidated block and outgoing work are identical in both populations.
    Check(large <= small * 16.0 + 2000.0,
        "one-block invalidation avoids a linear scan of unrelated JIT links");
}
