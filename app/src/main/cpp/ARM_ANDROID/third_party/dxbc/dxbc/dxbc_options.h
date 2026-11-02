#pragma once

// Trimmed for the vendored Android build: DXVK's original header pulls dxvk_device.h for a
// device-driven constructor that the DXBC->SPIR-V compiler never uses here.
#include <vulkan/vulkan.h>

#include "../util/util_flags.h"

namespace dxvk {

  enum class DxbcFloatControlFlag : uint32_t {
    DenormFlushToZero32,
    DenormPreserve64,
    PreserveNan32,
    PreserveNan64,
  };

  using DxbcFloatControlFlags = Flags<DxbcFloatControlFlag>;

  struct DxbcOptions {
    DxbcOptions();

    bool useDepthClipWorkaround = false;
    bool supportsTypedUavLoadR32 = false;
    bool supportsRawAccessChains = false;
    bool zeroInitWorkgroupMemory = false;
    bool invariantPosition = false;
    bool forceVolatileTgsmAccess = false;
    bool disableMsaa = false;
    bool forceSampleRateShading = false;
    bool enableSampleShadingInterlock = false;
    DxbcFloatControlFlags floatControl;
    VkDeviceSize minSsboAlignment = 0;
  };

}