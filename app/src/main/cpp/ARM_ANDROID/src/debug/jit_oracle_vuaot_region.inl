// SPDX-License-Identifier: GPL-3.0+
void RunVuAotRegionTests() {
  cpuinfo_initialize();
  EmuConfig = Pcsx2Config();
  EmuConfig.Speedhacks.vuThread = false;
  EmuConfig.Speedhacks.vuFlagHack = false;
  if (!SysMemory::Allocate()) {
    Check(false, "allocate VU region test memory");
    return;
  }
  CpuVU0 = &CpuMicroVU0;
  CpuVU1 = &CpuMicroVU1;
  CpuMicroVU0.Reserve();
  CpuMicroVU1.Reserve();
  // Non-fallthrough branches create actual block boundaries; B+1 is elided by
  // microVU.
  const u32 add = (15u << 21) | (2u << 16) | (1u << 11) | (3u << 6) | 0x28u;
  const u32 mul = (15u << 21) | (2u << 16) | (3u << 11) | (4u << 6) | 0x2au;
  const std::vector<u32> program{(0x20u << 25) | 3u,
                                 add,
                                 NOP_L,
                                 NOP_U,
                                 NOP_L,
                                 NOP_U,
                                 NOP_L,
                                 NOP_U,
                                 (0x20u << 25) | 3u,
                                 mul,
                                 NOP_L,
                                 NOP_U,
                                 NOP_L,
                                 NOP_U,
                                 NOP_L,
                                 NOP_U,
                                 NOP_L,
                                 add | (1u << 30),
                                 NOP_L,
                                 NOP_U};
  mvu_aot::ForceSynchronous(true);
  for (u32 clamp = 0; clamp != 2; ++clamp) {
    EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
    EmuConfig.Cpu.Recompiler.EnableVUAot = false;
    const auto baseline = RunVU(1, true, program, 0x80);
    const u64 before = mvu_aot::RegionCount();
    const u64 entered = mvu_aot::EntryCount();
    EmuConfig.Cpu.Recompiler.EnableVUAot = true;
    const auto region = RunVU(1, true, program, 0x80);
    Check(mvu_aot::RegionCount() > before && mvu_aot::EntryCount() > entered,
          "VU1 executes an installed region rather than falling back");
    Compare(baseline, region,
            "VU1 region preserves baseline architectural and GIF state");
  }

  // Every destination remains observable: the E-bit tail is a NOP.
  const std::array<std::array<u32, 4>, 4> patterns{
      {{{0x3f801001, 0xbf803001, 0x40000001, 0x3eaaaaab}},
       {{0, 0x80000000, 1, 0x807fffff}},
       {{0x7f800000, 0xff800000, 0x7fc12345, 0xffa12345}},
       {{0x7f7fffff, 0xff7fffff, 0x00800000, 0x80800000}}}};
  for (u32 clamp = 0; clamp < 4; ++clamp)
    for (u32 opcode : {0x28u, 0x2cu, 0x2au, 0x29u, 0x2du, 0x2bu, 0x2fu,
        0x00u, 0x04u, 0x18u, 0x08u, 0x0cu, 0x03u, 0x07u, 0x1bu, 0x0bu, 0x0fu,
        0x22u, 0x26u, 0x1eu, 0x23u, 0x27u, 0x20u, 0x24u, 0x1cu, 0x21u, 0x25u,
        0x2bcu, 0x2fcu, 0x2beu, 0x2bdu, 0x2fdu, 0x3fu, 0x7fu, 0x1bfu, 0xbfu, 0xffu,
        0x23eu, 0x27eu, 0x1feu, 0x23fu, 0x27fu})
      for (u32 mask : {1u, 3u, 7u, 15u})
        for (u32 pattern = 0; pattern < patterns.size(); ++pattern) {
          EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
          EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = clamp >= 2;
          EmuConfig.Cpu.Recompiler.vu1SignOverflow = clamp >= 3;
          std::array<u32, 128> vf{};
          vf[3] = 0x3f800000;
          for (u32 r = 1; r < 32; ++r)
            for (u32 lane = 0; lane < 4; ++lane)
              vf[r * 4 + lane] = patterns[pattern][(r + lane) & 3];
          const u32 upper =
              (mask << 21) | (2u << 16) | (1u << 11) | ((opcode < 0x3c ? 3u : 0u) << 6) | opcode;
          const std::vector<u32> code{(0x20u << 25) | 3u,
                                      upper,
                                      NOP_L,
                                      NOP_U,
                                      NOP_L,
                                      NOP_U,
                                      NOP_L,
                                      NOP_U,
                                      (0x20u << 25) | 3u,
                                      NOP_U,
                                      NOP_L,
                                      NOP_U,
                                      NOP_L,
                                      NOP_U,
                                      NOP_L,
                                      NOP_U,
                                      NOP_L,
                                      NOP_U | (1u << 30),
                                      NOP_L,
                                      NOP_U};
          char name[120];
          std::snprintf(name, sizeof(name),
                        "region upper op=%02x mask=%x clamp=%u pattern=%u",
                        opcode, mask, clamp, pattern);
          EmuConfig.Cpu.Recompiler.EnableVUAot = false;
          const auto baseline =
              RunVU(1, true, code, 0x80, 0, 128, false, nullptr, vf.data(),
                    patterns[pattern].data());
          const u64 before = mvu_aot::RegionCount();
          const u64 entered = mvu_aot::EntryCount();
          EmuConfig.Cpu.Recompiler.EnableVUAot = true;
          const auto region = RunVU(1, true, code, 0x80, 0, 128, false, nullptr,
                                    vf.data(), patterns[pattern].data());
          Check(mvu_aot::RegionCount() > before &&
                    mvu_aot::EntryCount() > entered,
                "upper matrix installs a region");
          Compare(baseline, region, name);
          Check(baseline.regs.cycle == region.regs.cycle &&
                    baseline.regs.pending_p == region.regs.pending_p &&
                    baseline.regs.pending_q == region.regs.pending_q &&
                    !std::memcmp(baseline.regs.micro_statusflags,
                                 region.regs.micro_statusflags,
                                 sizeof(baseline.regs.micro_statusflags)) &&
                    !std::memcmp(baseline.regs.micro_macflags,
                                 region.regs.micro_macflags,
                                 sizeof(baseline.regs.micro_macflags)) &&
                    !std::memcmp(baseline.regs.micro_clipflags,
                                 region.regs.micro_clipflags,
                                 sizeof(baseline.regs.micro_clipflags)),
                "region upper preserves exact cycles, pending PQ and all flag "
                "ring slots");
        }

  // The E-bit block must execute its own region end sequence. Vary the
  // pipeline length and dispatcher budget so the end is not only a cold exit.
  for (u32 clamp = 0; clamp < 4; ++clamp)
    for (u32 length = 0; length < 8; ++length)
      for (u32 budget : {1u, 128u}) {
        EmuConfig.Cpu.Recompiler.vu1Overflow = clamp != 0;
        EmuConfig.Cpu.Recompiler.vu1ExtraOverflow = clamp >= 2;
        EmuConfig.Cpu.Recompiler.vu1SignOverflow = clamp >= 3;
        std::vector<u32> code{(0x20u << 25) | 3u, NOP_U, NOP_L, NOP_U,
                              NOP_L, NOP_U, NOP_L, NOP_U};
        for (u32 i = 0; i < length; ++i) {
          code.push_back(NOP_L);
          code.push_back(add);
        }
        code.insert(code.end(), {NOP_L, add | (1u << 30), NOP_L, mul});
        EmuConfig.Cpu.Recompiler.EnableVUAot = false;
        const auto baseline = RunVU(1, true, code, 0x80, 0, budget);
        const u64 ended = mvu_aot::NativeEndCount();
        const u64 entered = mvu_aot::EntryCount();
        EmuConfig.Cpu.Recompiler.EnableVUAot = true;
        const auto region = RunVU(1, true, code, 0x80, 0, budget);
        Check(budget == 128 ? mvu_aot::NativeEndCount() > ended :
                  (mvu_aot::EntryCount() > entered && mvu_aot::BailCount() > 0),
              "native E-bit executes, or a small budget enters its baseline bail");
        Compare(baseline, region, "native E-bit region architectural state");
        Check(baseline.regs.cycle == region.regs.cycle &&
                  baseline.regs.pending_q == region.regs.pending_q &&
                  baseline.regs.pending_p == region.regs.pending_p &&
                  !std::memcmp(baseline.regs.micro_statusflags, region.regs.micro_statusflags,
                               sizeof(baseline.regs.micro_statusflags)) &&
                  !std::memcmp(baseline.regs.micro_macflags, region.regs.micro_macflags,
                               sizeof(baseline.regs.micro_macflags)) &&
                  !std::memcmp(baseline.regs.micro_clipflags, region.regs.micro_clipflags,
                               sizeof(baseline.regs.micro_clipflags)),
              "native E-bit cycles, pending PQ and flag rings");
      }

  EmuConfig.Cpu.Recompiler.EnableVUAot = false;
  mvu_aot::ForceSynchronous(false);
}
