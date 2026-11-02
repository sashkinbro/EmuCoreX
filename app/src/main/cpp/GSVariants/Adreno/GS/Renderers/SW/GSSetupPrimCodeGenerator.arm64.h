// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0

#pragma once

#include "GS/Renderers/Common/GSFunctionMap.h"
#include "GS/Renderers/SW/GSScanlineEnvironment.h"

#include "arm64/OaknutHelpers-arm64.h"

class GSSetupPrimCodeGenerator
{
public:
	GSSetupPrimCodeGenerator(u64 key, void* code, size_t maxsize);
	void Generate();

	size_t GetSize() const { return m_size; }
	const u8* GetCode() const { return m_code; }

private:
	void Depth();
	void Texture();
	void Color();

	u8* m_code = nullptr;
	size_t m_capacity = 0;
	size_t m_size = 0;

	GSScanlineSelector m_sel;

	struct
	{
		u32 z : 1, f : 1, t : 1, c : 1;
	} m_en;
};
