// SPDX-License-Identifier: GPL-3.0+
void GSRestartTests()
{
    cpuinfo_initialize();
    Check(!g_gs_device, "GS restart oracle starts without a VM device");
    if (g_gs_device) return;
    const auto source = FileSystem::ReadFileToString("resources/shaders/vulkan/tfx.glsl");
    Check(source && !source->empty(), "GS restart oracle reads its canonical shader source");
    if (!source || source->empty()) return;
    const auto saved = GSConfig;
    const auto saved_cache = EmuFolders::Cache;
    GSConfig = Pcsx2Config::GSOptions();
    GSConfig.Renderer = GSRendererType::VK;
    EmuFolders::Cache = "artifacts/gs-restart-cache";
    FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(), true);
    g_gs_device = std::make_unique<GSDeviceVK>();
    auto& device = *static_cast<GSDeviceVK*>(g_gs_device.get());
    const bool created = device.CreateTFXPreparationDevice(*source);
    Check(created, "GS restart oracle creates an isolated real Vulkan device");
    if (created)
    {
        std::printf("GS_RESTART_DEVICE name=%s vendor=%x driver=%s\n", device.GetDeviceProperties().deviceName,
            device.GetDeviceProperties().vendorID, device.GetDriverInfo().c_str());
        device.TestRenderPassRestarts(Check);
        Check(device.TestTFXPreparationResources(), "GS restart fixture releases graphics and frame resources before teardown");
    }
    device.DestroyTFXPreparationDevice();
    g_gs_device.reset();
    GSConfig = saved;
    EmuFolders::Cache = saved_cache;
}
