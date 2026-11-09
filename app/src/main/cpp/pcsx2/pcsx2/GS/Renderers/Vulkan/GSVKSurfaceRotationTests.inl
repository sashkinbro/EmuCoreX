// SPDX-License-Identifier: GPL-3.0+
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
#include "common/ScopedGuard.h"
extern "C" void EmuCoreXOracleVKSurfaceRotation(void(*check)(bool,const char*))
{
#ifdef GS_VK_PRESENT_ROTATION_PARITY
    const auto config=GSConfig;ScopedGuard restore=[&](){GSConfig=config;};
    VkSurfaceCapabilitiesKHR caps{};WindowInfo wi{};GSConfig.DisableSwapChainPreRotation=false;
    caps.supportedTransforms=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR|VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR|VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR|VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    caps.currentTransform=VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;wi.surface_rotation=3;
    check(WantedSurfaceRotation(caps,wi)==DisplayRotation::Rot90,"GS actual swapchain surface-reported turn outranks frontend hint");
    GSConfig.DisableSwapChainPreRotation=true;check(WantedSurfaceRotation(caps,wi)==DisplayRotation::Rot0,"GS actual swapchain opt-out leaves rotation with compositor");GSConfig.DisableSwapChainPreRotation=false;
    caps.currentTransform=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;check(WantedSurfaceRotation(caps,wi)==DisplayRotation::Rot270,"GS actual swapchain fallback uses supported frontend hint");
    caps.supportedTransforms=VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;check(WantedSurfaceRotation(caps,wi)==DisplayRotation::Rot0,"GS actual swapchain refuses unsupported hinted transforms");
    for(auto turn:{DisplayRotation::Rot0,DisplayRotation::Rot90,DisplayRotation::Rot180,DisplayRotation::Rot270})
        check(RotationFromSurfaceTransform(SurfaceTransformFromRotation(turn))==turn,"GS actual swapchain transform mappings preserve all four rotations");
    check(RotationFromSurfaceTransform(VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR)==DisplayRotation::Rot0,"GS actual swapchain mirrored transforms are not silently treated as rotations");
    check(RotationSwapsAxes(DisplayRotation::Rot90)&&RotationSwapsAxes(DisplayRotation::Rot270)&&!RotationSwapsAxes(DisplayRotation::Rot0)&&!RotationSwapsAxes(DisplayRotation::Rot180),"GS actual swapchain image extent swaps axes only for quarter turns");
    check(ConstrainPowerVRSwapchainWidth(0x1010,0x00582557,1024,1080)==1056,"GS actual old-PowerVR swapchain width applies exact driver-version32-pixel workaround");
    check(ConstrainPowerVRSwapchainWidth(0x1010,0x00582558,1024,1080)==1080&&ConstrainPowerVRSwapchainWidth(0x5143,0,1024,1080)==1080,"GS actual PowerVR width workaround leaves fixed drivers and other vendors unchanged");
    check(ConstrainPowerVRSwapchainWidth(0x1010,0,1070,1080)==1080,"GS actual PowerVR width workaround never violates surface minimum extent");
#else
    check(false,"GS actual Vulkan swapchain rotation/extent policy consumers are available");
#endif
}
