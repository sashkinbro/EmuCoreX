// SPDX-License-Identifier: GPL-3.0+
void GSDateStencilTests()
{
    cpuinfo_initialize();
    Check(!g_gs_device&&!g_gs_renderer&&!Vulkan::IsVulkanLibraryLoaded(),"GS DATE GPU oracle starts without a VM or Vulkan owner");
    if(g_gs_device||g_gs_renderer||Vulkan::IsVulkanLibraryLoaded())return;
    const auto saved=GSConfig;const auto resources=EmuFolders::Resources,cache=EmuFolders::Cache;
    ScopedGuard restore([&](){
        if(g_gs_device){static_cast<GSDeviceVK*>(g_gs_device.get())->DestroyTFXPreparationDevice();g_gs_device.reset();}
        GSConfig=saved;EmuFolders::Resources=resources;EmuFolders::Cache=cache;
    });
    GSConfig=Pcsx2Config::GSOptions();GSConfig.Renderer=GSRendererType::VK;
    GSConfig.UpscaleMultiplier=1;GSConfig.HWSpinCPUForReadbacks=false;GSConfig.HWSpinGPUForReadbacks=false;
    EmuFolders::Resources="resources";EmuFolders::Cache="artifacts/gs-date-stencil-cache";
    FileSystem::EnsureDirectoryExists(EmuFolders::Cache.c_str(),true);
    const auto source=FileSystem::ReadFileToString("resources/shaders/vulkan/tfx.glsl");
    Check(source&&!source->empty(),"GS DATE GPU oracle reads canonical TFX source");if(!source||source->empty())return;
    g_gs_device=std::make_unique<GSDeviceVK>();auto& device=*static_cast<GSDeviceVK*>(g_gs_device.get());
    const bool created=device.CreateTFXPreparationDevice(*source);
    Check(created,"GS DATE GPU oracle creates one isolated physical Vulkan device without a surface or preparation workers");
    if(!created)return;
    std::printf("GS_DATE_DEVICE %s\n",device.GetDriverInfo().c_str());device.TestDateStencilPixels(Check);
    Check(device.GetAllocator()==nullptr,"GS DATE GPU oracle releases extra draw resources before device teardown");
}
