// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Exercises the same final capability resolver used by CheckFeatures. Vendor
// stand-ins never reach Vulkan device creation or unsupported GPU commands.
void GSDeviceVK::TestDepthSamplingPolicy(void (*check)(bool, const char*))
{
    const auto features = m_features;
    const auto facts = m_road_facts;
    bool adreno_safe = true, other_devices = true, barriers_off = true, isolated = true;
    u32 cases = 0;
    for (const u32 vendor : {0x13b5u, 0x5143u, 0x1010u, 0x14e4u, 0x106bu,
        0x144du, 0x10deu, 0x1002u, 0x8086u, 0u})
    for (const auto driver : {GSVulkanRoad::Driver::Unknown, GSVulkanRoad::Driver::Arm,
        GSVulkanRoad::Driver::PanVK, GSVulkanRoad::Driver::Qualcomm, GSVulkanRoad::Driver::Turnip,
        GSVulkanRoad::Driver::Imagination, GSVulkanRoad::Driver::MesaPowerVR,
        GSVulkanRoad::Driver::Honeykrisp, GSVulkanRoad::Driver::Angle})
    for (bool barriers : {false, true})
    {
        m_features = features;
        m_features.texture_barrier = barriers;
        // Poison the output so both transitions are exercised, regardless of
        // the native device's actual family and capabilities.
        m_features.test_and_sample_depth = !barriers;
        m_road_facts = GSVulkanRoad::ResolveFacts({vendor, driver, "", "", "", "", false}, false, 0);
        const auto before = m_features;
        ResolveDepthSamplingFeatures();
        const bool enabled = m_features.test_and_sample_depth;
        if (vendor == 0x5143u) adreno_safe &= !enabled;
        else other_devices &= enabled == barriers;
        if (!barriers) barriers_off &= !enabled;
        m_features.test_and_sample_depth = before.test_and_sample_depth;
        isolated &= std::memcmp(&m_features, &before, sizeof(before)) == 0;
        ++cases;
    }
    m_features = features;
    m_road_facts = facts;
    check(adreno_safe, "GPU depth sampling excludes every Adreno driver even when color barriers are enabled");
    check(other_devices, "GPU depth sampling retains existing non-Adreno barrier capabilities across vendors");
    check(barriers_off, "GPU depth sampling never enables in-place depth reads with barriers disabled");
    check(isolated, "GPU depth sampling selection changes no other device or depth-feedback feature");
    std::printf("DEPTH_SAMPLING_POLICY cases=%u\n", cases);
}
