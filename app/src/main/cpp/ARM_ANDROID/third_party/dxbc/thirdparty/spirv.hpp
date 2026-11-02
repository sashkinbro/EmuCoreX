#pragma once

// lsfg's trans.cpp includes the DXBC->SPIR-V compiler's SPIR-V enums as
// <thirdparty/spirv.hpp>; the vendored compiler ships the Khronos generated header
// under include/spirv/unified1, so this is the usual forwarding shim.
#include <spirv/unified1/spirv.hpp>
