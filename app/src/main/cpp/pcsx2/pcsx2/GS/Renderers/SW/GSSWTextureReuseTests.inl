// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
namespace {
class SWCacheOracleRenderer final : public GSRenderer {
    void Draw() override {}
    GSTexture* GetOutput(int,float&,int&) override { return nullptr; }
};
template<class T> static size_t SWTextureCapacity(const T& t,size_t old_capacity) {
    if constexpr(requires { t.m_buff_size; }) return t.m_buff_size;
    return old_capacity;
}
static size_t SWTextureNominal(const GSTextureCacheSW::Texture& t) {
    const auto& psm=GSLocalMemory::m_psm[t.m_TEX0.PSM];
    const size_t height=std::max<size_t>(size_t(1)<<t.m_TEX0.TH,psm.bs.y);
    return (size_t(1)<<t.m_tw)*(psm.pal?1:4)*height*4;
}
}
void GSTextureCacheSW::TestPersistentTexture(void(*check)(bool,const char*))
{
    const bool isolated=!g_gs_renderer&&!g_gs_device;check(isolated,"GS SW texture reuse requires isolated renderer globals");if(!isolated)return;
    cpuinfo_initialize();const auto config=GSConfig;const u64 draw=GSState::s_n;GSConfig=Pcsx2Config::GSOptions();
    g_gs_renderer=std::make_unique<SWCacheOracleRenderer>();
    ScopedGuard restore=[&](){g_gs_renderer.reset();GSConfig=config;GSState::s_n=draw;};
    auto& memory=g_gs_renderer->m_mem;
    for(size_t i=0;i<memory.m_vmsize;i++)memory.m_vm8[i]=u8((i*37+(i>>8)*13)&255);
    GIFRegTEXA texa{};texa.TA0=0x20;texa.TA1=0x80;
    bool retained=true,pixels=true,slack=true,valid=true;
    for(u32 tbw:{1u,2u}) {
        GIFRegTEX0 tex0{};tex0.TBW=tbw;tex0.PSM=PSMCT32;tex0.TW=7;tex0.TH=7;
        Texture persistent(0,tex0,texa);check(persistent.Update({8,8,64,64}),"GS actual SW texture initial partial unswizzle succeeds");
        const void* allocation=persistent.m_buff;const size_t first_capacity=SWTextureNominal(persistent);
        for(const auto& shape:std::array<std::array<u32,3>,5>{{{6,6,PSMCT32},{6,5,PSMT8},{5,5,PSMT4},{6,6,PSMCT16},{7,7,PSMCT32}}}) {
            tex0.TW=shape[0];tex0.TH=shape[1];tex0.PSM=shape[2];
            persistent.Reset(0,tex0,texa);retained&=persistent.m_buff==allocation;
            const GSVector4i rect(0,0,16,16);const bool updated=persistent.Update(rect);
            Texture fresh(0,tex0,texa);const bool fresh_ok=fresh.Update(rect);
            const size_t nominal=SWTextureNominal(persistent),fresh_size=SWTextureNominal(fresh);
            const size_t actual=SWTextureCapacity(persistent,nominal);
            const bool safe=updated&&fresh_ok&&persistent.m_buff&&fresh.m_buff&&actual>=fresh_size;
            pixels&=safe&&std::memcmp(persistent.m_buff,fresh.m_buff,fresh_size)==0;
            slack&=safe&&actual>=first_capacity;
            if(safe&&actual>fresh_size) {
                const auto* bytes=static_cast<const u8*>(persistent.m_buff);
                slack&=std::all_of(bytes+fresh_size,bytes+actual,[](u8 byte){return byte==0;});
            }
            valid&=std::memcmp(persistent.m_valid,fresh.m_valid,sizeof(fresh.m_valid))==0;
        }
    }
    check(retained,"GS actual SW Reset retains grow-only unswizzle allocation across shape pitch and paletted format changes");
    check(pixels,"GS actual SW reused Update matches a freshly zeroed actual unswizzle byte for byte across five format/shape transitions");
    check(slack,"GS actual SW retained allocation clears all previous writes and x4 sampling slack beyond current nominal capacity");
    check(valid,"GS actual SW Reset/Update valid bitmap matches fresh unswizzle after fast and repeating map transitions");
}
extern "C" void EmuCoreXOracleSWTextureReuse(void(*check)(bool,const char*)) { GSTextureCacheSW::TestPersistentTexture(check); }
