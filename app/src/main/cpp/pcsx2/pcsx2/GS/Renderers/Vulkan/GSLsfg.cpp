// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
#include "GS/Renderers/Vulkan/GSLsfg.h"
#include "GS/Renderers/Vulkan/GSLsfgNative.h"
#include "GS/Renderers/Vulkan/GSLsfgLegacy.h"
#include "Config.h"
#include "GS/GS.h"
#include "common/FileSystem.h"
#include "common/ScopedGuard.h"
#include <algorithm>
#include <atomic>
#include <mutex>
#ifdef EMUCOREX_HAS_LSFG
#include "GS/Renderers/Vulkan/FrameGen/LosslessDll.h"
#endif
namespace GSLsfg
{
namespace {
 enum class Engine:u8 {Unknown,Native,Legacy};
 std::atomic<Engine> s_engine{Engine::Unknown};
 std::mutex s_path_mutex;
 std::string s_path;
 std::atomic<u64> s_revision{1};
 std::atomic<bool> s_shader_cache_dirty{false};
 u64 s_open_revision=0;
 bool s_fp16=false,s_performance=false;
 Engine Current(){return s_engine.load(std::memory_order_acquire);}
 Engine SelectEngine()
 {
  // Native SPIR-V carries the performance family. Keep Android's existing
  // standard/quality setting on the backend which implements that family.
  if(!GSConfig.LsfgPerformance)return Engine::Legacy;
#ifdef EMUCOREX_HAS_LSFG
  VideoCore::FrameGen::ShaderModules modules;
  if(VideoCore::FrameGen::LoadShaderModules(modules,GSConfig.LsfgFp16,GSConfig.LsfgFp16)==VideoCore::FrameGen::LosslessStatus::Ok)
   return Engine::Native;
#endif
  return Engine::Legacy;
 }
}
void NoteRendererCapability(bool vk,u32 generation){GSLsfgNative::NoteRendererCapability(vk,generation);GSLsfgLegacy::NoteRendererCapability(vk,generation);}
void SetHostRefreshRate(float rate){GSLsfgLegacy::SetHostRefreshRate(rate);}
void SetDllPath(std::string path)
{
 std::unique_lock lock(s_path_mutex);
 if(s_path==path)return;
 s_path=path;GSLsfgNative::SetDllPath(path);GSLsfgLegacy::SetDllPath(std::move(path));s_revision.fetch_add(1,std::memory_order_release);
}
std::string GetDllPath(){std::unique_lock lock(s_path_mutex);return s_path;}
void InvalidateDllVerdict()
{
 GSLsfgNative::InvalidateDllVerdict();GSLsfgLegacy::InvalidateDllVerdict();
 // Imports can replace the same-size DLL within one filesystem timestamp tick.
 // Drop its shader cache on the GS thread, before the next engine selection.
 s_shader_cache_dirty.store(true,std::memory_order_release);
 s_revision.fetch_add(1,std::memory_order_release);
}
bool LooksLikeLosslessDll(const std::string& path){return GSLsfgNative::LooksLikeLosslessDll(path);}
Unavailable GetUnavailableReason(){return Current()==Engine::Legacy?static_cast<Unavailable>(GSLsfgLegacy::GetUnavailableReason()):static_cast<Unavailable>(GSLsfgNative::GetUnavailableReason());}
bool IsAvailable(){return GetUnavailableReason()==Unavailable::Available;}
bool HasSupportedRenderer(){return GSLsfgNative::HasSupportedRenderer();}
const char* GetUnavailableReasonString(){return Current()==Engine::Legacy?GSLsfgLegacy::GetUnavailableReasonString():GSLsfgNative::GetUnavailableReasonString();}
std::string GetStatusText(){return Current()==Engine::Legacy?GSLsfgLegacy::GetStatusText():GSLsfgNative::GetStatusText();}
bool StatusIsProblem(){return GSConfig.LsfgEnabled && (Current()==Engine::Legacy?GetUnavailableReason()!=Unavailable::Available:GSLsfgNative::StatusIsProblem());}
float GetDisplayFPS(){return Current()==Engine::Legacy?GSLsfgLegacy::GetDisplayFPS():GSLsfgNative::GetDisplayFPS();}
void Shutdown()
{
 if(Current()==Engine::Legacy)GSLsfgLegacy::Shutdown();else if(Current()==Engine::Native)GSLsfgNative::Shutdown();
 s_engine.store(Engine::Unknown,std::memory_order_release);
}
bool Initialize(VKSwapChain* swap_chain,u32 multiplier)
{
 const u64 revision=s_revision.load(std::memory_order_acquire);
 if(Current()==Engine::Unknown || s_open_revision!=revision || s_fp16!=GSConfig.LsfgFp16 || s_performance!=GSConfig.LsfgPerformance)
 {
  Shutdown();
  if(s_shader_cache_dirty.exchange(false,std::memory_order_acq_rel))
  {
#ifdef EMUCOREX_HAS_LSFG
   VideoCore::FrameGen::ClearShaderCache();
#endif
   GSLsfgLegacy::ClearShaderCache();
  }
  s_engine.store(SelectEngine(),std::memory_order_release);
  s_open_revision=revision;s_fp16=GSConfig.LsfgFp16;s_performance=GSConfig.LsfgPerformance;
 }
 if(Current()==Engine::Legacy)return GSLsfgLegacy::Initialize(swap_chain,multiplier);
 if(GSLsfgNative::Initialize(swap_chain,multiplier))return true;
 // A DLL can carry both native SPIR-V and the older DXBC family. Preserve the
 // existing AHardwareBuffer backend if this device cannot initialize native FG.
 GSLsfgNative::Shutdown();s_engine.store(Engine::Legacy,std::memory_order_release);
 return GSLsfgLegacy::Initialize(swap_chain,multiplier);
}
bool IsActive(){return Current()==Engine::Legacy?GSLsfgLegacy::IsActive():Current()==Engine::Native&&GSLsfgNative::IsActive();}
u32 GetMultiplier(){return Current()==Engine::Legacy?GSLsfgLegacy::GetMultiplier():Current()==Engine::Native?GSLsfgNative::GetMultiplier():1;}
bool PresentWithGeneration(VkQueue queue,VKSwapChain* chain,VkSemaphore finished,bool new_content)
{return Current()==Engine::Legacy?GSLsfgLegacy::PresentWithGeneration(queue,chain,finished,new_content):Current()==Engine::Native&&GSLsfgNative::PresentWithGeneration(queue,chain,finished,new_content);}
void CaptureGameFrame(VkCommandBuffer cmd,const GameFrame* frame)
{
 if(Current()!=Engine::Native)return;
 static_assert(sizeof(GameFrame)==sizeof(GSLsfgNative::GameFrame));
 GSLsfgNative::GameFrame copy;
 if(frame){copy.image=frame->image;std::copy(std::begin(frame->src),std::end(frame->src),copy.src);std::copy(std::begin(frame->draw),std::end(frame->draw),copy.draw);copy.linear=frame->linear;}
 GSLsfgNative::CaptureGameFrame(cmd,frame?&copy:nullptr);
}
}
#ifdef EMUCOREX_ENABLE_NATIVE_SELF_TESTS
#include "GSFrameGenTests.inl"
#endif
