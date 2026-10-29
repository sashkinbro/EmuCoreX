// SPDX-License-Identifier: GPL-3.0+
// Include beside the existing GS oracle includes; route gs-uploads here.
extern "C" void EmuCoreXOracleTestTargetScratchUpload(void (*check)(bool, const char*));
void GSUploadTests()
{
    cpuinfo_initialize();
    Check(!g_gs_device, "GS upload oracle starts with no VM device");
    if (g_gs_device) return;
    EmuCoreXOracleTestTargetScratchUpload(Check);
    const auto source = FileSystem::ReadFileToString("resources/shaders/vulkan/tfx.glsl");
    Check(source && !source->empty(), "GS upload oracle reads the canonical Vulkan shader source");
    if (!source || source->empty()) return;
    const auto saved = GSConfig;
    const auto savedCache = EmuFolders::Cache;
    const auto savedResources = EmuFolders::Resources;
    GSConfig = Pcsx2Config::GSOptions();
    GSConfig.Renderer = GSRendererType::VK;
    GSConfig.UseDebugDevice = false;
    EmuFolders::Cache = "artifacts/gs-upload-cache";
    EmuFolders::Resources = "resources";
    FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(), true);
    g_gs_device = std::make_unique<GSDeviceVK>();
    auto& device = *static_cast<GSDeviceVK*>(g_gs_device.get());
    const bool created = device.CreateTFXPreparationDevice(*source);
    Check(created, "GS upload oracle creates an isolated real Vulkan device");
    if (created)
    {
        device.TestTextureUploadRing(Check);
        Check(device.TestTFXPreparationResources(), "GS upload oracle releases every buffer and fence before preparation teardown");
    }
    device.DestroyTFXPreparationDevice();
    g_gs_device.reset();
    GSConfig = saved;
    EmuFolders::Cache = savedCache;
    EmuFolders::Resources = savedResources;
}
