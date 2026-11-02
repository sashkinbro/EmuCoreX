#pragma once

// Minimal stand-in for DXVK's dxvk_shader.h.
//
// DXVK's DXBC compiler returns a device-level shader object, but the only consumer here
// (lsfg's trans.cpp) reads the compiled SPIR-V words straight out of it: it iterates the
// instructions, patches binding operands and copies the bytes. So the shim makes DxvkShader
// the SPIR-V buffer itself, keeping the create-info struct the compiler fills in.
#include <utility>

#include <vulkan/vulkan.h>

#include "../spirv/spirv_code_buffer.h"

namespace dxvk {

  constexpr uint32_t MaxNumXfbBuffers = 4;

  struct DxvkBindingInfo {
    VkDescriptorType  descriptorType  = VK_DESCRIPTOR_TYPE_MAX_ENUM;
    VkAccessFlags     access          = 0;
    uint32_t          resourceBinding = 0;
    uint32_t          uboSet          = 0;
    VkImageViewType   viewType        = VK_IMAGE_VIEW_TYPE_MAX_ENUM;
  };

  struct DxvkShaderCreateInfo {
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
    uint32_t bindingCount = 0;
    const DxvkBindingInfo* bindings = nullptr;
    uint32_t inputMask = 0;
    uint32_t outputMask = 0;
    uint32_t flatShadingInputs = 0;
    VkShaderStageFlags pushConstStages = 0;
    uint32_t pushConstSize = 0;
    uint32_t uniformSize = 0;
    const char* uniformData = nullptr;
    int32_t xfbRasterizedStream = 0;
    uint32_t patchVertexCount = 0;
    uint32_t xfbStrides[MaxNumXfbBuffers] = { };
    VkPrimitiveTopology outputTopology = VK_PRIMITIVE_TOPOLOGY_MAX_ENUM;
  };

  /**
   * \brief Compiled shader
   *
   * Derives from SpirvCodeBuffer so callers get data(), size() and instruction iteration
   * directly from the returned object.
   */
  class DxvkShader : public SpirvCodeBuffer {

  public:

    DxvkShader(const DxvkShaderCreateInfo& info, SpirvCodeBuffer&& code)
      : SpirvCodeBuffer(std::move(code)), m_stage(info.stage) { }

    VkShaderStageFlagBits getStage() const { return m_stage; }

  private:

    VkShaderStageFlagBits m_stage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;

  };

}