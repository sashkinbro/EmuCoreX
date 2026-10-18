// SPDX-License-Identifier: GPL-3.0+
static void OracleEmitBranch(u32 kind, const void* target, oak::Cond cond = oak::Cond::EQ)
{
    switch (kind)
    {
        case 0: oakEmitJmp(target); break;
        case 1: oakEmitCall(target); break;
        case 2: oakEmitCondBranch(cond, target); break;
        case 3: oakEmitCbz(oak::util::W0, target); break;
        case 4: oakEmitCbnz(oak::util::W0, target); break;
        case 5: oakEmitCbz(oak::util::X0, target); break;
        case 6: oakEmitCbnz(oak::util::X0, target); break;
    }
}

static bool OracleCondition(u32 cond, u32 flags)
{
    const bool n = flags & 8, z = flags & 4, c = flags & 2, v = flags & 1;
    const bool base[] = {z, c, n, v, c && !z, n == v, !z && n == v};
    return cond < 14 && (base[cond / 2] != bool(cond & 1));
}


static void OracleEmitReferenceBranch(u32 kind, const void* target)
{
    oak::Label skip;
    if (kind == 2) oakAsm->B(oak::Cond::NE, skip);
    else if (kind == 3) oakAsm->CBNZ(oak::util::W0, skip);
    else if (kind == 4) oakAsm->CBZ(oak::util::W0, skip);
    else if (kind == 5) oakAsm->CBNZ(oak::util::X0, skip);
    else if (kind == 6) oakAsm->CBZ(oak::util::X0, skip);
    oakMoveAddressToReg(OAK_XSCRATCH, target);
    if (kind == 1) oakAsm->BLR(OAK_XSCRATCH); else oakAsm->BR(OAK_XSCRATCH);
    if (kind >= 2) oakAsm->l(skip);
}

static __attribute__((noinline)) u64 OracleBranchReturn(u64 value)
{
    return (value ^ 0x123456789abcdef0ull) + 1;
}

static void Arm64BranchTimingTests(u8* memory)
{
    for (u32 kind = 0; kind < 7; ++kind)
    {
        const auto compile = [&](bool reference) {
            oakSetAsmPtr(memory + (reference ? 0x1000 : 0), 0x1000);
            u8* entry = oakStartBlock();
            if (kind == 1)
            {
                oakAsm->SUB(oak::util::SP, oak::util::SP, 16);
                oakAsm->STR(oak::util::X30, oak::util::SP);
            }
            oakAsm->MOV(oak::util::W3, oak::util::W0);
            oakAsm->MOV(oak::util::W0, kind == 4 || kind == 6 ? 1 : 0);
            oakAsm->CMP(oak::util::W0, oak::util::W0);
            oak::Label loop;
            oakAsm->l(loop);
            if (reference) OracleEmitReferenceBranch(kind, entry + 0x400);
            else OracleEmitBranch(kind, entry + 0x400);
            const auto end_loop = [&]() {
                oakAsm->SUB(oak::util::W3, oak::util::W3, 1);
                oakAsm->CBNZ(oak::util::W3, loop);
            };
            if (kind == 1)
            {
                end_loop();
                oakAsm->LDR(oak::util::X30, oak::util::SP);
                oakAsm->ADD(oak::util::SP, oak::util::SP, 16);
                oakAsm->RET();
            }
            while (oakGetCurrentCodePointer() < entry + 0x400) oakAsm->NOP();
            if (kind != 1) end_loop();
            oakAsm->RET();
            oakEndBlock();
            return reinterpret_cast<void (*)(u32)>(entry);
        };
        const auto current = compile(false), reference = compile(true);
        current(32); reference(32);
        std::array<double, 9> times{}, old_times{};
        const auto timed = [](auto kernel) {
            const auto start = std::chrono::steady_clock::now();
            kernel(1048576);
            return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / 1048576;
        };
        for (u32 run = 0; run < times.size(); ++run)
            if (run & 1) { old_times[run] = timed(reference); times[run] = timed(current); }
            else { times[run] = timed(current); old_times[run] = timed(reference); }
        std::sort(times.begin(), times.end()); std::sort(old_times.begin(), old_times.end());
        std::printf("ARM64BRANCH kind=%u current_median_ns=%.3f reference_median_ns=%.3f\n", kind, times[4], old_times[4]);
    }
    for (u32 kind = 0; kind < 7; ++kind)
    {
        oakSetAsmPtr(memory, 0x1000);
        u8* entry = oakStartBlock();
        if (kind == 1)
        {
            oakAsm->SUB(oak::util::SP, oak::util::SP, 16);
            oakAsm->STR(oak::util::X30, oak::util::SP);
        }
        oakAsm->MSR(oak::SystemReg::NZCV, oak::util::X1);
        OracleEmitBranch(kind, reinterpret_cast<const void*>(OracleBranchReturn));
        if (kind == 1)
        {
            oakAsm->LDR(oak::util::X30, oak::util::SP);
            oakAsm->ADD(oak::util::SP, oak::util::SP, 16);
            oakAsm->RET();
        }
        else if (kind >= 2)
        {
            oakAsm->MOV(oak::util::W0, 55);
            oakAsm->RET();
        }
        oakEndBlock();
        const auto kernel = reinterpret_cast<u64 (*)(u64, u64)>(entry);
        bool exact = true;
        for (u32 flags = 0; flags < 16; ++flags)
            for (u64 value : {0ull, 1ull, 0x100000000ull, 0xffffffffffffffffull})
            {
                const bool zero = kind < 5 ? static_cast<u32>(value) == 0 : value == 0;
                const bool taken = kind < 2 || (kind == 2 ? OracleCondition(0, flags) :
                    ((kind == 3 || kind == 5) ? zero : !zero));
                exact &= kernel(value, u64(flags) << 28) == (taken ? OracleBranchReturn(value) : 55);
            }
        Check(exact, "ARM64 helper branches/calls to a real native function and returns with exact predicates and ABI results");
    }
}

void Arm64BranchTests()
{
    if (!SysMemory::Allocate()) { Check(false, "allocate ARM64 branch test memory"); return; }
    u8* const memory = SysMemory::GetEERec() + 0x2000000;
    for (u32 kind = 0; kind < 7; ++kind)
    {
        const s64 limit = 1ll << (kind < 2 ? 27 : 20);
        for (s64 displacement : {-limit - 4, -limit, s64(-4), s64(0), s64(4), limit - 4, limit, s64(6)})
        {
            oakSetAsmPtr(memory, 0x1000);
            u8* entry = oakStartBlock();
            const void* target = reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(entry) + displacement);
            OracleEmitBranch(kind, target);
            const u32 bytes = oakGetCurrentCodePointer() - entry;
            oakEndBlock();
            const bool near = !(displacement & 3) && displacement >= -limit && displacement < limit;
            Check(near ? bytes == 4 : bytes > 4, "ARM64 helper selects direct branch only inside aligned signed immediate range");
            if (bytes == 4)
            {
                u32 instruction; std::memcpy(&instruction, entry, 4);
                const u32 bits = kind < 2 ? 26 : 19;
                const u32 immediate = kind < 2 ? instruction & 0x3ffffff : (instruction >> 5) & 0x7ffff;
                const s64 decoded = (static_cast<s64>(immediate ^ (1u << (bits - 1))) - (1ll << (bits - 1))) * 4;
                Check(decoded == displacement, "ARM64 direct branch encodes the exact signed target displacement");
                const u32 expected = kind == 0 ? 0x14000000u : kind == 1 ? 0x94000000u : kind == 2 ? 0x54000000u :
                    kind == 3 ? 0x34000000u : kind == 4 ? 0x35000000u : kind == 5 ? 0xb4000000u : 0xb5000000u;
                Check((instruction & (kind < 2 ? 0xfc000000u : 0xff00001fu)) == expected,
                    "ARM64 direct branch retains branch kind, condition and operand width");
            }
        }
        const u32 conditions = kind == 2 ? 16 : 1;
        for (u32 condition = 0; condition < conditions; ++condition)
        for (u32 distance : {0x400u, 0x200000u})
        {
            oakSetAsmPtr(memory, 0x1000);
            u8* entry = oakStartBlock();
            oakAsm->SUB(oak::util::SP, oak::util::SP, 16);
            oakAsm->STR(oak::util::X30, oak::util::SP);
            oakAsm->MSR(oak::SystemReg::NZCV, oak::util::X1);
            OracleEmitBranch(kind, memory + distance, static_cast<oak::Cond>(condition));
            if (kind != 1) oakAsm->MOV(oak::util::W0, 0);
            const auto finish = [&]() {
                oakAsm->MRS(oak::util::X3, oak::SystemReg::NZCV);
                oakAsm->STR(oak::util::X3, oak::util::X2);
                oakAsm->LDR(oak::util::X30, oak::util::SP);
                oakAsm->ADD(oak::util::SP, oak::util::SP, 16);
                oakAsm->RET();
            };
            finish();
            oakEndBlock();
            oakSetAsmPtr(memory + distance, 0x1000);
            oakStartBlock();
            oakAsm->MOV(oak::util::W0, 1);
            if (kind == 1) oakAsm->RET(); else finish();
            oakEndBlock();
            auto kernel = reinterpret_cast<u64 (*)(u64, u64, u64*)>(entry);
            bool exact = true;
            for (u32 flags = 0; flags < 16; ++flags)
                for (u64 value : {0ull, 1ull, 0x100000000ull, 0x80000000ull, 0xffffffffffffffffull})
                {
                    u64 after = 0;
                    const bool zero = kind < 5 ? static_cast<u32>(value) == 0 : value == 0;
                    const bool expected = kind < 2 || (kind == 2 ? OracleCondition(condition, flags) :
                        ((kind == 3 || kind == 5) ? zero : !zero));
                    exact &= kernel(value, u64(flags) << 28, &after) == expected && after == u64(flags) << 28;
                }
            Check(exact, "ARM64 branch executes taken/fallthrough paths and preserves NZCV, link return and W/X zero semantics");
        }
    }
    Arm64BranchTimingTests(memory);
}
