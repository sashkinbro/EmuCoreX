// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Native-only first gate; include/call before any synthetic feature models.
// Capability agreement is not a GPU pixel or pipeline-transition proof.
void GSDeviceVK::TestBlendCarryCapability(void (*check)(bool,const char*))
{
    VkPhysicalDeviceFeatures physical{};
    vkGetPhysicalDeviceFeatures(m_physical_device,&physical);
    const bool available=physical.dualSrcBlend!=VK_FALSE;
    const bool enabled=m_device_features.dualSrcBlend!=VK_FALSE;
    const bool effective=m_features.dual_source_blend;
    std::printf("GS_BLEND_DEVICE physicalDual=%u enabledDual=%u effectiveDual=%u vendor=%04x device=%04x\n",
        available,enabled,effective,m_device_properties.vendorID,m_device_properties.deviceID);
    // Current CheckFeatures assigns this feature exactly from the enabled
    // device bit. Pin that contract so an uninitialized/default-zero policy
    // cannot masquerade as a measured unsupported device. A future deliberate
    // policy downgrade must update this assertion with its explicit reason.
    check(effective==enabled&&(!enabled||available),
        "GS blend carry effective dual-source capability agrees with the real selected physical device");
    if(!effective)std::fprintf(stderr,
        "UNAVAILABLE GS carried dual-source GPU pipeline/pixel proof: effective hardware support absent; canonical fallback proof remains required\n");
}
