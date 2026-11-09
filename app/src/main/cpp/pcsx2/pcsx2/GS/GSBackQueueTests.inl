// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "common/ScopedGuard.h"
#include "cpuinfo.h"
#if __has_include("GS/GSBackQueue.h")
#include "GS/GSBackQueue.h"
#include "GS/Renderers/Common/GSBackThreadPolicy.h"
#endif
void GSState::TestBackQueue(void (*check)(bool,const char*))
{
#if !__has_include("GS/GSBackQueue.h")
    check(false,"GS front/back production record producers, consumer and drains are available");
#else
    cpuinfo_initialize();
    const auto saved=GSConfig;
    const ScopedGuard restore([&](){GSConfig=saved;});
    GSConfig=Pcsx2Config::GSOptions();
    GSConfig.Renderer=GSRendererType::SW;
    GSConfig.BackThread=true;
    GSConfig.BackThreadResolved=true;
    struct Snapshot {u32 color;u32 x;u64 serial;u32 display_fbp;bool blend;};
    struct State final:GSState
    {
        std::vector<Snapshot> draws;
        void Draw() override {draws.push_back({m_vertex.buff[0].RGBAQ.U32[0],m_vertex.buff[0].XYZ.X,s_n,m_draw_priv.dispfb_fbp[0],PRIM->ABE!=0});}
    };
    auto back=std::make_unique<State>();
    GSPrivRegSet regs={}; back->SetRegsMem(reinterpret_cast<u8*>(&regs));
    auto front=std::make_unique<GSFrontState>(back.get());
    front->SetRegsMem(reinterpret_cast<u8*>(&regs));
    check(back->IsBackThreadRunning(),"GS configured back consumer starts");
    for(u32 i=0;i<130;i++)
    {
        regs.DISP[0].DISPFB.FBP=i;
        front->m_env.PRIM.U64=0;front->m_env.PRIM.PRIM=GS_POINTLIST;
        front->m_prev_env=front->m_env;front->m_draw_env=&front->m_prev_env;
        front->PRIM=&front->m_prev_env.PRIM;front->UpdateContext();
        front->m_vertex.buff[0]={};front->m_vertex.buff[0].RGBAQ.U32[0]=i;
        front->m_vertex.buff[0].XYZ.X=i;front->m_vertex.tail=1;front->m_vertex.head=1;
        front->m_vertex.next=1;front->m_index.buff[0]=0;front->m_index.tail=1;
        front->FlushPrim();
    }
    front->DrainBackQueue();
    check(back->draws.size()==130,"GS draw producer/consumer survives bounded pool recycling");
    bool owned=back->draws.size()==130;
    for(u32 i=0;i<back->draws.size();i++) owned &= back->draws[i].color==i && back->draws[i].x==i && back->draws[i].serial==i+1 && back->draws[i].display_fbp==i;
    check(owned,"GS queued draw arrays and serials stay immutable after front reuse");
    // Actual HOST->LOCAL producer must own the bytes rather than retain packet pointers.
    GIFRegBITBLTBUF blit={};blit.DBP=32;blit.DBW=1;blit.DPSM=PSMCT32;
    GIFRegTRXPOS pos={};GIFRegTRXREG reg={};reg.RRW=4;reg.RRH=1;
    front->m_env.BITBLTBUF=blit;front->m_env.TRXPOS=pos;front->m_env.TRXREG=reg;front->m_env.TRXDIR.XDIR=0;
    front->RotateTransferPayload();front->m_tr.Init(pos,reg,blit,true);
    u32 packet[4]={0x12345678,0x9abcdef0,0x10203040,0xaabbccdd};
    front->Write(reinterpret_cast<const u8*>(packet),sizeof(packet));front->FlushWrite();
    std::memset(packet,0,sizeof(packet));front->DrainBackQueue();
    const GSOffset offset=back->m_mem.GetOffset(blit.DBP,blit.DBW,blit.DPSM);
    check(back->m_mem.ReadPixel32(0,0,blit.DBP,blit.DBW)==0x12345678 && back->m_mem.ReadPixel32(3,0,blit.DBP,blit.DBW)==0xaabbccdd,
        "GS transfer producer copies packet bytes before publication and drains authoritative memory");
    // The same source is moved twice with no draw between them; every MOVE must carry XDIR=2.
    front->m_env.BITBLTBUF=blit;front->m_env.BITBLTBUF.SBP=blit.DBP;
    front->m_env.BITBLTBUF.SBW=1;front->m_env.BITBLTBUF.SPSM=PSMCT32;
    front->m_env.TRXPOS=pos;front->m_env.TRXREG=reg;
    front->m_env.BITBLTBUF.DBP=64;front->m_env.TRXDIR.XDIR=2;front->SubmitMove();
    front->m_env.BITBLTBUF.DBP=96;front->m_env.TRXDIR.XDIR=2;front->SubmitMove();front->DrainBackQueue();
    check(back->m_mem.ReadPixel32(0,0,64,1)==0x12345678 && back->m_mem.ReadPixel32(0,0,96,1)==0x12345678,
        "GS repeated queued moves restore direction independently of previous executor state");
    // Rotation must retire buffers only after every referenced FIFO slice has completed.
    for(u32 i=0;i<40;i++)
    {
        front->RotateTransferPayload();blit.DBP=160+i;
        front->m_env.BITBLTBUF=blit;front->m_env.TRXPOS=pos;front->m_env.TRXREG=reg;front->m_env.TRXDIR.XDIR=0;
        front->m_tr.Init(pos,reg,blit,true);
        u32 bytes[4]={i+1,i+1,i+1,i+1};front->Write(reinterpret_cast<const u8*>(bytes),sizeof(bytes));
    }
    front->DrainBackQueue();bool payloads=true;
    for(u32 i=0;i<40;i++)payloads &= back->m_mem.ReadPixel32(0,0,160+i,1)==i+1;
    check(payloads,"GS actual packet producer survives bounded payload pool recycling");
    front->RotateTransferPayload();blit.DBP=320;reg.RRW=16;reg.RRH=1;
    front->m_env.BITBLTBUF=blit;front->m_env.TRXPOS=pos;front->m_env.TRXREG=reg;front->m_env.TRXDIR.XDIR=0;
    front->m_tr.Init(pos,reg,blit,true);
    u32 palette[16];for(u32 i=0;i<16;i++)palette[i]=0x80000000u+i;
    front->Write(reinterpret_cast<const u8*>(palette),sizeof(palette));
    GIFRegTEX0 tex0={};tex0.CBP=320;tex0.CPSM=PSMCT32;tex0.PSM=PSMT4;tex0.CSM=1;
    GIFRegTEXCLUT texclut={};texclut.CBW=1;
    front->SubmitClutLoad(tex0,texclut);front->DrainBackQueue();
    back->m_mem.m_clut.Read32(tex0,GIFRegTEXA{});
    check(back->m_mem.m_clut[0]==0x80000000u && back->m_mem.m_clut[15]==0x8000000fu,
        "GS actual CLUT consumer reads palette bytes after the queued host upload");
    reg.RRW=4;
    // Save a half-completed upload, then resume it from the authoritative backend cursor.
    front->RotateTransferPayload();blit.DBP=256;reg.RRH=2;
    front->m_env.BITBLTBUF=blit;front->m_env.TRXPOS=pos;front->m_env.TRXREG=reg;front->m_env.TRXDIR.XDIR=0;
    front->m_tr.Init(pos,reg,blit,true);
    const u32 first[4]={11,12,13,14},second[4]={21,22,23,24};
    front->Write(reinterpret_cast<const u8*>(first),sizeof(first));
    freezeData fd={};front->Freeze(&fd,true);std::vector<u8> frozen(fd.size);fd.data=frozen.data();
    const int saved_state=front->Freeze(&fd,false);const int loaded_state=front->Defrost(&fd);
    front->Write(reinterpret_cast<const u8*>(second),sizeof(second));front->FlushWrite();front->DrainBackQueue();
    check(saved_state==0 && loaded_state==0 && back->m_mem.ReadPixel32(0,0,256,1)==11 &&
        back->m_mem.ReadPixel32(0,1,256,1)==21 && back->m_mem.ReadPixel32(3,1,256,1)==24,
        "GS save/load drains authoritative memory and resumes partial-transfer executor cursor");
    front->PCRTCDisplays.PCRTCDisplays[0].FBP=42;front->m_scanmask_used=2;front->SubmitPcrtcSync();
    front->PCRTCDisplays.PCRTCDisplays[0].FBP=99;front->DrainBackQueue();
    check(back->PCRTCDisplays.PCRTCDisplays[0].FBP==42 && back->m_scanmask_used==2,
        "GS queued PCRTC carries the immutable full digest and pre-decrement scanmask");
    check(GSDecideBackThread({true,true,false,GSHardwareDownloadMode::Enabled}).on==false,
        "GS hardware GL cannot queue off its context thread");
    check(GSDecideBackThread({true,true,true,GSHardwareDownloadMode::Unsynchronized}).on==false,
        "GS live-memory EE downloads disable pipelining");
    check(GSDecideBackThread({true,false,false,GSHardwareDownloadMode::Unsynchronized}).on,
        "GS software rendering remains queueable without a Vulkan device");
    // Real XYZ register producers must split compatible opaque/blend draws into owned slots.
    GSConfig.UserHacks_DrawBuffering=true;front->Reset(false);front->ResetHandlers();
    front->m_env.PRMODECONT.AC=1;
    front->m_env.PRIM.PRIM=GS_TRIANGLELIST;front->m_env.PRIM.IIP=1;
    auto& ctxt=front->m_env.CTXT[0];ctxt.FRAME.FBW=1;ctxt.ZBUF.ZMSK=0;ctxt.ZBUF.PSM=PSMZ32;
    ctxt.TEST.ZTE=1;ctxt.TEST.ZTST=ZTST_GEQUAL;ctxt.SCISSOR.SCAX1=64;ctxt.SCISSOR.SCAY1=64;
    ctxt.UpdateScissor();front->PRIM=&front->m_env.PRIM;front->m_draw_env=&front->m_env;
    front->m_prev_env=front->m_env;front->UpdateContext();front->UpdateVertexKick();
    front->m_v.RGBAQ.Q=1.0f;front->m_v.RGBAQ.A=128;
    const auto triangle=[&](u32 x)
    {
        const u32 xy[3][2]={{x+16,16},{x+64,16},{x+16,64}};
        for(const auto& v:xy){GIFReg r={};r.XYZ.X=v[0];r.XYZ.Y=v[1];(front.get()->*front->m_fpGIFRegHandlers[GIF_A_D_REG_XYZ2])(&r);}
    };
    const auto drawn_before=back->draws.size();triangle(0);
    GIFReg alpha={};alpha.ALPHA.A=0;alpha.ALPHA.B=1;alpha.ALPHA.C=2;alpha.ALPHA.D=1;alpha.ALPHA.FIX=64;
    front->GIFRegHandlerALPHA<0>(&alpha);
    GIFReg prim={};prim.PRIM=front->m_env.PRIM;prim.PRIM.ABE=1;front->GIFRegHandlerPRIM(&prim);
    check(front->m_env.PRIM.ABE==1,"GS buffered fixture enables PRIM-controlled attributes through PRMODECONT.AC");
    triangle(128);
    check(front->m_used_buffers_idx==2,"GS real register producers buffer a compatible following blend draw");
    front->Flush(GSFlushReason::RESET);front->DrainBackQueue();
    check(back->draws.size()==drawn_before+2 && !back->draws[drawn_before].blend && back->draws[drawn_before+1].blend,
        "GS buffered draw flush transfers independent arrays and the environment captured on each first accepted primitive");
    GSConfig.UserHacks_DrawBuffering=false;front->ResetHandlers();
    front->PCRTCDisplays.PCRTCDisplays[0].FBP=777;front->SubmitPcrtcSync();
    front.reset();
    check(back->PCRTCDisplays.PCRTCDisplays[0].FBP==777,"GS front destruction drains queued records before releasing parse ownership");
    back->StopBackThread();
    check(!back->IsBackThreadRunning(),"GS front destruction drains before back worker shutdown");
#endif
}
