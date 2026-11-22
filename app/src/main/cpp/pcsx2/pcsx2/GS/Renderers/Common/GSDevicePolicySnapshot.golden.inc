R"GOLDEN(
# GS device policy snapshot v1. Regenerate: see GSDevicePolicySnapshotTests.inl
[global]
vk.swapchain_present_modes(maintenance=1 malisx2=0)=1
vk.swapchain_present_modes(maintenance=1 malisx2=1)=0
vk.swapchain_present_modes(maintenance=0 malisx2=0)=0
gl.per_primitive_fb_copy(barrier=0 tiler=0)=1
gl.per_primitive_fb_copy(barrier=0 tiler=1)=0
gl.per_primitive_fb_copy(barrier=1 tiler=0)=0
gl.per_primitive_fb_copy(barrier=1 tiler=1)=0
[adreno840-qcom-oneplus-CPH2747]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=8 name='Adreno (TM) 840' driver_name='Qualcomm Technologies Inc. Adreno Vulkan Driver' info='Driver Build: 1c927b3c50, I90301f9f20, 1786608725|Date: 08/13/26|Compiler Version: E031.50.19.34|Driver Branch: ' version=0x8034a035 platform='QTI sun qcom OnePlus CPH2747 arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=8
vk.platform_rules [broken_colormask_with_depth]
vk.inline provoking_vertex_disabled=1 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Qualcomm' renderer='Adreno (TM) 840' version='OpenGL ES 3.2 V@0842.52 (GIT@1c927b3c50, I90301f9f20, 1786608725) (Date:08/13/26)'
gl.runtime_profile=Adreno adreno_generation=8 qualcomm_proprietary=1 single_rgba8_ext_fetch=1 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,adreno8xx_proprietary]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,adreno8xx_proprietary]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,adreno8xx_proprietary]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,adreno8xx_proprietary]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,adreno8xx_proprietary]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,adreno8xx_proprietary]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[device-log roaa=1 layout=0 dyn=0 cmd=0 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,adreno8xx_proprietary]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
[adreno750-qcom-lenovo-TB710FU]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=8 name='Adreno (TM) 750' driver_name='Qualcomm Technologies Inc. Adreno Vulkan Driver' info='Driver Build: 346fca22e2, I26e0f54d9e, 1748952422|Date: 06/03/25|Compiler Version: E031.45.02.25|Driver Branch: ' version=0x802fa025 platform='QTI pineapple qcom Lenovo TB710FU arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=7
vk.platform_rules [broken_colormask_with_depth]
vk.inline provoking_vertex_disabled=1 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Qualcomm' renderer='Adreno (TM) 750' version='OpenGL ES 3.2 V@0762.37 (GIT@346fca22e2, I26e0f54d9e, 1748952422) (Date:06/03/25)'
gl.runtime_profile=Adreno adreno_generation=7 qualcomm_proprietary=1 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=750 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=750 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=750 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Qualcomm model=750 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Qualcomm model=750 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Qualcomm model=750 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[device-log roaa=0 layout=0 dyn=0 cmd=0 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=750 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
[mali-g57-r40p0-arm-anbernic-RG556]
vk.identity vendor=0x13b5 device_id=0x00000000 driver_props=1 driver_id=9 name='Mali-G57' driver_name='ARM proprietary' info='v1.r40p0-01eac0.b0251c048237dcd59e6be15fba11a31a' version=0x0a000000 platform='UNISOC ums9620_2h10 ums9620 Anbernic RG556 arm64-v8a' mediatek=0
vk.runtime_profile=Mobile mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules [broken_mad_deinterlace,prefer_coherent_readback,no_ps2_z_quantization]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='ARM' renderer='Mali-G57' version='OpenGL ES 3.2 v1.r40p0-01eac0.b0251c048237dcd59e6be15fba11a31a'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=None flags=[texture_barrier]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
caps[device-log roaa=1 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read]
[adreno730-turnip-axfl2]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=18 name='Turnip Adreno (TM) 730' driver_name='turnip Mesa driver' info='Mesa 26.2.0-devel (git-axfl2-1a2b3c4d5e)' version=0x06802000 platform='QTI taro qcom arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=7
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=1 swapchain_present_modes=1
gl.identity vendor='freedreno' renderer='Turnip Adreno (TM) 730' version='OpenGL ES 3.2 Mesa 26.2.0-devel'
gl.runtime_profile=Adreno adreno_generation=7 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=2 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop,order_on_request,prefer_declared_barrier]
  sel road=InPassOrdered spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,orders_overlapping_prims,loop_declared,selected_by_driver_fact,dynamic_loop,raster_order_on_overlap,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=2 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop,order_on_request,prefer_declared_barrier]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=2 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop,order_on_request,prefer_declared_barrier]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=2 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop,order_on_request,prefer_declared_barrier]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=2 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop,order_on_request,prefer_declared_barrier]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Turnip model=730 fix_generation=2 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop,order_on_request,prefer_declared_barrier]
  sel road=InPassOrdered spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,orders_overlapping_prims,loop_declared,selected_by_driver_fact,dynamic_loop,raster_order_on_overlap,push_descriptors]
[adreno730-turnip-plain]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=18 name='Turnip Adreno (TM) 730' driver_name='turnip Mesa driver' info='Mesa 25.3.0' version=0x06403000 platform='QTI taro qcom arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=7
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=1 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=1 swapchain_present_modes=1
gl.identity vendor='freedreno' renderer='Turnip Adreno (TM) 730' version='OpenGL ES 3.2 Mesa 25.3.0'
gl.runtime_profile=Adreno adreno_generation=7 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=0 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=0 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=0 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=0 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Turnip model=730 fix_generation=0 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Turnip model=730 fix_generation=0 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
[adreno740-turnip-axfl1]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=18 name='Turnip Adreno (TM) 740' driver_name='turnip Mesa driver' info='Mesa 26.1.0-devel (git-axfl1-0f0e0d0c0b)' version=0x06801000 platform='QTI kalama qcom arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=7
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=1 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=1 swapchain_present_modes=1
gl.identity vendor='freedreno' renderer='Turnip Adreno (TM) 740' version='OpenGL ES 3.2 Mesa 26.1.0-devel'
gl.runtime_profile=Adreno adreno_generation=7 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=740 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=740 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=740 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Turnip model=740 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Turnip model=740 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Turnip model=740 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,prefer_declared_barrier]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
[adreno650-turnip-axfl1]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=18 name='Turnip Adreno (TM) 650' driver_name='turnip Mesa driver' info='Mesa 26.1.0-devel (git-axfl1-9a8b7c6d5e)' version=0x06801000 platform='QTI kona qcom arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=6
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=1 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=1 swapchain_present_modes=1
gl.identity vendor='freedreno' renderer='Turnip Adreno (TM) 650' version='OpenGL ES 3.2 Mesa 26.1.0-devel'
gl.runtime_profile=Adreno adreno_generation=6 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop]
  sel road=InPassOrdered spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,orders_overlapping_prims,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop]
  sel road=InPassOrdered spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,orders_overlapping_prims,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Turnip model=650 fix_generation=1 flags=[adreno,copy_self_read,measured_layout,driver_orders_loop]
  sel road=InPassOrdered spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,force_feedback_loop_layout,orders_overlapping_prims,loop_declared,selected_by_driver_fact,dynamic_loop,push_descriptors]
[adreno650-turnip-plain]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=18 name='Turnip Adreno (TM) 650' driver_name='turnip Mesa driver' info='Mesa 25.3.0' version=0x06403000 platform='QTI kona qcom arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=6
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=1 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=1 swapchain_present_modes=1
gl.identity vendor='freedreno' renderer='Turnip Adreno (TM) 650' version='OpenGL ES 3.2 Mesa 25.3.0'
gl.runtime_profile=Adreno adreno_generation=6 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
[adreno650-turnip-unmapped-driver-id]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=0 name='Turnip Adreno (TM) 650' driver_name='turnip Mesa driver' info='Mesa 25.3.0' version=0x06403000 platform='QTI kona qcom arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=6
vk.platform_rules [broken_colormask_with_depth]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=1 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=1 swapchain_present_modes=1
gl.identity vendor='freedreno' renderer='Turnip Adreno (TM) 650' version='OpenGL ES 3.2 Mesa 25.3.0'
gl.runtime_profile=Adreno adreno_generation=6 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Turnip model=650 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
[adreno610-turnip]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=18 name='Turnip Adreno (TM) 610' driver_name='turnip Mesa driver' info='Mesa 25.1.0' version=0x06401000 platform='QTI bengal qcom arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=6
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=1 prefer_cached_stream_rings=1 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=1 swapchain_present_modes=1
gl.identity vendor='freedreno' renderer='Turnip Adreno (TM) 610' version='OpenGL ES 3.2 Mesa 25.1.0'
gl.runtime_profile=Adreno adreno_generation=6 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=610 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=610 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Turnip model=610 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Turnip model=610 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Turnip model=610 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Turnip model=610 fix_generation=0 flags=[adreno,copy_self_read,measured_layout]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
[adreno840-qcom-no-driver-properties]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=0 driver_id=0 name='Adreno (TM) 840' driver_name='' info='' version=0x8034a035 platform='QTI sun qcom arm64-v8a' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=8
vk.platform_rules [broken_colormask_with_depth]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Qualcomm' renderer='Adreno (TM) 840' version='OpenGL ES 3.2 V@0842.52 (GIT@1c927b3c50)'
gl.runtime_profile=Adreno adreno_generation=8 qualcomm_proprietary=1 single_rgba8_ext_fetch=1 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Qualcomm model=840 fix_generation=0 flags=[adreno,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
[adreno-chipid-only-650]
vk.identity vendor=0x5143 device_id=0x06050000 driver_props=1 driver_id=8 name='Adreno' driver_name='Qualcomm Technologies Inc. Adreno Vulkan Driver' info='' version=0x801f4001 platform='' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules []
vk.inline provoking_vertex_disabled=1 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Qualcomm' renderer='Adreno' version='OpenGL ES 3.2 V@0500.1'
gl.runtime_profile=Adreno adreno_generation=0 qualcomm_proprietary=1 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=650 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=650 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=650 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Qualcomm model=650 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Qualcomm model=650 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Qualcomm model=650 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
[adreno-x1-85-windows-arm]
vk.identity vendor=0x5143 device_id=0x00000000 driver_props=1 driver_id=8 name='Qualcomm(R) Adreno(TM) X1-85 GPU' driver_name='Qualcomm Technologies Inc. Adreno Vulkan Driver' info='' version=0x80320001 platform='' mediatek=0
vk.runtime_profile=Adreno mediatek_hint(platform)=0 adreno_generation=9
vk.platform_rules [broken_colormask_with_depth]
vk.inline provoking_vertex_disabled=1 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=2 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Qualcomm' renderer='Adreno (TM) X1-85' version='OpenGL ES 3.2 V@0800.1'
gl.runtime_profile=Adreno adreno_generation=9 qualcomm_proprietary=1 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=185 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=185 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Qualcomm model=185 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Qualcomm model=185 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Qualcomm model=185 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Qualcomm model=185 fix_generation=0 flags=[adreno,copy_self_read]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
[mali-g57-r44p1-arm]
vk.identity vendor=0x13b5 device_id=0x00000000 driver_props=1 driver_id=9 name='Mali-G57' driver_name='ARM proprietary' info='v1.r44p1-00eac0.0123456789abcdef0123456789abcdef' version=0x0b001000 platform='Samsung exynos arm64-v8a' mediatek=0
vk.runtime_profile=Mobile mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules [broken_mad_deinterlace,no_ps2_z_quantization]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='ARM' renderer='Mali-G57' version='OpenGL ES 3.2 v1.r44p1-00eac0.0123456789abcdef0123456789abcdef'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_layout,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[avoid_layout]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_layout,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[avoid_layout]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_layout,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[avoid_layout]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_layout,copy_self_read,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,avoid_layout]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_layout,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[avoid_layout]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,avoid_layout,copy_self_read,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[avoid_layout]
[mali-g57-malisx2-0.5.1]
vk.identity vendor=0x13b5 device_id=0x00000000 driver_props=1 driver_id=9 name='Mali-G57' driver_name='MaliSX2' info='malisx2-0.5.1-308fe121c7' version=0x0a000000 platform='UNISOC ums9620_2h10 ums9620 arm64-v8a' mediatek=0
vk.runtime_profile=Mobile mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules [broken_mad_deinterlace,prefer_coherent_readback,no_ps2_z_quantization]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=0
gl.identity vendor='ARM' renderer='Mali-G57' version='OpenGL ES 3.2 v1.r40p0-01eac0'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,malisx2]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,malisx2,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,malisx2]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=None flags=[texture_barrier,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,malisx2]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,malisx2]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Arm model=57 fix_generation=0 flags=[mali,malisx2]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier,push_descriptors]
[mali-g720-mediatek-mt6989]
vk.identity vendor=0x13b5 device_id=0x00000000 driver_props=1 driver_id=9 name='Mali-G720-Immortalis MC12' driver_name='ARM proprietary' info='v1.r48p0-00eac0.0123456789abcdef0123456789abcdef' version=0x0c000000 platform='MediaTek mt6989 Dimensity 9300 arm64-v8a' mediatek=1
vk.runtime_profile=Mobile mediatek_hint(platform)=1 adreno_generation=0
vk.platform_rules [prefer_coherent_readback,no_ps2_z_quantization]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='ARM' renderer='Mali-G720-Immortalis MC12' version='OpenGL ES 3.2 v1.r48p0-00eac0'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=720 fix_generation=0 flags=[mali,broken_roaa,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Arm model=720 fix_generation=0 flags=[mali,broken_roaa,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=720 fix_generation=0 flags=[mali,broken_roaa,avoid_push]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=None flags=[texture_barrier]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Arm model=720 fix_generation=0 flags=[mali,broken_roaa,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Arm model=720 fix_generation=0 flags=[mali,broken_roaa,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Arm model=720 fix_generation=0 flags=[mali,broken_roaa,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
[mali-g615-mediatek-mt6897]
vk.identity vendor=0x13b5 device_id=0x00000000 driver_props=1 driver_id=9 name='Mali-G615 MC6' driver_name='ARM proprietary' info='v1.r48p0-00eac0.0123456789abcdef0123456789abcdef' version=0x0c000000 platform='MediaTek mt6897 Dimensity 8300 arm64-v8a' mediatek=1
vk.runtime_profile=Mobile mediatek_hint(platform)=1 adreno_generation=0
vk.platform_rules [broken_timestamps,prefer_coherent_readback,no_ps2_z_quantization]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='ARM' renderer='Mali-G615 MC6' version='OpenGL ES 3.2 v1.r48p0-00eac0'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=615 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Arm model=615 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=615 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=None flags=[texture_barrier]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Arm model=615 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Arm model=615 fix_generation=0 flags=[mali,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Arm model=615 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
[mali-g715-tensor-google]
vk.identity vendor=0x13b5 device_id=0x00000000 driver_props=1 driver_id=9 name='Mali-G715' driver_name='ARM proprietary' info='v1.r47p0-00eac0.0123456789abcdef0123456789abcdef' version=0x0bc00000 platform='Google Pixel 9 zuma tensor arm64-v8a' mediatek=0
vk.runtime_profile=Mobile mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules [prefer_coherent_readback,no_ps2_z_quantization]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='ARM' renderer='Mali-G715' version='OpenGL ES 3.2 v1.r47p0-00eac0'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=715 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Arm model=715 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Arm model=715 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=None flags=[texture_barrier]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Arm model=715 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Arm model=715 fix_generation=0 flags=[mali,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Arm model=715 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
[mali-g610-panvk-mesa]
vk.identity vendor=0x13b5 device_id=0x00000000 driver_props=1 driver_id=20 name='Mali-G610' driver_name='panvk Mesa driver' info='Mesa 25.1.0' version=0x06401000 platform='Rockchip rk3588 arm64-v8a' mediatek=0
vk.runtime_profile=Mobile mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules [no_ps2_z_quantization]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Mesa' renderer='Mali-G610' version='OpenGL ES 3.1 Mesa 25.1.0'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=PanVK model=610 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=PanVK model=610 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=PanVK model=610 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=None flags=[texture_barrier]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=PanVK model=610 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=PanVK model=610 fix_generation=0 flags=[mali,avoid_push]
  sel road=Copy spelling=Clone carry=None flags=[]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=PanVK model=610 fix_generation=0 flags=[mali,avoid_push]
  sel road=InPassBarrier spelling=InputAttachment carry=NonReaders flags=[texture_barrier]
[xclipse940-samsung]
vk.identity vendor=0x144d device_id=0x00000000 driver_props=1 driver_id=21 name='Samsung Xclipse 940' driver_name='Samsung proprietary' info='' version=0x00800000 platform='Samsung s5e9945 exynos arm64-v8a' mediatek=0
vk.runtime_profile=Unknown mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Samsung Electronics Co., Ltd.' renderer='Samsung Xclipse 940' version='OpenGL ES 3.2 ANGLE'
gl.runtime_profile=Unknown adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[xclipse]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[xclipse]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[xclipse]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=None flags=[texture_barrier,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[xclipse]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[xclipse]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Unknown model=0 fix_generation=0 flags=[xclipse]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier,push_descriptors]
[powervr-bxm-8-256-imagination]
vk.identity vendor=0x1010 device_id=0x00000000 driver_props=1 driver_id=7 name='PowerVR B-Series BXM-8-256' driver_name='Imagination Technologies' info='1.18@6234021' version=0x00412000 platform='Mediatek mt8188 arm64-v8a' mediatek=1
vk.runtime_profile=Mobile mediatek_hint(platform)=1 adreno_generation=0
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=1 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Imagination Technologies' renderer='PowerVR B-Series BXM-8-256' version='OpenGL ES 3.2 build 24.1@6234021'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,push_descriptors,avoid_layout]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,avoid_layout]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,push_descriptors,avoid_layout]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,push_descriptors,avoid_layout]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors,avoid_layout]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier,push_descriptors,avoid_layout]
[powervr-bxm-8-256-mesa]
vk.identity vendor=0x1010 device_id=0x00000000 driver_props=1 driver_id=25 name='PowerVR B-Series BXM-8-256' driver_name='pvr mesa driver' info='Mesa 25.2.0 (PowerVR Mesa)' version=0x06402000 platform='' mediatek=0
vk.runtime_profile=Mobile mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Mesa' renderer='PowerVR B-Series BXM-8-256' version='OpenGL ES 3.2 Mesa 25.2.0'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=MesaPowerVR model=0 fix_generation=0 flags=[powervr]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=MesaPowerVR model=0 fix_generation=0 flags=[powervr]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=MesaPowerVR model=0 fix_generation=0 flags=[powervr]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=None flags=[texture_barrier,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=MesaPowerVR model=0 fix_generation=0 flags=[powervr]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=MesaPowerVR model=0 fix_generation=0 flags=[powervr]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=MesaPowerVR model=0 fix_generation=0 flags=[powervr]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier,push_descriptors]
[powervr-dxt-48-1536-pixel10-angle]
vk.identity vendor=0x1010 device_id=0x00000000 driver_props=1 driver_id=7 name='PowerVR D-Series DXT-48-1536' driver_name='Imagination Technologies' info='1.20@7000000' version=0x00414000 platform='Google Pixel 10 tensor g5 arm64-v8a' mediatek=0
vk.runtime_profile=Mobile mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=1 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Google (Imagination Technologies)' renderer='ANGLE (Imagination Technologies, Vulkan 1.3.0 (PowerVR D-Series DXT-48-1536 (0x00000000)), Imagination Technologies)' version='OpenGL ES 3.2 ANGLE (Imagination Technologies, Vulkan 1.3.0)'
gl.runtime_profile=Mobile adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,push_descriptors,avoid_layout]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,avoid_layout]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,push_descriptors,avoid_layout]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassOrdered spelling=InputAttachment carry=NonReaders flags=[texture_barrier,in_tile_read,push_descriptors,avoid_layout]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors,avoid_layout]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Imagination model=0 fix_generation=0 flags=[powervr,avoid_layout,ordered_input]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier,push_descriptors,avoid_layout]
[apple-m2-honeykrisp]
vk.identity vendor=0x106b device_id=0x00000000 driver_props=1 driver_id=26 name='Apple M2' driver_name='honeykrisp Mesa driver' info='Mesa 25.3.0' version=0x06403000 platform='' mediatek=0
vk.runtime_profile=Unknown mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules [no_ps2_z_quantization]
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Apple' renderer='Apple M2' version='OpenGL 4.1 Metal'
gl.runtime_profile=Unknown adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Honeykrisp model=0 fix_generation=0 flags=[measured_layout,measured_barrier]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Honeykrisp model=0 fix_generation=0 flags=[measured_layout,measured_barrier]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Honeykrisp model=0 fix_generation=0 flags=[measured_layout,measured_barrier]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=NonReaders flags=[texture_barrier,dynamic_loop,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Honeykrisp model=0 fix_generation=0 flags=[measured_layout,measured_barrier]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Honeykrisp model=0 fix_generation=0 flags=[measured_layout,measured_barrier]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Honeykrisp model=0 fix_generation=0 flags=[measured_layout,measured_barrier]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier,push_descriptors]
[broadcom-v3d-v3dv]
vk.identity vendor=0x14e4 device_id=0x00000000 driver_props=1 driver_id=19 name='V3D 7.1.7' driver_name='V3DV Mesa' info='Mesa 25.0.0' version=0x06400000 platform='Raspberry Pi 5 bcm2712 arm64-v8a' mediatek=0
vk.runtime_profile=Unknown mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Broadcom' renderer='V3D 7.1.7' version='OpenGL ES 3.1 Mesa 25.0.0'
gl.runtime_profile=Unknown adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[broadcom]
  sel road=InPassOrdered spelling=InputAttachment carry=All flags=[texture_barrier,in_tile_read,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[broadcom]
  sel road=InPassBarrier spelling=InputAttachment carry=All flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[broadcom]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=All flags=[texture_barrier,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[broadcom]
  sel road=InPassOrdered spelling=InputAttachment carry=All flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[broadcom]
  sel road=Copy spelling=Clone carry=All flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Unknown model=0 fix_generation=0 flags=[broadcom]
  sel road=InPassBarrier spelling=InputAttachment carry=All flags=[texture_barrier,push_descriptors]
[unknown-vendor]
vk.identity vendor=0x1234 device_id=0x00000000 driver_props=0 driver_id=0 name='Mystery GPU 9000' driver_name='' info='' version=0x00000000 platform='' mediatek=0
vk.runtime_profile=Unknown mediatek_hint(platform)=0 adreno_generation=0
vk.platform_rules []
vk.inline provoking_vertex_disabled=0 d32_date_fallback=0 prefer_cached_stream_rings=0 warmup_workers=1 warmup_blocklisted_static=0 broken_blend_constant=0 swapchain_present_modes=1
gl.identity vendor='Acme' renderer='Mystery GPU 9000' version='OpenGL ES 3.2'
gl.runtime_profile=Unknown adreno_generation=0 qualcomm_proprietary=0 single_rgba8_ext_fetch=0 per_primitive_fb_copy(gles,no_barrier)=0
gl.fetch arm=1 ext=0 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=1 enabled=1 backend=EXT veto=None overlap_ordered=0
gl.fetch arm=1 ext=1 enabled=1 backend=ARM veto=None overlap_ordered=1
gl.fetch arm=0 ext=0 enabled=0 backend=None veto=NoExtension overlap_ordered=0
caps[all-ext roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[no-ext roaa=0 layout=0 dyn=0 cmd=0 push=0/0 barriers=-1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier]
caps[layout-no-roaa roaa=0 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[]
  sel road=InPassBarrier spelling=FeedbackLoopLayout carry=None flags=[texture_barrier,push_descriptors]
caps[barriers-forced-on roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=1 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[]
  sel road=InPassOrdered spelling=InputAttachment carry=None flags=[texture_barrier,in_tile_read,push_descriptors]
caps[barriers-forced-off roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=0 disable_fetch=0]
  facts driver=Unknown model=0 fix_generation=0 flags=[]
  sel road=Copy spelling=Clone carry=None flags=[push_descriptors]
caps[fetch-user-disabled roaa=1 layout=1 dyn=1 cmd=1 push=1/32 barriers=-1 disable_fetch=1]
  facts driver=Unknown model=0 fix_generation=0 flags=[]
  sel road=InPassBarrier spelling=InputAttachment carry=None flags=[texture_barrier,push_descriptors]
[derived]
adreno840-qcom-oneplus-CPH2747 vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=8
adreno840-qcom-oneplus-CPH2747 vk.general_attachment_layout[all-ext]=0
adreno840-qcom-oneplus-CPH2747 vk.general_attachment_layout[no-ext]=0
adreno840-qcom-oneplus-CPH2747 vk.general_attachment_layout[layout-no-roaa]=0
adreno840-qcom-oneplus-CPH2747 vk.general_attachment_layout[barriers-forced-on]=1
adreno840-qcom-oneplus-CPH2747 vk.general_attachment_layout[barriers-forced-off]=0
adreno840-qcom-oneplus-CPH2747 vk.general_attachment_layout[fetch-user-disabled]=0
adreno840-qcom-oneplus-CPH2747 vk.fetch_probe[passed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno840-qcom-oneplus-CPH2747 vk.fetch_probe[failed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno840-qcom-oneplus-CPH2747 vk.general_attachment_layout[device-log]=0
adreno750-qcom-lenovo-TB710FU vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=7
adreno750-qcom-lenovo-TB710FU vk.general_attachment_layout[all-ext]=0
adreno750-qcom-lenovo-TB710FU vk.general_attachment_layout[no-ext]=0
adreno750-qcom-lenovo-TB710FU vk.general_attachment_layout[layout-no-roaa]=0
adreno750-qcom-lenovo-TB710FU vk.general_attachment_layout[barriers-forced-on]=1
adreno750-qcom-lenovo-TB710FU vk.general_attachment_layout[barriers-forced-off]=0
adreno750-qcom-lenovo-TB710FU vk.general_attachment_layout[fetch-user-disabled]=0
adreno750-qcom-lenovo-TB710FU vk.fetch_probe[passed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno750-qcom-lenovo-TB710FU vk.fetch_probe[failed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno750-qcom-lenovo-TB710FU vk.general_attachment_layout[device-log]=0
mali-g57-r40p0-arm-anbernic-RG556 vk.shader_workarounds scalarize_vector_bitwise_and=1 store_bitwise_negation_in_temporary=1 gl.shader_workarounds=1 adreno_generation_unified=0
mali-g57-r40p0-arm-anbernic-RG556 vk.general_attachment_layout[all-ext]=1
mali-g57-r40p0-arm-anbernic-RG556 vk.general_attachment_layout[no-ext]=1
mali-g57-r40p0-arm-anbernic-RG556 vk.general_attachment_layout[layout-no-roaa]=1
mali-g57-r40p0-arm-anbernic-RG556 vk.general_attachment_layout[barriers-forced-on]=1
mali-g57-r40p0-arm-anbernic-RG556 vk.general_attachment_layout[barriers-forced-off]=0
mali-g57-r40p0-arm-anbernic-RG556 vk.general_attachment_layout[fetch-user-disabled]=1
mali-g57-r40p0-arm-anbernic-RG556 vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=NonReaders
mali-g57-r40p0-arm-anbernic-RG556 vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
mali-g57-r40p0-arm-anbernic-RG556 vk.general_attachment_layout[device-log]=1
adreno730-turnip-axfl2 vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=7
adreno730-turnip-axfl2 vk.general_attachment_layout[all-ext]=1
adreno730-turnip-axfl2 vk.general_attachment_layout[no-ext]=0
adreno730-turnip-axfl2 vk.general_attachment_layout[layout-no-roaa]=1
adreno730-turnip-axfl2 vk.general_attachment_layout[barriers-forced-on]=1
adreno730-turnip-axfl2 vk.general_attachment_layout[barriers-forced-off]=0
adreno730-turnip-axfl2 vk.general_attachment_layout[fetch-user-disabled]=1
adreno730-turnip-axfl2 vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=0 texture_barrier=1 carry=NonReaders
adreno730-turnip-axfl2 vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=NonReaders
adreno730-turnip-plain vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=7
adreno730-turnip-plain vk.general_attachment_layout[all-ext]=1
adreno730-turnip-plain vk.general_attachment_layout[no-ext]=0
adreno730-turnip-plain vk.general_attachment_layout[layout-no-roaa]=1
adreno730-turnip-plain vk.general_attachment_layout[barriers-forced-on]=1
adreno730-turnip-plain vk.general_attachment_layout[barriers-forced-off]=0
adreno730-turnip-plain vk.general_attachment_layout[fetch-user-disabled]=1
adreno730-turnip-plain vk.fetch_probe[passed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=NonReaders
adreno730-turnip-plain vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=NonReaders
adreno740-turnip-axfl1 vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=7
adreno740-turnip-axfl1 vk.general_attachment_layout[all-ext]=1
adreno740-turnip-axfl1 vk.general_attachment_layout[no-ext]=0
adreno740-turnip-axfl1 vk.general_attachment_layout[layout-no-roaa]=1
adreno740-turnip-axfl1 vk.general_attachment_layout[barriers-forced-on]=1
adreno740-turnip-axfl1 vk.general_attachment_layout[barriers-forced-off]=0
adreno740-turnip-axfl1 vk.general_attachment_layout[fetch-user-disabled]=1
adreno740-turnip-axfl1 vk.fetch_probe[passed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=NonReaders
adreno740-turnip-axfl1 vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=NonReaders
adreno650-turnip-axfl1 vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=6
adreno650-turnip-axfl1 vk.general_attachment_layout[all-ext]=1
adreno650-turnip-axfl1 vk.general_attachment_layout[no-ext]=0
adreno650-turnip-axfl1 vk.general_attachment_layout[layout-no-roaa]=1
adreno650-turnip-axfl1 vk.general_attachment_layout[barriers-forced-on]=1
adreno650-turnip-axfl1 vk.general_attachment_layout[barriers-forced-off]=0
adreno650-turnip-axfl1 vk.general_attachment_layout[fetch-user-disabled]=1
adreno650-turnip-axfl1 vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=0 texture_barrier=1 carry=NonReaders
adreno650-turnip-axfl1 vk.fetch_probe[failed all-ext] road=InPassOrdered in_tile_read=0 texture_barrier=1 carry=NonReaders
adreno650-turnip-plain vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=6
adreno650-turnip-plain vk.general_attachment_layout[all-ext]=0
adreno650-turnip-plain vk.general_attachment_layout[no-ext]=0
adreno650-turnip-plain vk.general_attachment_layout[layout-no-roaa]=0
adreno650-turnip-plain vk.general_attachment_layout[barriers-forced-on]=1
adreno650-turnip-plain vk.general_attachment_layout[barriers-forced-off]=0
adreno650-turnip-plain vk.general_attachment_layout[fetch-user-disabled]=0
adreno650-turnip-plain vk.fetch_probe[passed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno650-turnip-plain vk.fetch_probe[failed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno650-turnip-unmapped-driver-id vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=6
adreno650-turnip-unmapped-driver-id vk.general_attachment_layout[all-ext]=0
adreno650-turnip-unmapped-driver-id vk.general_attachment_layout[no-ext]=0
adreno650-turnip-unmapped-driver-id vk.general_attachment_layout[layout-no-roaa]=0
adreno650-turnip-unmapped-driver-id vk.general_attachment_layout[barriers-forced-on]=1
adreno650-turnip-unmapped-driver-id vk.general_attachment_layout[barriers-forced-off]=0
adreno650-turnip-unmapped-driver-id vk.general_attachment_layout[fetch-user-disabled]=0
adreno650-turnip-unmapped-driver-id vk.fetch_probe[passed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno650-turnip-unmapped-driver-id vk.fetch_probe[failed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno610-turnip vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=6
adreno610-turnip vk.general_attachment_layout[all-ext]=0
adreno610-turnip vk.general_attachment_layout[no-ext]=0
adreno610-turnip vk.general_attachment_layout[layout-no-roaa]=0
adreno610-turnip vk.general_attachment_layout[barriers-forced-on]=1
adreno610-turnip vk.general_attachment_layout[barriers-forced-off]=0
adreno610-turnip vk.general_attachment_layout[fetch-user-disabled]=0
adreno610-turnip vk.fetch_probe[passed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno610-turnip vk.fetch_probe[failed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno840-qcom-no-driver-properties vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=8
adreno840-qcom-no-driver-properties vk.general_attachment_layout[all-ext]=0
adreno840-qcom-no-driver-properties vk.general_attachment_layout[no-ext]=0
adreno840-qcom-no-driver-properties vk.general_attachment_layout[layout-no-roaa]=0
adreno840-qcom-no-driver-properties vk.general_attachment_layout[barriers-forced-on]=1
adreno840-qcom-no-driver-properties vk.general_attachment_layout[barriers-forced-off]=0
adreno840-qcom-no-driver-properties vk.general_attachment_layout[fetch-user-disabled]=0
adreno840-qcom-no-driver-properties vk.fetch_probe[passed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno840-qcom-no-driver-properties vk.fetch_probe[failed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno-chipid-only-650 vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=6
adreno-chipid-only-650 vk.general_attachment_layout[all-ext]=0
adreno-chipid-only-650 vk.general_attachment_layout[no-ext]=0
adreno-chipid-only-650 vk.general_attachment_layout[layout-no-roaa]=0
adreno-chipid-only-650 vk.general_attachment_layout[barriers-forced-on]=1
adreno-chipid-only-650 vk.general_attachment_layout[barriers-forced-off]=0
adreno-chipid-only-650 vk.general_attachment_layout[fetch-user-disabled]=0
adreno-chipid-only-650 vk.fetch_probe[passed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno-chipid-only-650 vk.fetch_probe[failed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno-x1-85-windows-arm vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=9
adreno-x1-85-windows-arm vk.general_attachment_layout[all-ext]=0
adreno-x1-85-windows-arm vk.general_attachment_layout[no-ext]=0
adreno-x1-85-windows-arm vk.general_attachment_layout[layout-no-roaa]=0
adreno-x1-85-windows-arm vk.general_attachment_layout[barriers-forced-on]=1
adreno-x1-85-windows-arm vk.general_attachment_layout[barriers-forced-off]=0
adreno-x1-85-windows-arm vk.general_attachment_layout[fetch-user-disabled]=0
adreno-x1-85-windows-arm vk.fetch_probe[passed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
adreno-x1-85-windows-arm vk.fetch_probe[failed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
mali-g57-r44p1-arm vk.shader_workarounds scalarize_vector_bitwise_and=1 store_bitwise_negation_in_temporary=1 gl.shader_workarounds=1 adreno_generation_unified=0
mali-g57-r44p1-arm vk.general_attachment_layout[all-ext]=0
mali-g57-r44p1-arm vk.general_attachment_layout[no-ext]=0
mali-g57-r44p1-arm vk.general_attachment_layout[layout-no-roaa]=0
mali-g57-r44p1-arm vk.general_attachment_layout[barriers-forced-on]=1
mali-g57-r44p1-arm vk.general_attachment_layout[barriers-forced-off]=0
mali-g57-r44p1-arm vk.general_attachment_layout[fetch-user-disabled]=0
mali-g57-r44p1-arm vk.fetch_probe[passed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
mali-g57-r44p1-arm vk.fetch_probe[failed all-ext] road=Copy in_tile_read=0 texture_barrier=0 carry=None
mali-g57-malisx2-0.5.1 vk.shader_workarounds scalarize_vector_bitwise_and=1 store_bitwise_negation_in_temporary=1 gl.shader_workarounds=1 adreno_generation_unified=0
mali-g57-malisx2-0.5.1 vk.general_attachment_layout[all-ext]=1
mali-g57-malisx2-0.5.1 vk.general_attachment_layout[no-ext]=1
mali-g57-malisx2-0.5.1 vk.general_attachment_layout[layout-no-roaa]=1
mali-g57-malisx2-0.5.1 vk.general_attachment_layout[barriers-forced-on]=1
mali-g57-malisx2-0.5.1 vk.general_attachment_layout[barriers-forced-off]=0
mali-g57-malisx2-0.5.1 vk.general_attachment_layout[fetch-user-disabled]=1
mali-g57-malisx2-0.5.1 vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=NonReaders
mali-g57-malisx2-0.5.1 vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
mali-g720-mediatek-mt6989 vk.shader_workarounds scalarize_vector_bitwise_and=1 store_bitwise_negation_in_temporary=1 gl.shader_workarounds=1 adreno_generation_unified=0
mali-g720-mediatek-mt6989 vk.general_attachment_layout[all-ext]=1
mali-g720-mediatek-mt6989 vk.general_attachment_layout[no-ext]=1
mali-g720-mediatek-mt6989 vk.general_attachment_layout[layout-no-roaa]=1
mali-g720-mediatek-mt6989 vk.general_attachment_layout[barriers-forced-on]=1
mali-g720-mediatek-mt6989 vk.general_attachment_layout[barriers-forced-off]=0
mali-g720-mediatek-mt6989 vk.general_attachment_layout[fetch-user-disabled]=1
mali-g720-mediatek-mt6989 vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=NonReaders
mali-g720-mediatek-mt6989 vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
mali-g615-mediatek-mt6897 vk.shader_workarounds scalarize_vector_bitwise_and=1 store_bitwise_negation_in_temporary=1 gl.shader_workarounds=1 adreno_generation_unified=0
mali-g615-mediatek-mt6897 vk.general_attachment_layout[all-ext]=1
mali-g615-mediatek-mt6897 vk.general_attachment_layout[no-ext]=1
mali-g615-mediatek-mt6897 vk.general_attachment_layout[layout-no-roaa]=1
mali-g615-mediatek-mt6897 vk.general_attachment_layout[barriers-forced-on]=1
mali-g615-mediatek-mt6897 vk.general_attachment_layout[barriers-forced-off]=0
mali-g615-mediatek-mt6897 vk.general_attachment_layout[fetch-user-disabled]=1
mali-g615-mediatek-mt6897 vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=NonReaders
mali-g615-mediatek-mt6897 vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
mali-g715-tensor-google vk.shader_workarounds scalarize_vector_bitwise_and=1 store_bitwise_negation_in_temporary=1 gl.shader_workarounds=1 adreno_generation_unified=0
mali-g715-tensor-google vk.general_attachment_layout[all-ext]=1
mali-g715-tensor-google vk.general_attachment_layout[no-ext]=1
mali-g715-tensor-google vk.general_attachment_layout[layout-no-roaa]=1
mali-g715-tensor-google vk.general_attachment_layout[barriers-forced-on]=1
mali-g715-tensor-google vk.general_attachment_layout[barriers-forced-off]=0
mali-g715-tensor-google vk.general_attachment_layout[fetch-user-disabled]=1
mali-g715-tensor-google vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=NonReaders
mali-g715-tensor-google vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
mali-g610-panvk-mesa vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=0
mali-g610-panvk-mesa vk.general_attachment_layout[all-ext]=1
mali-g610-panvk-mesa vk.general_attachment_layout[no-ext]=1
mali-g610-panvk-mesa vk.general_attachment_layout[layout-no-roaa]=1
mali-g610-panvk-mesa vk.general_attachment_layout[barriers-forced-on]=1
mali-g610-panvk-mesa vk.general_attachment_layout[barriers-forced-off]=0
mali-g610-panvk-mesa vk.general_attachment_layout[fetch-user-disabled]=1
mali-g610-panvk-mesa vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=NonReaders
mali-g610-panvk-mesa vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
xclipse940-samsung vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=0
xclipse940-samsung vk.general_attachment_layout[all-ext]=1
xclipse940-samsung vk.general_attachment_layout[no-ext]=1
xclipse940-samsung vk.general_attachment_layout[layout-no-roaa]=1
xclipse940-samsung vk.general_attachment_layout[barriers-forced-on]=1
xclipse940-samsung vk.general_attachment_layout[barriers-forced-off]=0
xclipse940-samsung vk.general_attachment_layout[fetch-user-disabled]=1
xclipse940-samsung vk.fetch_probe[passed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
xclipse940-samsung vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
powervr-bxm-8-256-imagination vk.shader_workarounds scalarize_vector_bitwise_and=1 store_bitwise_negation_in_temporary=1 gl.shader_workarounds=1 adreno_generation_unified=0
powervr-bxm-8-256-imagination vk.general_attachment_layout[all-ext]=1
powervr-bxm-8-256-imagination vk.general_attachment_layout[no-ext]=1
powervr-bxm-8-256-imagination vk.general_attachment_layout[layout-no-roaa]=1
powervr-bxm-8-256-imagination vk.general_attachment_layout[barriers-forced-on]=1
powervr-bxm-8-256-imagination vk.general_attachment_layout[barriers-forced-off]=0
powervr-bxm-8-256-imagination vk.general_attachment_layout[fetch-user-disabled]=1
powervr-bxm-8-256-imagination vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=NonReaders
powervr-bxm-8-256-imagination vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
powervr-bxm-8-256-mesa vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=0
powervr-bxm-8-256-mesa vk.general_attachment_layout[all-ext]=1
powervr-bxm-8-256-mesa vk.general_attachment_layout[no-ext]=1
powervr-bxm-8-256-mesa vk.general_attachment_layout[layout-no-roaa]=1
powervr-bxm-8-256-mesa vk.general_attachment_layout[barriers-forced-on]=1
powervr-bxm-8-256-mesa vk.general_attachment_layout[barriers-forced-off]=0
powervr-bxm-8-256-mesa vk.general_attachment_layout[fetch-user-disabled]=1
powervr-bxm-8-256-mesa vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=None
powervr-bxm-8-256-mesa vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
powervr-dxt-48-1536-pixel10-angle vk.shader_workarounds scalarize_vector_bitwise_and=1 store_bitwise_negation_in_temporary=1 gl.shader_workarounds=1 adreno_generation_unified=0
powervr-dxt-48-1536-pixel10-angle vk.general_attachment_layout[all-ext]=1
powervr-dxt-48-1536-pixel10-angle vk.general_attachment_layout[no-ext]=1
powervr-dxt-48-1536-pixel10-angle vk.general_attachment_layout[layout-no-roaa]=1
powervr-dxt-48-1536-pixel10-angle vk.general_attachment_layout[barriers-forced-on]=1
powervr-dxt-48-1536-pixel10-angle vk.general_attachment_layout[barriers-forced-off]=0
powervr-dxt-48-1536-pixel10-angle vk.general_attachment_layout[fetch-user-disabled]=1
powervr-dxt-48-1536-pixel10-angle vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=NonReaders
powervr-dxt-48-1536-pixel10-angle vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
apple-m2-honeykrisp vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=0
apple-m2-honeykrisp vk.general_attachment_layout[all-ext]=1
apple-m2-honeykrisp vk.general_attachment_layout[no-ext]=1
apple-m2-honeykrisp vk.general_attachment_layout[layout-no-roaa]=1
apple-m2-honeykrisp vk.general_attachment_layout[barriers-forced-on]=1
apple-m2-honeykrisp vk.general_attachment_layout[barriers-forced-off]=0
apple-m2-honeykrisp vk.general_attachment_layout[fetch-user-disabled]=1
apple-m2-honeykrisp vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=None
apple-m2-honeykrisp vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=NonReaders
broadcom-v3d-v3dv vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=0
broadcom-v3d-v3dv vk.general_attachment_layout[all-ext]=1
broadcom-v3d-v3dv vk.general_attachment_layout[no-ext]=1
broadcom-v3d-v3dv vk.general_attachment_layout[layout-no-roaa]=1
broadcom-v3d-v3dv vk.general_attachment_layout[barriers-forced-on]=1
broadcom-v3d-v3dv vk.general_attachment_layout[barriers-forced-off]=0
broadcom-v3d-v3dv vk.general_attachment_layout[fetch-user-disabled]=1
broadcom-v3d-v3dv vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=All
broadcom-v3d-v3dv vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=All
unknown-vendor vk.shader_workarounds scalarize_vector_bitwise_and=0 store_bitwise_negation_in_temporary=0 gl.shader_workarounds=0 adreno_generation_unified=0
unknown-vendor vk.general_attachment_layout[all-ext]=1
unknown-vendor vk.general_attachment_layout[no-ext]=1
unknown-vendor vk.general_attachment_layout[layout-no-roaa]=1
unknown-vendor vk.general_attachment_layout[barriers-forced-on]=1
unknown-vendor vk.general_attachment_layout[barriers-forced-off]=0
unknown-vendor vk.general_attachment_layout[fetch-user-disabled]=1
unknown-vendor vk.fetch_probe[passed all-ext] road=InPassOrdered in_tile_read=1 texture_barrier=1 carry=None
unknown-vendor vk.fetch_probe[failed all-ext] road=InPassBarrier in_tile_read=0 texture_barrier=1 carry=None
)GOLDEN"
