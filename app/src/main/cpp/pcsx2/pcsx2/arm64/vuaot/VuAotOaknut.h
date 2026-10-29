// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+
#pragma once
// Focused region-emitter adapter. All instructions are encoded by oaknut.
// x17 is the only macro scratch register; region allocation reserves it.
#include "common/Pcsx2Defs.h"
#include "VuAotConstants.h"
#include <oaknut/oaknut.hpp>
#include <memory>
#include <stdexcept>
#include <type_traits>
namespace vuaot
{
	namespace a64
	{
		using Register = oaknut::XReg;
		using SRegister = oaknut::SReg;
		using Label = oaknut::Label;
		using Condition = oaknut::Cond;
		inline constexpr auto eq = Condition::EQ, ne = Condition::NE, ge = Condition::GE, gt = Condition::GT, le = Condition::LE, lt = Condition::LT;
		enum Shift
		{
			LSL,
			LSR
		};
		struct Operand
		{
			oaknut::WReg reg;
			Shift shift;
			int amount;
			Operand(oaknut::WReg r, Shift s, int n)
				: reg(r)
				, shift(s)
				, amount(n)
			{
			}
		};
		struct VRegister : oaknut::QReg
		{
			explicit constexpr VRegister(int n)
				: QReg(n)
			{
			}
			oaknut::VReg_16B V16B() const { return oaknut::VReg_16B(index()); }
			oaknut::VReg_4S V4S() const { return oaknut::VReg_4S(index()); }
			oaknut::VReg_8H V8H() const { return oaknut::VReg_8H(index()); }
			oaknut::VReg_2D V2D() const { return oaknut::VReg_2D(index()); }
		};
		inline constexpr oaknut::XReg x0{0};
		inline constexpr oaknut::WReg w0{0};
		inline constexpr VRegister q0{0}, v0{0};
		inline constexpr oaknut::XReg x1{1};
		inline constexpr oaknut::WReg w1{1};
		inline constexpr VRegister q1{1}, v1{1};
		inline constexpr oaknut::XReg x2{2};
		inline constexpr oaknut::WReg w2{2};
		inline constexpr VRegister q2{2}, v2{2};
		inline constexpr oaknut::XReg x3{3};
		inline constexpr oaknut::WReg w3{3};
		inline constexpr VRegister q3{3}, v3{3};
		inline constexpr oaknut::XReg x4{4};
		inline constexpr oaknut::WReg w4{4};
		inline constexpr VRegister q4{4}, v4{4};
		inline constexpr oaknut::XReg x5{5};
		inline constexpr oaknut::WReg w5{5};
		inline constexpr VRegister q5{5}, v5{5};
		inline constexpr oaknut::XReg x6{6};
		inline constexpr oaknut::WReg w6{6};
		inline constexpr VRegister q6{6}, v6{6};
		inline constexpr oaknut::XReg x7{7};
		inline constexpr oaknut::WReg w7{7};
		inline constexpr VRegister q7{7}, v7{7};
		inline constexpr oaknut::XReg x8{8};
		inline constexpr oaknut::WReg w8{8};
		inline constexpr VRegister q8{8}, v8{8};
		inline constexpr oaknut::XReg x9{9};
		inline constexpr oaknut::WReg w9{9};
		inline constexpr VRegister q9{9}, v9{9};
		inline constexpr oaknut::XReg x10{10};
		inline constexpr oaknut::WReg w10{10};
		inline constexpr VRegister q10{10}, v10{10};
		inline constexpr oaknut::XReg x11{11};
		inline constexpr oaknut::WReg w11{11};
		inline constexpr VRegister q11{11}, v11{11};
		inline constexpr oaknut::XReg x12{12};
		inline constexpr oaknut::WReg w12{12};
		inline constexpr VRegister q12{12}, v12{12};
		inline constexpr oaknut::XReg x13{13};
		inline constexpr oaknut::WReg w13{13};
		inline constexpr VRegister q13{13}, v13{13};
		inline constexpr oaknut::XReg x14{14};
		inline constexpr oaknut::WReg w14{14};
		inline constexpr VRegister q14{14}, v14{14};
		inline constexpr oaknut::XReg x15{15};
		inline constexpr oaknut::WReg w15{15};
		inline constexpr VRegister q15{15}, v15{15};
		inline constexpr oaknut::XReg x16{16};
		inline constexpr oaknut::WReg w16{16};
		inline constexpr VRegister q16{16}, v16{16};
		inline constexpr oaknut::XReg x17{17};
		inline constexpr oaknut::WReg w17{17};
		inline constexpr VRegister q17{17}, v17{17};
		inline constexpr oaknut::XReg x18{18};
		inline constexpr oaknut::WReg w18{18};
		inline constexpr VRegister q18{18}, v18{18};
		inline constexpr oaknut::XReg x19{19};
		inline constexpr oaknut::WReg w19{19};
		inline constexpr VRegister q19{19}, v19{19};
		inline constexpr oaknut::XReg x20{20};
		inline constexpr oaknut::WReg w20{20};
		inline constexpr VRegister q20{20}, v20{20};
		inline constexpr oaknut::XReg x21{21};
		inline constexpr oaknut::WReg w21{21};
		inline constexpr VRegister q21{21}, v21{21};
		inline constexpr oaknut::XReg x22{22};
		inline constexpr oaknut::WReg w22{22};
		inline constexpr VRegister q22{22}, v22{22};
		inline constexpr oaknut::XReg x23{23};
		inline constexpr oaknut::WReg w23{23};
		inline constexpr VRegister q23{23}, v23{23};
		inline constexpr oaknut::XReg x24{24};
		inline constexpr oaknut::WReg w24{24};
		inline constexpr VRegister q24{24}, v24{24};
		inline constexpr oaknut::XReg x25{25};
		inline constexpr oaknut::WReg w25{25};
		inline constexpr VRegister q25{25}, v25{25};
		inline constexpr oaknut::XReg x26{26};
		inline constexpr oaknut::WReg w26{26};
		inline constexpr VRegister q26{26}, v26{26};
		inline constexpr oaknut::XReg x27{27};
		inline constexpr oaknut::WReg w27{27};
		inline constexpr VRegister q27{27}, v27{27};
		inline constexpr oaknut::XReg x28{28};
		inline constexpr oaknut::WReg w28{28};
		inline constexpr VRegister q28{28}, v28{28};
		inline constexpr oaknut::XReg x29{29};
		inline constexpr oaknut::WReg w29{29};
		inline constexpr VRegister q29{29}, v29{29};
		inline constexpr oaknut::XReg x30{30};
		inline constexpr oaknut::WReg w30{30};
		inline constexpr VRegister q30{30}, v30{30};
		inline constexpr oaknut::XReg x31{31};
		inline constexpr oaknut::WReg w31{31};
		inline constexpr VRegister q31{31}, v31{31};
		inline constexpr oaknut::SpReg sp{};
		inline constexpr oaknut::WReg wzr{31};
		struct MemOperand
		{
			oaknut::XRegSp base;
			int64_t offset = 0;
			int index = -1;
			explicit MemOperand(oaknut::XRegSp b, int64_t o = 0)
				: base(b)
				, offset(o)
			{
			}
			MemOperand(oaknut::XRegSp b, oaknut::XReg i)
				: base(b)
				, index(i.index())
			{
			}
		};
		class MacroAssembler
		{
			std::unique_ptr<oaknut::CodeGenerator> owned_;
			oaknut::CodeGenerator* cg_;
			u8* begin_;
			size_t capacity_;
			void Check(size_t n = 32) const
			{
				if (static_cast<size_t>(Cursor() - begin_) + n > capacity_)
					throw std::runtime_error("VU AOT code buffer exhausted");
			}

		public:
			explicit MacroAssembler(oaknut::CodeGenerator& cg, size_t capacity = 16u << 20)
				: cg_(&cg)
				, begin_(cg.wptr<u8*>())
				, capacity_(capacity)
			{
			}
			MacroAssembler(u8* p, size_t size)
				: owned_(std::make_unique<oaknut::CodeGenerator>(reinterpret_cast<u32*>(p)))
				, cg_(owned_.get())
				, begin_(p)
				, capacity_(size)
			{
			}
			u8* Cursor() const { return cg_->wptr<u8*>(); }
			size_t GetSizeOfCodeGenerated() const { return Cursor() - begin_; }
			oaknut::CodeGenerator& Raw()
			{
				Check();
				return *cg_;
			}
			void Bind(Label* l) { cg_->l(*l); }
			void B(Label* l)
			{
				Check();
				cg_->B(*l);
			}
			void B(Label* l, Condition c)
			{
				Check();
				cg_->B(c, *l);
			}
			void Adr(oaknut::XReg d, Label* l)
			{
				Check();
				cg_->ADR(d, *l);
			}
			template <class R>
			void Cbz(R r, Label* l)
			{
				Check();
				cg_->CBZ(r, *l);
			}
			template <class R>
			void Tbz(R r, int bit, Label* l)
			{
				Check();
				cg_->TBZ(r, bit, *l);
			}
			template <class R>
			void Cbnz(R r, Label* l)
			{
				Check();
				cg_->CBNZ(r, *l);
			}
			template <class... A>
			void Fcvt(A... a)
			{
				Check();
				cg_->FCVT(a...);
			}
			template <class... A>
			void Fmadd(A... a)
			{
				Check();
				cg_->FMADD(a...);
			}
			template <class... A>
			void Shl(A... a)
			{
				Check();
				cg_->SHL(a...);
			}

			void dc32(u32 x)
			{
				Check(4);
				cg_->dw(x);
			}
			void dc64(u64 x)
			{
				Check(8);
				cg_->dx(x);
			}
			template <class... A>
			void Addv(A... a)
			{
				Check();
				cg_->ADDV(a...);
			}
			template <class... A>
			void Bif(A... a)
			{
				Check();
				cg_->BIF(a...);
			}
			template <class... A>
			void Bsl(A... a)
			{
				Check();
				cg_->BSL(a...);
			}
			template <class... A>
			void Cmeq(A... a)
			{
				Check();
				cg_->CMEQ(a...);
			}
			template <class... A>
			void Cmgt(A... a)
			{
				Check();
				cg_->CMGT(a...);
			}
			template <class... A>
			void Cmlt(A... a)
			{
				Check();
				cg_->CMLT(a...);
			}
			template <class... A>
			void Csel(A... a)
			{
				Check();
				cg_->CSEL(a...);
			}
			template <class... A>
			void Cset(A... a)
			{
				Check();
				cg_->CSET(a...);
			}
			template <class A, class B>
			void Dup(A a, B b)
			{
				Check();
				cg_->DUP(a, b);
			}
			template <class... A>
			void Ext(A... a)
			{
				Check();
				cg_->EXT(a...);
			}
			template <class... A>
			void Fabs(A... a)
			{
				Check();
				cg_->FABS(a...);
			}
			template <class... A>
			void Fadd(A... a)
			{
				Check();
				cg_->FADD(a...);
			}
			template <class... A>
			void Faddp(A... a)
			{
				Check();
				cg_->FADDP(a...);
			}
			template <class... A>
			void Fcmeq(A... a)
			{
				Check();
				cg_->FCMEQ(a...);
			}
			template <class... A>
			void Fcmp(A... a)
			{
				Check();
				cg_->FCMP(a...);
			}
			template <class... A>
			void Fcvtzs(A... a)
			{
				Check();
				cg_->FCVTZS(a...);
			}
			template <class... A>
			void Fdiv(A... a)
			{
				Check();
				cg_->FDIV(a...);
			}
			template <class... A>
			void Fmaxnm(A... a)
			{
				Check();
				cg_->FMAXNM(a...);
			}
			template <class... A>
			void Fminnm(A... a)
			{
				Check();
				cg_->FMINNM(a...);
			}
			template <class... A>
			void Fmov(A... a)
			{
				Check();
				cg_->FMOV(a...);
			}
			template <class... A>
			void Fmul(A... a)
			{
				Check();
				cg_->FMUL(a...);
			}
			template <class... A>
			void Fsqrt(A... a)
			{
				Check();
				cg_->FSQRT(a...);
			}
			template <class... A>
			void Fsub(A... a)
			{
				Check();
				cg_->FSUB(a...);
			}
			template <class... A>
			void Lsl(A... a)
			{
				Check();
				cg_->LSL(a...);
			}
			template <class... A>
			void Lsr(A... a)
			{
				Check();
				cg_->LSR(a...);
			}
			template <class... A>
			void Mov(A... a)
			{
				Check();
				cg_->MOV(a...);
			}
			template <class... A>
			void Rev64(A... a)
			{
				Check();
				cg_->REV64(a...);
			}
			template <class... A>
			void Scvtf(A... a)
			{
				Check();
				cg_->SCVTF(a...);
			}
			template <class... A>
			void Sli(A... a)
			{
				Check();
				cg_->SLI(a...);
			}
			template <class... A>
			void Smax(A... a)
			{
				Check();
				cg_->SMAX(a...);
			}
			template <class... A>
			void Smin(A... a)
			{
				Check();
				cg_->SMIN(a...);
			}
			template <class... A>
			void Sri(A... a)
			{
				Check();
				cg_->SRI(a...);
			}
			template <class... A>
			void Sshr(A... a)
			{
				Check();
				cg_->SSHR(a...);
			}
			template <class... A>
			void Sxth(A... a)
			{
				Check();
				cg_->SXTH(a...);
			}
			template <class... A>
			void Ubfiz(A... a)
			{
				Check();
				cg_->UBFIZ(a...);
			}
			template <class... A>
			void Umin(A... a)
			{
				Check();
				cg_->UMIN(a...);
			}
			template <class... A>
			void Ushr(A... a)
			{
				Check();
				cg_->USHR(a...);
			}
			template <class... A>
			void Uxth(A... a)
			{
				Check();
				cg_->UXTH(a...);
			}
			template <class... A>
			void Uzp1(A... a)
			{
				Check();
				cg_->UZP1(a...);
			}
			template <class... A>
			void Bfxil(A... a)
			{
				Check();
				cg_->BFXIL(a...);
			}
			template <class... A>
			void Br(A... a)
			{
				Check();
				cg_->BR(a...);
			}
			template <class R>
			void Add(R d, R n, R m)
			{
				Check();
				cg_->ADD(d, n, m);
			}
			template <class R, class I>
				requires std::is_integral_v<I>
			void Add(R d, R n, I v)
			{
				Check();
				const u64 x = static_cast<u64>(v);
				if (oaknut::AddSubImm::is_valid(x))
					cg_->ADD(d, n, x);
				else
				{
					cg_->MOV(oaknut::XReg{17}, x);
					if constexpr (std::is_same_v<R, oaknut::WReg>)
						cg_->ADD(d, n, oaknut::WReg{17});
					else
						cg_->ADD(d, n, oaknut::XReg{17});
				}
			}
			template <class R>
			void Sub(R d, R n, R m)
			{
				Check();
				cg_->SUB(d, n, m);
			}
			template <class R, class I>
				requires std::is_integral_v<I>
			void Sub(R d, R n, I v)
			{
				Check();
				const u64 x = static_cast<u64>(v);
				if (oaknut::AddSubImm::is_valid(x))
					cg_->SUB(d, n, x);
				else
				{
					cg_->MOV(oaknut::XReg{17}, x);
					if constexpr (std::is_same_v<R, oaknut::WReg>)
						cg_->SUB(d, n, oaknut::WReg{17});
					else
						cg_->SUB(d, n, oaknut::XReg{17});
				}
			}
			template <class R>
			void And(R d, R n, R m)
			{
				Check();
				cg_->AND(d, n, m);
			}
			void And(oaknut::WReg d, oaknut::WReg n, Operand m)
			{
				Check();
				cg_->AND(d, n, m.reg, m.shift == LSL ? oaknut::LogShift::LSL : oaknut::LogShift::LSR, m.amount);
			}
			template <class I>
				requires std::is_integral_v<I>
			void And(oaknut::WReg d, oaknut::WReg n, I v)
			{
				Check();
				u32 bits = static_cast<u32>(v);
				if (oaknut::detail::encode_bit_imm(bits))
					cg_->AND(d, n, bits);
				else
				{
					cg_->MOV(oaknut::WReg{17}, bits);
					cg_->AND(d, n, oaknut::WReg{17});
				}
			}
			template <class R>
			void Orr(R d, R n, R m)
			{
				Check();
				cg_->ORR(d, n, m);
			}
			void Orr(oaknut::WReg d, oaknut::WReg n, Operand m)
			{
				Check();
				cg_->ORR(d, n, m.reg, m.shift == LSL ? oaknut::LogShift::LSL : oaknut::LogShift::LSR, m.amount);
			}
			template <class I>
				requires std::is_integral_v<I>
			void Orr(oaknut::WReg d, oaknut::WReg n, I v)
			{
				Check();
				u32 bits = static_cast<u32>(v);
				if (oaknut::detail::encode_bit_imm(bits))
					cg_->ORR(d, n, bits);
				else
				{
					cg_->MOV(oaknut::WReg{17}, bits);
					cg_->ORR(d, n, oaknut::WReg{17});
				}
			}
			template <class R>
			void Eor(R d, R n, R m)
			{
				Check();
				cg_->EOR(d, n, m);
			}
			void Eor(oaknut::WReg d, oaknut::WReg n, Operand m)
			{
				Check();
				cg_->EOR(d, n, m.reg, m.shift == LSL ? oaknut::LogShift::LSL : oaknut::LogShift::LSR, m.amount);
			}
			template <class I>
				requires std::is_integral_v<I>
			void Eor(oaknut::WReg d, oaknut::WReg n, I v)
			{
				Check();
				u32 bits = static_cast<u32>(v);
				if (oaknut::detail::encode_bit_imm(bits))
					cg_->EOR(d, n, bits);
				else
				{
					cg_->MOV(oaknut::WReg{17}, bits);
					cg_->EOR(d, n, oaknut::WReg{17});
				}
			}
			template <class R>
			void Bic(R d, R n, R m)
			{
				Check();
				cg_->BIC(d, n, m);
			}
			void Bic(oaknut::WReg d, oaknut::WReg n, Operand m)
			{
				Check();
				cg_->BIC(d, n, m.reg, m.shift == LSL ? oaknut::LogShift::LSL : oaknut::LogShift::LSR, m.amount);
			}
			template <class I>
				requires std::is_integral_v<I>
			void Bic(oaknut::WReg d, oaknut::WReg n, I v)
			{
				Check();
				u32 bits = ~static_cast<u32>(v);
				if (oaknut::detail::encode_bit_imm(bits))
					cg_->AND(d, n, bits);
				else
				{
					cg_->MOV(oaknut::WReg{17}, bits);
					cg_->AND(d, n, oaknut::WReg{17});
				}
			}
			template <class R>
			void Bics(R d, R n, R m)
			{
				Check();
				cg_->BICS(d, n, m);
			}
			void Bics(oaknut::WReg d, oaknut::WReg n, Operand m)
			{
				Check();
				cg_->BICS(d, n, m.reg, m.shift == LSL ? oaknut::LogShift::LSL : oaknut::LogShift::LSR, m.amount);
			}
			template <class I>
				requires std::is_integral_v<I>
			void Bics(oaknut::WReg d, oaknut::WReg n, I v)
			{
				Check();
				u32 bits = ~static_cast<u32>(v);
				if (oaknut::detail::encode_bit_imm(bits))
					cg_->ANDS(d, n, bits);
				else
				{
					cg_->MOV(oaknut::WReg{17}, bits);
					cg_->ANDS(d, n, oaknut::WReg{17});
				}
			}
			template <class R>
			void Cmp(R n, R m)
			{
				Check();
				cg_->CMP(n, m);
			}
			template <class I>
				requires std::is_integral_v<I>
			void Cmp(oaknut::WReg n, I v)
			{
				Check();
				u32 x = static_cast<u32>(v);
				if (oaknut::AddSubImm::is_valid(x))
					cg_->CMP(n, x);
				else
				{
					cg_->MOV(oaknut::WReg{17}, x);
					cg_->CMP(n, oaknut::WReg{17});
				}
			}
			template <class R>
			void Cmn(R n, R m)
			{
				Check();
				cg_->CMN(n, m);
			}
			template <class I>
				requires std::is_integral_v<I>
			void Cmn(oaknut::WReg n, I v)
			{
				Check();
				u32 x = static_cast<u32>(v);
				if (oaknut::AddSubImm::is_valid(x))
					cg_->CMN(n, x);
				else
				{
					cg_->MOV(oaknut::WReg{17}, x);
					cg_->CMN(n, oaknut::WReg{17});
				}
			}
			void Cmn(oaknut::WReg n, Operand m)
			{
				Check();
				cg_->CMN(n, m.reg, m.shift == LSL ? oaknut::AddSubShift::LSL : oaknut::AddSubShift::LSR, m.amount);
			}
			void Fmul(oaknut::VReg_4S d, oaknut::VReg_4S n, oaknut::SReg m, int i)
			{
				Check();
				cg_->FMUL(d, n, oaknut::SElem{m.index(), static_cast<unsigned>(i)});
			}
			void Tst(oaknut::WReg n, u32 v)
			{
				Check();
				if (oaknut::detail::encode_bit_imm(v))
					cg_->TST(n, v);
				else
				{
					cg_->MOV(oaknut::WReg{17}, v);
					cg_->TST(n, oaknut::WReg{17});
				}
			}
			void Movi(oaknut::VReg_4S d, u32 v, Shift = LSL, int n = 0)
			{
				Check();
				cg_->MOVI(d, v, oaknut::LslSymbol::LSL, n);
			}
			void Movi(oaknut::VReg_2D d, u64 v)
			{
				Check();
				if (v)
					throw std::runtime_error("MOVI 2D nonzero unsupported");
				cg_->MOVI(d, oaknut::RepImm{0});
			}
			void Dup(oaknut::VReg_4S d, oaknut::VReg_4S n, int i)
			{
				Check();
				cg_->DUP(d, oaknut::SElem{n.index(), static_cast<unsigned>(i)});
			}
			void Dup(oaknut::SReg d, oaknut::VReg_4S n, int i)
			{
				Check();
				cg_->DUP(d, oaknut::SElem{n.index(), static_cast<unsigned>(i)});
			}
			void Dup(oaknut::DReg d, oaknut::VReg_2D n, int i)
			{
				Check();
				cg_->DUP(d, oaknut::DElem{n.index(), static_cast<unsigned>(i)});
			}
			void Ins(oaknut::VReg_4S d, int i, oaknut::WReg n)
			{
				Check();
				cg_->INS(oaknut::SElem{d.index(), static_cast<unsigned>(i)}, n);
			}
			void Ins(oaknut::VReg_4S d, int i, oaknut::VReg_4S n, int j)
			{
				Check();
				cg_->INS(oaknut::SElem{d.index(), static_cast<unsigned>(i)}, oaknut::SElem{n.index(), static_cast<unsigned>(j)});
			}
			void Ins(oaknut::VReg_2D d, int i, oaknut::VReg_2D n, int j)
			{
				Check();
				cg_->INS(oaknut::DElem{d.index(), static_cast<unsigned>(i)}, oaknut::DElem{n.index(), static_cast<unsigned>(j)});
			}
			void Umov(oaknut::WReg d, oaknut::VReg_4S n, int i)
			{
				Check();
				cg_->UMOV(d, oaknut::SElem{n.index(), static_cast<unsigned>(i)});
			}
			void Umov(oaknut::WReg d, oaknut::VReg_8H n, int i)
			{
				Check();
				cg_->UMOV(d, oaknut::HElem{n.index(), static_cast<unsigned>(i)});
			}
			void St1(oaknut::VReg_4S d, int i, MemOperand m)
			{
				Check();
				if (m.offset || m.index >= 0)
					throw std::runtime_error("ST1 address");
				cg_->ST1(oaknut::List<oaknut::SElem, 1>{oaknut::SElem{d.index(), static_cast<unsigned>(i)}}, m.base);
			}
			void St1(oaknut::VReg_2D d, int i, MemOperand m)
			{
				Check();
				if (m.offset || m.index >= 0)
					throw std::runtime_error("ST1 address");
				cg_->ST1(oaknut::List<oaknut::DElem, 1>{oaknut::DElem{d.index(), static_cast<unsigned>(i)}}, m.base);
			}
			template <class R>
			void Ldr(R r, MemOperand m)
			{
				Check();
				if (m.index >= 0)
					cg_->LDR(r, m.base, oaknut::XReg{m.index});
				else if (m.offset >= 0 && m.offset < 4096 && m.offset % (r.bitsize() / 8) == 0)
					cg_->LDR(r, m.base, m.offset);
				else
				{
					cg_->MOV(oaknut::XReg{17}, static_cast<u64>(m.offset));
					cg_->ADD(oaknut::XReg{17}, m.base, oaknut::XReg{17});
					cg_->LDR(r, oaknut::XReg{17});
				}
			}
			template <class R>
			void Str(R r, MemOperand m)
			{
				Check();
				if (m.index >= 0)
					cg_->STR(r, m.base, oaknut::XReg{m.index});
				else if (m.offset >= 0 && m.offset < 4096 && m.offset % (r.bitsize() / 8) == 0)
					cg_->STR(r, m.base, m.offset);
				else
				{
					cg_->MOV(oaknut::XReg{17}, static_cast<u64>(m.offset));
					cg_->ADD(oaknut::XReg{17}, m.base, oaknut::XReg{17});
					cg_->STR(r, oaknut::XReg{17});
				}
			}
			template <class R>
			void Ldrh(R r, MemOperand m)
			{
				Check();
				if (m.index >= 0)
					cg_->LDRH(r, m.base, oaknut::XReg{m.index});
				else if (m.offset >= 0 && m.offset < 4096 && m.offset % (r.bitsize() / 8) == 0)
					cg_->LDRH(r, m.base, m.offset);
				else
				{
					cg_->MOV(oaknut::XReg{17}, static_cast<u64>(m.offset));
					cg_->ADD(oaknut::XReg{17}, m.base, oaknut::XReg{17});
					cg_->LDRH(r, oaknut::XReg{17});
				}
			}
			template <class R>
			void Strh(R r, MemOperand m)
			{
				Check();
				if (m.index >= 0)
					cg_->STRH(r, m.base, oaknut::XReg{m.index});
				else if (m.offset >= 0 && m.offset < 4096 && m.offset % (r.bitsize() / 8) == 0)
					cg_->STRH(r, m.base, m.offset);
				else
				{
					cg_->MOV(oaknut::XReg{17}, static_cast<u64>(m.offset));
					cg_->ADD(oaknut::XReg{17}, m.base, oaknut::XReg{17});
					cg_->STRH(r, oaknut::XReg{17});
				}
			}
			template <class R>
			void Ldrsw(R r, MemOperand m)
			{
				Check();
				if (m.index >= 0)
					cg_->LDRSW(r, m.base, oaknut::XReg{m.index});
				else if (m.offset >= 0 && m.offset < 4096 && m.offset % (r.bitsize() / 8) == 0)
					cg_->LDRSW(r, m.base, m.offset);
				else
				{
					cg_->MOV(oaknut::XReg{17}, static_cast<u64>(m.offset));
					cg_->ADD(oaknut::XReg{17}, m.base, oaknut::XReg{17});
					cg_->LDRSW(r, oaknut::XReg{17});
				}
			}
			template <class R>
			void Ldp(R a, R b, MemOperand m)
			{
				Check();
				if (m.index >= 0)
					throw std::runtime_error("LDP indexed");
				cg_->LDP(a, b, m.base, m.offset);
			}
		};
	} // namespace a64
	extern thread_local a64::MacroAssembler* armAsm;
	struct ArmAddressRecorder
	{
		enum class BranchForm
		{
			Rel26
		};
		virtual ~ArmAddressRecorder() = default;
		virtual void OnMovRun(u8*, u32, const void*) = 0;
		virtual void OnBranch(u8*, const void*, BranchForm) = 0;
	};
	extern thread_local ArmAddressRecorder* armAddressRecorder;
	class ScopedAssembler
	{
		a64::MacroAssembler facade_;
		a64::MacroAssembler* saved_;

	public:
		explicit ScopedAssembler(oaknut::CodeGenerator& cg, size_t capacity)
			: facade_(cg, capacity)
			, saved_(armAsm)
		{
			armAsm = &facade_;
		}
		~ScopedAssembler() { armAsm = saved_; }
	};
	inline u8* armGetCurrentCodePointer() { return armAsm->Cursor(); }
	void armMoveAddressToReg(oaknut::XReg r, const void* p);
	void armEmitCall(const void* p);
	void armEmitJmp(const void* p);
} // namespace vuaot
