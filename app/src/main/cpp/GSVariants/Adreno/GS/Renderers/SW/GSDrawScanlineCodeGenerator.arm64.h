// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include "GS/Renderers/Common/GSFunctionMap.h"
#include "GS/Renderers/SW/GSScanlineEnvironment.h"

#include "arm64/OaknutHelpers-arm64.h"

class GSDrawScanlineCodeGenerator
{
public:
	GSDrawScanlineCodeGenerator(u64 key, void* code, size_t maxsize);
	void Generate();

	size_t GetSize() const { return m_size; }
	const u8* GetCode() const { return m_code; }

private:
	void Init();
	void Step();
	void TestZ(oak::QReg temp1, oak::QReg temp2);
	void SampleTexture();
	void SampleTexture_TexelReadHelper(int mip_offset);
	void Wrap(oak::QReg uv0);
	void Wrap(oak::QReg uv0, oak::QReg uv1);
	void SampleTextureLOD();
	void WrapLOD(oak::QReg uv, oak::QReg tmp, oak::QReg tmp2, oak::QReg min, oak::QReg max);
	void WrapLOD(oak::QReg uv0, oak::QReg uv1, oak::QReg tmp, oak::QReg tmp2, oak::QReg min, oak::QReg max);
	void AlphaTFX();
	void ReadMask();
	void TestAlpha();
	void ColorTFX();
	void Fog();
	void ReadFrame();
	void TestDestAlpha();
	void WriteMask();
	void WriteZBuf();
	void AlphaBlend();
	void WriteFrame();
	void ReadPixel(oak::QReg dst, oak::WReg addr);
	void WritePixel(oak::QReg src, oak::WReg addr, oak::WReg mask, bool high, bool fast, int psm, int fz);
	void WritePixel(oak::QReg src, oak::WReg addr, u8 i, int psm);

	void ReadTexel1(oak::QReg dst, oak::QReg src, oak::QReg tmp1, int mip_offset);
	void ReadTexel4(
		oak::QReg d0, oak::QReg d1,
		oak::QReg d2s0, oak::QReg d3s1,
		oak::QReg s2, oak::QReg s3,
		int mip_offset);
	void ReadTexelImplLoadTexLOD(oak::XReg addr, int lod, int mip_offset);
	void ReadTexelImpl(
		oak::QReg d0, oak::QReg d1,
		oak::QReg d2s0, oak::QReg d3s1,
		oak::QReg s2, oak::QReg s3,
		int pixels, int mip_offset);
	void ReadTexelImpl(oak::QReg dst, oak::QReg addr, u8 i, oak::XReg baseRegister, bool preserveDst);

	void modulate16(oak::QReg d, oak::QReg a, oak::QReg f, u8 shift);
	void modulate16(oak::QReg a, oak::QReg f, u8 shift);
	void lerp16(oak::QReg a, oak::QReg b, oak::QReg f, u8 shift);
	void lerp16_4(oak::QReg a, oak::QReg b, oak::QReg f);
	void mix16(oak::QReg a, oak::QReg b, oak::QReg temp);
	void clamp16(oak::QReg a, oak::QReg temp);
	void alltrue(oak::QReg test, oak::QReg temp);
	void blend8(oak::QReg a, oak::QReg b, oak::QReg mask, oak::QReg temp);
	void blend8r(oak::QReg b, oak::QReg a, oak::QReg mask, oak::QReg temp);
	void split16_2x8(oak::QReg l, oak::QReg h, oak::QReg src);

	u8* m_code = nullptr;
	size_t m_capacity = 0;
	size_t m_size = 0;

	GSScanlineSelector m_sel;

	oak::Label m_step_label;
};
