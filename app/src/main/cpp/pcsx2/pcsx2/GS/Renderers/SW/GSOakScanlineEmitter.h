// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#pragma once
#include "arm64/OaknutHelpers-arm64.h"

class GSDrawOakEmitter
{
public:
	explicit GSDrawOakEmitter(oak::CodeGenerator& emitter)
		: m_emit(emitter)
	{
	}

	template <typename... Args>
	void ADD(Args&&... args) { m_emit.ADD(std::forward<Args>(args)...); }
	template <typename... Args>
	void ADDV(Args&&... args) { m_emit.ADDV(std::forward<Args>(args)...); }
	template <typename... Args>
	void AND(Args&&... args) { m_emit.AND(std::forward<Args>(args)...); }
	template <typename... Args>
	void ASR(Args&&... args) { m_emit.ASR(std::forward<Args>(args)...); }
	template <typename... Args>
	void B(Args&&... args) { m_emit.B(std::forward<Args>(args)...); }
	void BIC(oak::VReg_4S rd, int imm, int amount) { m_emit.BIC(rd, imm, oak::LslSymbol::LSL, amount); }
	void BIC(oak::VReg_8H rd, int imm, int amount) { m_emit.BIC(rd, imm, oak::LslSymbol::LSL, amount); }
	template <typename... Args>
	void BIC(Args&&... args) { m_emit.BIC(std::forward<Args>(args)...); }
	template <typename... Args>
	void BR(Args&&... args) { m_emit.BR(std::forward<Args>(args)...); }
	template <typename... Args>
	void BRK(Args&&... args) { m_emit.BRK(std::forward<Args>(args)...); }
	template <typename... Args>
	void BSL(Args&&... args) { m_emit.BSL(std::forward<Args>(args)...); }
	template <typename... Args>
	void CMEQ(Args&&... args) { m_emit.CMEQ(std::forward<Args>(args)...); }
	template <typename... Args>
	void CMGT(Args&&... args) { m_emit.CMGT(std::forward<Args>(args)...); }
	template <typename... Args>
	void CMN(Args&&... args) { m_emit.CMN(std::forward<Args>(args)...); }
	template <typename... Args>
	void CMP(Args&&... args) { m_emit.CMP(std::forward<Args>(args)...); }
	template <typename... Args>
	void EOR(Args&&... args) { m_emit.EOR(std::forward<Args>(args)...); }
	template <typename... Args>
	void EXT(Args&&... args) { m_emit.EXT(std::forward<Args>(args)...); }
	template <typename... Args>
	void FADD(Args&&... args) { m_emit.FADD(std::forward<Args>(args)...); }
	template <typename... Args>
	void FCVTL(Args&&... args) { m_emit.FCVTL(std::forward<Args>(args)...); }
	template <typename... Args>
	void FCVTL2(Args&&... args) { m_emit.FCVTL2(std::forward<Args>(args)...); }
	template <typename... Args>
	void FCVTZS(Args&&... args) { m_emit.FCVTZS(std::forward<Args>(args)...); }
	template <typename... Args>
	void FDIV(Args&&... args) { m_emit.FDIV(std::forward<Args>(args)...); }
	template <typename... Args>
	void FMAXNM(Args&&... args) { m_emit.FMAXNM(std::forward<Args>(args)...); }
	template <typename... Args>
	void FMINNM(Args&&... args) { m_emit.FMINNM(std::forward<Args>(args)...); }
	template <typename... Args>
	void FMLA(Args&&... args) { m_emit.FMLA(std::forward<Args>(args)...); }
	template <typename... Args>
	void FMOV(Args&&... args) { m_emit.FMOV(std::forward<Args>(args)...); }
	template <typename... Args>
	void FMUL(Args&&... args) { m_emit.FMUL(std::forward<Args>(args)...); }
	template <typename... Args>
	void FSUB(Args&&... args) { m_emit.FSUB(std::forward<Args>(args)...); }
	template <typename... Args>
	void LDP(Args&&... args) { m_emit.LDP(std::forward<Args>(args)...); }
	template <typename... Args>
	void LSL(Args&&... args) { m_emit.LSL(std::forward<Args>(args)...); }
	template <typename... Args>
	void MUL(Args&&... args) { m_emit.MUL(std::forward<Args>(args)...); }
	template <typename... Args>
	void MVN(Args&&... args) { m_emit.MVN(std::forward<Args>(args)...); }
	template <typename... Args>
	void NEG(Args&&... args) { m_emit.NEG(std::forward<Args>(args)...); }
	template <typename... Args>
	void ORR(Args&&... args) { m_emit.ORR(std::forward<Args>(args)...); }
	template <typename... Args>
	void RET(Args&&... args) { m_emit.RET(std::forward<Args>(args)...); }
	template <typename... Args>
	void SCVTF(Args&&... args) { m_emit.SCVTF(std::forward<Args>(args)...); }
	template <typename... Args>
	void SHL(Args&&... args) { m_emit.SHL(std::forward<Args>(args)...); }
	template <typename... Args>
	void SMAX(Args&&... args) { m_emit.SMAX(std::forward<Args>(args)...); }
	template <typename... Args>
	void SMIN(Args&&... args) { m_emit.SMIN(std::forward<Args>(args)...); }
	template <typename... Args>
	void SQDMULH(Args&&... args) { m_emit.SQDMULH(std::forward<Args>(args)...); }
	template <typename... Args>
	void SQXTN(Args&&... args) { m_emit.SQXTN(std::forward<Args>(args)...); }
	template <typename... Args>
	void SQXTN2(Args&&... args) { m_emit.SQXTN2(std::forward<Args>(args)...); }
	template <typename... Args>
	void SQXTUN(Args&&... args) { m_emit.SQXTUN(std::forward<Args>(args)...); }
	template <typename... Args>
	void SQXTUN2(Args&&... args) { m_emit.SQXTUN2(std::forward<Args>(args)...); }
	template <typename... Args>
	void SSHL(Args&&... args) { m_emit.SSHL(std::forward<Args>(args)...); }
	template <typename... Args>
	void SSHR(Args&&... args) { m_emit.SSHR(std::forward<Args>(args)...); }
	template <typename... Args>
	void STP(Args&&... args) { m_emit.STP(std::forward<Args>(args)...); }
	template <typename... Args>
	void SUB(Args&&... args) { m_emit.SUB(std::forward<Args>(args)...); }
	template <typename... Args>
	void TRN1(Args&&... args) { m_emit.TRN1(std::forward<Args>(args)...); }
	template <typename... Args>
	void TRN2(Args&&... args) { m_emit.TRN2(std::forward<Args>(args)...); }
	template <typename... Args>
	void TST(Args&&... args) { m_emit.TST(std::forward<Args>(args)...); }
	template <typename... Args>
	void UMIN(Args&&... args) { m_emit.UMIN(std::forward<Args>(args)...); }
	template <typename... Args>
	void UMINV(Args&&... args) { m_emit.UMINV(std::forward<Args>(args)...); }
	template <typename... Args>
	void UQADD(Args&&... args) { m_emit.UQADD(std::forward<Args>(args)...); }
	template <typename... Args>
	void USHL(Args&&... args) { m_emit.USHL(std::forward<Args>(args)...); }
	template <typename... Args>
	void USHLL(Args&&... args) { m_emit.USHLL(std::forward<Args>(args)...); }
	template <typename... Args>
	void USHR(Args&&... args) { m_emit.USHR(std::forward<Args>(args)...); }
	template <typename... Args>
	void UZP1(Args&&... args) { m_emit.UZP1(std::forward<Args>(args)...); }
	template <typename... Args>
	void ZIP1(Args&&... args) { m_emit.ZIP1(std::forward<Args>(args)...); }
	template <typename... Args>
	void ZIP2(Args&&... args) { m_emit.ZIP2(std::forward<Args>(args)...); }

	void l(oak::Label& label) { m_emit.l(label); }

	void DUP(oak::VReg_8H rd, oak::VReg_8H rn, int lane) { m_emit.DUP(rd, oak::QReg(rn.index()).Helem()[lane]); }
	void DUP(oak::VReg_4S rd, oak::VReg_4S rn, int lane) { m_emit.DUP(rd, oak::QReg(rn.index()).Selem()[lane]); }
	void DUP(oak::VReg_2D rd, oak::VReg_2D rn, int lane) { m_emit.DUP(rd, oak::QReg(rn.index()).Delem()[lane]); }
	template <typename... Args>
	void DUP(Args&&... args) { m_emit.DUP(std::forward<Args>(args)...); }

	void LDR(oak::WReg dst, OakMemOperand mem) { oakLoad32(dst, mem); }
	void LDR(oak::XReg dst, OakMemOperand mem) { oakLoad64(dst, mem); }
	void LDR(oak::SReg dst, OakMemOperand mem) { oakLoadScalar32(dst, mem); }
	void LDR(oak::DReg dst, OakMemOperand mem) { oakLoadVector64(dst, mem); }
	void LDR(oak::QReg dst, OakMemOperand mem) { oakLoad128(dst, mem); }
	template <typename... Args>
	void LDR(Args&&... args) { m_emit.LDR(std::forward<Args>(args)...); }

	void LDRB(oak::WReg dst, OakMemOperand mem)
	{
		m_emit.ADD(OAK_XSCRATCH, mem.base, mem.offset);
		m_emit.LDRB(dst, OAK_XSCRATCH);
	}
	template <typename... Args>
	void LDRB(Args&&... args) { m_emit.LDRB(std::forward<Args>(args)...); }

	void STR(oak::WReg src, OakMemOperand mem) { oakStore32(src, mem); }
	void STR(oak::XReg src, OakMemOperand mem) { oakStore64(src, mem); }
	void STR(oak::SReg src, OakMemOperand mem) { m_emit.FMOV(OAK_WSCRATCH, src); oakStore32(OAK_WSCRATCH, mem); }
	void STR(oak::DReg src, OakMemOperand mem) { m_emit.FMOV(OAK_XSCRATCH, src); oakStore64(OAK_XSCRATCH, mem); }
	void STR(oak::QReg src, OakMemOperand mem) { oakStore128(src, mem); }
	void STR(oak::VReg_4S src, OakMemOperand mem) { oakStore128(oak::QReg(src.index()), mem); }
	template <typename... Args>
	void STR(Args&&... args) { m_emit.STR(std::forward<Args>(args)...); }

	void STRH(oak::WReg src, OakMemOperand mem) { oakStore16(src, mem); }
	template <typename... Args>
	void STRH(Args&&... args) { m_emit.STRH(std::forward<Args>(args)...); }

	void LD1R(oak::VReg_4S dst, OakMemOperand mem) { oakLoad32(OAK_WSCRATCH, mem); m_emit.DUP(dst, OAK_WSCRATCH); }
	void LD1R(oak::VReg_2D dst, OakMemOperand mem) { oakLoad64(OAK_XSCRATCH, mem); m_emit.DUP(dst, OAK_XSCRATCH); }
	void LD1R(oak::VReg_8H dst, OakMemOperand mem) { oakLoad16(OAK_WSCRATCH, mem); m_emit.DUP(dst, OAK_WSCRATCH); }

	void LD1(oak::VReg_2D dst, int lane, OakMemOperand mem)
	{
		oakLoad64(OAK_XSCRATCH, mem);
		m_emit.INS(oak::QReg(dst.index()).Delem()[lane], OAK_XSCRATCH);
	}
	void LD1(oak::VReg_2D dst, u8 lane, OakMemOperand mem) { LD1(dst, static_cast<int>(lane), mem); }
	void LD1(oak::VReg_4S dst, int lane, OakMemOperand mem)
	{
		oakLoad32(OAK_WSCRATCH, mem);
		m_emit.INS(oak::QReg(dst.index()).Selem()[lane], OAK_WSCRATCH);
	}
	void LD1(oak::VReg_4S dst, u8 lane, OakMemOperand mem) { LD1(dst, static_cast<int>(lane), mem); }
	template <typename... Args>
	void LD1(Args&&... args) { m_emit.LD1(std::forward<Args>(args)...); }

	void ST1(oak::VReg_2D src, int lane, OakMemOperand mem)
	{
		m_emit.UMOV(OAK_XSCRATCH, oak::QReg(src.index()).Delem()[lane]);
		oakStore64(OAK_XSCRATCH, mem);
	}
	void ST1(oak::VReg_2D src, u8 lane, OakMemOperand mem) { ST1(src, static_cast<int>(lane), mem); }
	void ST1(oak::VReg_4S src, int lane, OakMemOperand mem)
	{
		m_emit.UMOV(OAK_WSCRATCH, oak::QReg(src.index()).Selem()[lane]);
		oakStore32(OAK_WSCRATCH, mem);
	}
	void ST1(oak::VReg_4S src, u8 lane, OakMemOperand mem) { ST1(src, static_cast<int>(lane), mem); }

	void MOVI(oak::VReg_2D rd, u64 imm)
	{
		m_emit.MOV(OAK_XSCRATCH, imm);
		m_emit.DUP(rd, OAK_XSCRATCH);
	}
	void MOVI(oak::VReg_2D rd, unsigned long long imm) { MOVI(rd, static_cast<u64>(imm)); }
	void MOVI(oak::VReg_4S rd, u32 imm)
	{
		if (imm <= 0xff)
		{
			m_emit.MOVI(rd, imm);
			return;
		}

		m_emit.MOV(OAK_WSCRATCH, imm);
		m_emit.DUP(rd, OAK_WSCRATCH);
	}
	void MOVI(oak::VReg_4S rd, int imm) { MOVI(rd, static_cast<u32>(imm)); }
	template <typename... Args>
	void MOVI(Args&&... args) { m_emit.MOVI(std::forward<Args>(args)...); }

	void MOV(oak::QReg dst, oak::QReg src) { m_emit.MOV(dst.B16(), src.B16()); }
	void MOV(oak::WReg dst, oak::VReg_4S src, int lane) { m_emit.UMOV(dst, oak::QReg(src.index()).Selem()[lane]); }
	void MOV(oak::WReg dst, oak::VReg_4S src, u8 lane) { MOV(dst, src, static_cast<int>(lane)); }
	template <typename... Args>
	void MOV(Args&&... args) { m_emit.MOV(std::forward<Args>(args)...); }

	void UMOV(oak::WReg dst, oak::VReg_8H src, int lane) { m_emit.UMOV(dst, oak::QReg(src.index()).Helem()[lane]); }
	void UMOV(oak::WReg dst, oak::VReg_4S src, int lane) { m_emit.UMOV(dst, oak::QReg(src.index()).Selem()[lane]); }
	void UMOV(oak::XReg dst, oak::VReg_2D src, int lane) { m_emit.UMOV(dst, oak::QReg(src.index()).Delem()[lane]); }
	template <typename... Args>
	void UMOV(Args&&... args) { m_emit.UMOV(std::forward<Args>(args)...); }

	template <typename... Args>
	void ABS(Args&&... args) { m_emit.ABS(std::forward<Args>(args)...); }

	template <typename... Args>
	void CSET(Args&&... args) { m_emit.CSET(std::forward<Args>(args)...); }

	template <typename... Args>
	void CSETM(Args&&... args) { m_emit.CSETM(std::forward<Args>(args)...); }

	template <typename... Args>
	void FCMGT(Args&&... args) { m_emit.FCMGT(std::forward<Args>(args)...); }

	template <typename... Args>
	void FCVTMS(Args&&... args) { m_emit.FCVTMS(std::forward<Args>(args)...); }

	template <typename... Args>
	void FCVTN(Args&&... args) { m_emit.FCVTN(std::forward<Args>(args)...); }

	template <typename... Args>
	void FCVTN2(Args&&... args) { m_emit.FCVTN2(std::forward<Args>(args)...); }

	template <typename... Args>
	void REV64(Args&&... args) { m_emit.REV64(std::forward<Args>(args)...); }

	template <typename... Args>
	void SMULL(Args&&... args) { m_emit.SMULL(std::forward<Args>(args)...); }

	template <typename... Args>
	void SQSHRUN(Args&&... args) { m_emit.SQSHRUN(std::forward<Args>(args)...); }

	template <typename... Args>
	void SXTL(Args&&... args) { m_emit.SXTL(std::forward<Args>(args)...); }

	template <typename... Args>
	void UXTL(Args&&... args) { m_emit.UXTL(std::forward<Args>(args)...); }

    void LDR(oak::VReg_4S dst, OakMemOperand mem) { oakLoad128(oak::QReg(dst.index()), mem); }
    void LDR(oak::VReg_2D dst, OakMemOperand mem) { oakLoad128(oak::QReg(dst.index()), mem); }
    void STR(oak::VReg_2D src, OakMemOperand mem) { oakStore128(oak::QReg(src.index()), mem); }
    void LDRH(oak::WReg dst, OakMemOperand mem) { oakLoad16(dst,mem); }
    void FMUL(oak::VReg_4S dst,oak::VReg_4S src,oak::SReg lane,int i) { m_emit.FMUL(dst,src,oak::QReg(lane.index()).Selem()[i]); }
private:
	oak::CodeGenerator& m_emit;
};
