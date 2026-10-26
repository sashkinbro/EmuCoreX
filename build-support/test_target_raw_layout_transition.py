"""Run the actual cache transition branch with instrumented cache/VRAM owners.

This proves policy and ordering, not GPU rendering. Matrix-0194 is the native
golden regression: raw word zero must stay 0x80102030 across CT32 -> CT16.
"""
import os
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_shared_ir_runtime_gates import ROOT, function


class RawLayoutTransition(unittest.TestCase):
    def test_known_clear_raw_preservation(self):
        path = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW/GSTextureCache.cpp'
        signature = 'bool GSTextureCache::ReadClearTarget('
        self.assertTrue(signature in path.read_text(), 'Known clears currently require a synchronous GPU readback')
        helper = function(path, signature)
        fixture = r'''
#include <vector>
#include <mutex>
#include <cstdint>
#include <cstdio>
#include <limits>
using u32=unsigned;using u16=unsigned short;
enum { PSMCT32=0,PSMCT24=1,PSMCT16=2,PSMCT16S=10,PSMZ32=48,PSMZ24=49,PSMZ16=50,PSMZ16S=58 };
enum class GSHardwareDownloadMode { Enabled, Asynchronous };
struct { GSHardwareDownloadMode HWDownloadMode=GSHardwareDownloadMode::Enabled; } GSConfig;
struct GSVector4i { int left=1,top=1,right=3,bottom=2;bool rempty() const{return left>=right||top>=bottom;} };
struct GIFRegTEX0 { u32 TBP0=0,TBW=1,PSM=0; };
struct GSOffset { struct PA { int y;u32 value(int x)const{return y*8+x;} };PA paMulti(int,int y) const{return {y};} };
struct GSLocalMemory { u32 words[64];u32 last_psm=99;GSOffset GetOffset(u32,u32,u32 psm){last_psm=psm;return{};} u32* vm32(){return words;}u16* vm16(){return reinterpret_cast<u16*>(words);} };
struct GSTexture { enum class State{Dirty,Cleared};enum class Format{Color,DepthStencil,DepthColor,ColorHDR};State state=State::Cleared;Format format=Format::Color;u32 color=0x80102030;float depth=0x1p-32f;State GetState()const{return state;}Format GetFormat()const{return format;}u32 GetClearColor()const{return color;}float GetClearDepth()const{return depth;} };
struct Target { int m_type=0;GSTexture texture;GSTexture* m_texture=&texture;GIFRegTEX0 m_TEX0;std::vector<int> m_dirty;bool m_valid_rgb=true,m_valid_alpha_low=true,m_valid_alpha_high=true,m_rt_alpha_scale=false; };
struct Renderer { GSLocalMemory m_mem,shadow;std::mutex mutex;int marks=0;std::mutex& GetAsyncReadbackMutex(){return mutex;}GSLocalMemory& GetAsyncReadbackMemory(){return shadow;}void MarkAsyncReadbackPagesWrittenLocked(GIFRegTEX0,GSVector4i){++marks;} } renderer;
Renderer* g_gs_renderer=&renderer;
struct GSTextureCache { enum{RenderTarget,DepthStencil};bool ReadClearTarget(Target*,const GSVector4i&,u32* raw_clear=nullptr); };
__HELPER__
int main(){
 GSTextureCache cache;int cases=0;
 for(int v=0;v<25;++v){
  Target t;GSVector4i rect;u32 expected=0x80102030;bool success=true,is16=false;
  GSConfig.HWDownloadMode=GSHardwareDownloadMode::Enabled;
  if(v==1){t.m_rt_alpha_scale=true;t.texture.color=0xff102030;}
  if(v==2){t.m_rt_alpha_scale=true;t.texture.color=0x00102030;expected=0x00102030;}
  if(v==3){t.m_rt_alpha_scale=true;t.texture.color=0xaa102030;success=false;}
  if(v>=4&&v<=7){t.m_TEX0.PSM=PSMZ32;expected=1;if(v==5)t.texture.depth=-1,success=false;if(v==6)t.texture.depth=1,success=false;if(v==7)t.texture.depth=std::numeric_limits<float>::quiet_NaN(),success=false;}
  if(v==8)t.texture.state=GSTexture::State::Dirty,success=false;
  if(v==9)t.m_dirty.push_back(1),success=false;
  if(v==10)t.m_TEX0.PSM=19,success=false;
  if(v>=11&&v<=14){is16=true;t.m_TEX0.PSM=v==11?PSMCT16:v==12?PSMCT16S:v==13?PSMZ16:PSMZ16S;expected=v<13?0x8886:1;}
  if(v==15){t.m_TEX0.PSM=PSMCT24;t.m_valid_alpha_low=t.m_valid_alpha_high=false;expected=0xa5102030;}
  if(v==16)t.m_TEX0.PSM=PSMCT16,t.m_valid_alpha_high=false,success=false;
  if(v==17)GSConfig.HWDownloadMode=GSHardwareDownloadMode::Asynchronous;
  if(v==18)rect.right=rect.left;
  if((t.m_TEX0.PSM&0x30)==0x30)t.m_type=1,t.texture.format=GSTexture::Format::DepthStencil;
  if(v==19)t.m_type=1,t.texture.format=GSTexture::Format::DepthStencil,success=false;
  if(v==20)t.m_TEX0.PSM=PSMZ32,t.texture.format=GSTexture::Format::DepthStencil,success=false;
  if(v==21)t.texture.format=GSTexture::Format::DepthColor,success=false;
  if(v==22)t.m_type=1,t.m_TEX0.PSM=PSMZ32,success=false;
  if(v==23)t.texture.format=GSTexture::Format::ColorHDR,success=false;
  if(v==24)t.m_type=1,t.m_TEX0.PSM=PSMZ32,t.texture.format=GSTexture::Format::DepthColor,success=false;
  for(auto& x:renderer.m_mem.words)x=0xa5a5a5a5;
  for(auto& x:renderer.shadow.words)x=0x5a5a5a5a;
  renderer.marks=0;
  u32 normalized=0xdeadbeef;
  if(cache.ReadClearTarget(&t,rect,&normalized)!=success){std::printf("FAIL clear eligibility %d\n",v);return 1;}
  if(success&&!rect.rempty()){
   if((is16?renderer.m_mem.vm16()[9]:renderer.m_mem.words[9])!=expected){std::printf("FAIL raw value %d got %x expected %x\n",v,is16?renderer.m_mem.vm16()[9]:renderer.m_mem.words[9],expected);return 2;}
   if(renderer.m_mem.last_psm!=t.m_TEX0.PSM)return 3;
   if(t.m_valid_rgb&&t.m_valid_alpha_low&&t.m_valid_alpha_high&&normalized!=expected)return 9;
  }else for(auto x:renderer.m_mem.words)if(x!=0xa5a5a5a5)return 4;
  if(is16){if(renderer.m_mem.vm16()[8]!=0xa5a5||renderer.m_mem.vm16()[11]!=0xa5a5)return 5;}
  else if(renderer.m_mem.words[8]!=0xa5a5a5a5||renderer.m_mem.words[11]!=0xa5a5a5a5)return 6;
  if(v==17&&(renderer.shadow.words[9]!=expected||renderer.marks!=1))return 7;
  ++cases;
 }
 for(unsigned flags=0;flags<8;++flags){
  Target t;GSVector4i rect;t.m_valid_rgb=flags&1;t.m_valid_alpha_low=flags&2;t.m_valid_alpha_high=flags&4;
  u32 mask=(t.m_valid_rgb?0x00ffffff:0)|(t.m_valid_alpha_low?0x0f000000:0)|(t.m_valid_alpha_high?0xf0000000:0);
  GSConfig.HWDownloadMode=GSHardwareDownloadMode::Asynchronous;renderer.marks=0;
  for(auto& x:renderer.m_mem.words)x=0xa5a5a5a5;for(auto& x:renderer.shadow.words)x=0x5a5a5a5a;
  if(!cache.ReadClearTarget(&t,rect)||renderer.m_mem.words[9]!=((0xa5a5a5a5&~mask)|(0x80102030&mask))||renderer.shadow.words[9]!=((0x5a5a5a5a&~mask)|(0x80102030&mask))||renderer.marks!=(mask?1:0))return 8;
  ++cases;
 }
 std::printf("PASS %d production known-clear/raw-mask/async-shadow cases\n",cases);
}
'''.replace('__HELPER__', helper)
        compiler = shutil.which('g++') or shutil.which('clang++') or 'C:/msys64/ucrt64/bin/g++.exe'
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='emucorex-raw-clear-') as temp:
            cpp = Path(temp) / 'test.cpp'
            executable = Path(temp) / ('test.exe' if os.name == 'nt' else 'test')
            cpp.write_text(fixture, encoding='utf-8')
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_transition_ownership_and_raw_preload(self):
        path = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW/GSTextureCache.cpp'
        source = path.read_text(encoding='utf-8')
        signature = 'if (iteration == 0 && used && !is_frame && !is_shuffle && !src &&'
        self.assertTrue(signature in source, 'A 16/32-bit format transition currently reuses colors instead of raw VRAM')
        branch = function(path, signature)
        self.assertIn('list->MoveFront(i.Index());', branch)
        # The vector fixture records the FastList-only MRU operation.
        branch = branch.replace('list->MoveFront(i.Index());', 'events.push_back("move-front");')
        config = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/Config.h'
        download_policy = function(config, 'enum class GSHardwareDownloadMode') + ';\n' + function(config, 'constexpr bool IsHardwareDownloadReadbackEnabled')
        fixture = r'''
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <algorithm>
using u32 = unsigned;
using u8 = unsigned char;
__DOWNLOAD_POLICY__
struct { GSHardwareDownloadMode HWDownloadMode=GSHardwareDownloadMode::Enabled; } GSConfig;
struct GSVector2i { int x=64,y=64; };
struct GSVector4i { bool empty=false; bool rempty() const { return empty; } static GSVector4i loadh(GSVector2i) { return {}; } };
struct GIFRegTEX0 { u32 TBP0=0, TBW=1, PSM=0; };
struct RGBAMask { u32 _u32=0; };
namespace GSUtil { u32 GetChannelMask(u32) { return 15; } }
namespace GSLocalMemory { struct PSM { int bpp=32,depth=0; }; PSM m_psm[8]; }
std::vector<std::string> events;
u32 raw_vram=0;
bool transfer_succeeds=true; bool pattern_success=false;bool reuse_success=false;
struct GSTexture { enum class State { Dirty, Cleared }; State state=State::Cleared; State GetState() const { return state; } };
struct Target {
 GSTexture texture;GSTexture* m_texture=&texture;
 GIFRegTEX0 m_TEX0;
 int m_type=0;
 bool fresh=false,overlaps=false,overlaps_old_only=false;
 bool m_valid_rgb=true,m_valid_alpha_low=true,m_valid_alpha_high=true;
 u32 gpu_word=0x80102030;
 GSVector4i m_drawn_since_read;
 std::vector<int> m_dirty;
 void Update(bool=false) { events.push_back(fresh ? "upload-new-raw" : "update-old"); if (fresh) gpu_word=raw_vram; }
 bool Overlaps(u32,u32,u32 psm,GSVector4i) { return overlaps || (overlaps_old_only && psm==0); }
 ~Target() { events.push_back(fresh ? "delete-new" : "delete-old"); }
};
struct Cache {
 std::vector<Target*> m_dst[2];
 bool ReadClearTarget(Target* t,GSVector4i,u32* raw_clear=nullptr) { if(t->m_texture->GetState()!=GSTexture::State::Cleared||!t->m_dirty.empty())return false;events.push_back("materialize-old-psm-"+std::to_string(t->m_TEX0.PSM));if(transfer_succeeds){raw_vram=t->gpu_word;if(raw_clear)*raw_clear=t->gpu_word;}return transfer_succeeds; }
 bool ReadWithStatus(Target* t,GSVector4i,bool) { events.push_back("GPU-wait");if(transfer_succeeds)raw_vram=t->gpu_word;return transfer_succeeds; }
 void InvalidateSourcesFromTarget(Target*) { events.push_back("invalidate-sources"); }
 Target* CreateTarget(GIFRegTEX0 tex,GSVector2i,GSVector2i,float,int type,bool,u32,bool display,bool preload,bool preserve,GSVector4i,void* src) {
  if (display || !preload || !preserve || src) return nullptr;
  events.push_back("create-new"); auto* t=new Target; t->fresh=true;t->m_type=type;t->m_TEX0=tex;
  t->m_dirty={1}; m_dst[type].push_back(t); return t;
 }
 void AddDirtyRectTarget(Target* t,GSVector4i,u32,u32,RGBAMask,bool) { events.push_back("dirty-new-raw");t->m_dirty.push_back(1); }
 bool TryReuseClearPatternTarget(Target* t,GIFRegTEX0 tex,GSVector2i,float,u32 raw){if(!reuse_success)return false;t->gpu_word=raw;t->m_TEX0=tex;events.push_back("reuse-inplace");events.push_back("invalidate-sources");return true;}
 bool UploadClearPattern(Target* t,GIFRegTEX0,GSVector4i,u32 raw){if(!pattern_success)return false;events.push_back("pattern-upload");t->gpu_word=raw;return true;}
 Target* Lookup(Target* t,int iteration,bool used,bool is_frame,bool is_shuffle,void* src,Target* ds,GIFRegTEX0 TEX0) {
  const auto& psm_s=GSLocalMemory::m_psm[TEX0.PSM];
  int type=t->m_type;u32 bp=TEX0.TBP0;auto* list=&m_dst[type];auto i=list->begin();
  GSVector2i size;float scale=1;u32 fbmask=0;GSVector4i draw_rect;
  __BRANCH__
  return t;
 }
};
int main() {
 int cases=0;
 for (int variant=0;variant<36;++variant) {
  events.clear();raw_vram=0;transfer_succeeds=true;pattern_success=false;reuse_success=false;
  GSConfig.HWDownloadMode=GSHardwareDownloadMode::Enabled;
  GSLocalMemory::m_psm[0]={32,0}; GSLocalMemory::m_psm[1]={16,0};
  Cache cache;auto* old=new Target;cache.m_dst[0].push_back(old);
  Target opposite; opposite.m_type=1;
  GIFRegTEX0 requested;requested.PSM=1;
  int iteration=0;bool used=true,display=false,shuffle=false;void* src=nullptr;Target* ds=nullptr;
  bool expected=variant<4 || variant==16 || variant==18 || variant==25 || variant==26 || variant==28 || variant==31 || variant==32 || variant==33 || variant==34 || variant==35;
  if (variant==1) { old->m_TEX0.PSM=1;requested.PSM=0; }
  if (variant==2) { GSLocalMemory::m_psm[0].depth=GSLocalMemory::m_psm[1].depth=1;old->m_type=1;cache.m_dst[0].clear();cache.m_dst[1].push_back(old); }
  if (variant==3) { old->m_drawn_since_read.empty=true;raw_vram=old->gpu_word; }
  if (variant==4) iteration=1;
  if (variant==5) used=false;
  if (variant==6) display=true;
  if (variant==7) shuffle=true;
  if (variant==8) src=old;
  if (variant==9) requested.TBW=2;
  if (variant==10) GSLocalMemory::m_psm[1].depth=1;
  if (variant==11) GSLocalMemory::m_psm[1].bpp=32;
  if (variant==12) GSLocalMemory::m_psm[0].bpp=16;
  if (variant==13) ds=old;
  if (variant==14) { opposite.overlaps=true;cache.m_dst[1].push_back(&opposite); }
  if (variant==15) transfer_succeeds=false;
  if (variant==16) { opposite.overlaps=false;cache.m_dst[1].push_back(&opposite); }
  if (variant==17 || variant==18) { opposite.m_type=0;opposite.m_TEX0.TBP0=8;opposite.overlaps=variant==17;cache.m_dst[0].push_back(&opposite); }
  if (variant>=19 && variant<=21) { old->m_TEX0.PSM=1;requested.PSM=0; if(variant==19) old->m_valid_rgb=false;if(variant==20) old->m_valid_alpha_low=false;if(variant==21) old->m_valid_alpha_high=false; }
  if (variant>=22 && variant<=26) GSConfig.HWDownloadMode=static_cast<GSHardwareDownloadMode>(variant==22 ? 2 : variant==23 ? 3 : variant==24 ? 4 : variant==25 ? 1 : 5);
  if (variant==27||variant==28) { opposite.m_type=0;opposite.m_TEX0.TBP0=8;opposite.overlaps_old_only=true;cache.m_dst[0].push_back(&opposite);if(variant==28) old->m_drawn_since_read.empty=true,raw_vram=old->gpu_word; }
  if(variant==29)old->m_texture->state=GSTexture::State::Dirty;
  if(variant==30)old->m_dirty.push_back(1);
  if(variant==31)old->m_texture->state=GSTexture::State::Dirty,old->m_drawn_since_read.empty=true,raw_vram=old->gpu_word;
  if(variant==32||variant==33)pattern_success=true;
  if(variant==33)old->m_valid_alpha_high=false;
  if(variant==34||variant==35)reuse_success=true;
  if(variant==35)old->m_valid_alpha_high=false;
  Target* actual=cache.Lookup(old,iteration,used,display,shuffle,src,ds,requested);
  if (!actual || actual->fresh!=(expected&&variant!=34)) { std::printf("FAIL policy %d\n",variant);return 1; }
  if (expected) {
   const std::vector<std::string> wanted=variant==34 ? std::vector<std::string>{"update-old","materialize-old-psm-0","reuse-inplace","invalidate-sources","move-front"} : variant==32 ? std::vector<std::string>{"update-old","materialize-old-psm-0","invalidate-sources","delete-old","create-new","pattern-upload"} : (variant==3 || variant==28 || variant==31) ?
    std::vector<std::string>{"update-old","invalidate-sources","delete-old","create-new","dirty-new-raw","upload-new-raw"} :
    std::vector<std::string>{"update-old","materialize-old-psm-"+std::to_string(variant==1?1:0),"invalidate-sources","delete-old","create-new","dirty-new-raw","upload-new-raw"};
   if (events!=wanted || actual->gpu_word!=0x80102030 || raw_vram!=0x80102030 || actual->m_TEX0.PSM!=requested.PSM) { std::printf("FAIL ordering/raw preservation %d\n",variant);return 2; }
  } else if (std::find(events.begin(),events.end(),"delete-old")!=events.end()) return 3;
  if ((variant==17 || variant==18 || variant==27||variant==28) && (std::find(cache.m_dst[0].begin(),cache.m_dst[0].end(),&opposite)==cache.m_dst[0].end() || opposite.m_TEX0.TBP0!=8)) return 4;
  if(std::find(events.begin(),events.end(),"GPU-wait")!=events.end()){std::printf("FAIL unexpected GPU wait %d\n",variant);return 5;}
  if((variant==29||variant==30)&&!events.empty())return 6;
  delete actual;++cases;
 }
 std::printf("PASS %d production transition ownership/policy/golden-raw cases\n",cases);
}
'''.replace('__BRANCH__', branch).replace('__DOWNLOAD_POLICY__', download_policy)
        compiler = shutil.which('g++') or shutil.which('clang++') or 'C:/msys64/ucrt64/bin/g++.exe'
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='emucorex-raw-layout-') as temp:
            cpp = Path(temp) / 'test.cpp'
            executable = Path(temp) / ('test.exe' if os.name == 'nt' else 'test')
            cpp.write_text(fixture, encoding='utf-8')
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(executable)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_inplace_clear_pattern_target(self):
        path = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW/GSTextureCache.cpp'
        self.assertTrue('bool GSTextureCache::TryReuseClearPatternTarget(' in path.read_text(), 'Known-clear transition still destroys and recreates an identical backing target')
        header = path.with_suffix('.h').read_text()
        # Pin every live Surface/Target field to the reuse equivalence audit.
        # Adding metadata requires reviewing its reset/preservation policy here.
        audited = {
            'm_texture', 'm_TEX0', 'm_TEXA', 'm_unscaled_size', 'm_scale', 'm_age',
            'm_end_block', 'm_32_bits_fmt', 'm_was_dst_matched', 'm_shared_texture',
            'm_type', 'm_alpha_max', 'm_alpha_min', 'm_alpha_range',
            'm_valid_alpha_low', 'm_valid_alpha_high', 'm_valid_rgb',
            'm_rt_alpha_scale', 'm_downscaled', 'm_last_draw', 'm_is_frame',
            'm_used', 'OffsetHack_modxy', 'm_dirty', 'm_valid',
            'm_drawn_since_read', 'readbacks_since_draw',
        }
        fields = set()
        for start, end in (('class Surface :', '__fi GSTexture* GetTexture'), ('class Target :', 'Target(GIFRegTEX0')):
            declaration = header[header.index(start):]
            declaration = declaration[:declaration.index(end)]
            fields.update(re.findall(r'\b(m_\w+|readbacks_since_draw|OffsetHack_modxy)\s*(?:=|\{|;)', declaration))
        self.assertEqual(fields, audited)

    def test_clear_pattern_cache_lifetime(self):
        path = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW/GSTextureCache.cpp'
        self.assertTrue('GSTexture* GSTextureCache::GetClearPatternTexture(' in path.read_text(), 'Repeated raw clears still allocate/upload pattern textures')

        helper = function(path, 'GSTexture* GSTextureCache::GetClearPatternTexture(') + function(path, 'void GSTextureCache::ReleaseClearPatterns(')
        renderer = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW/GSRendererHW.cpp'
        purge = function(renderer, 'void GSRendererHW::PurgeTextureCache(')
        self.assertLess(purge.index('ReleaseClearPatterns()'), purge.index('RemoveAll('))
        self.assertNotIn('ReleaseClearPatterns', function(renderer, 'void GSRendererHW::Reset('))
        self.assertNotIn('m_clear_patterns', function(path, 'void GSTextureCache::RemoveAll('))
        destructor = function(path, 'GSTextureCache::~GSTextureCache(')
        self.assertIn('ReleaseClearPatterns()', destructor)
        gs = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/GS.cpp'
        reopen = function(gs, 'bool GSreopen(')
        self.assertLess(reopen.index('PurgeTextureCache('), reopen.index('CloseGSDevice('))
        fixture = r'''
#include <vector>
#include <cstdio>
using u32=unsigned;
struct GSVector4i{int x,y,z,w;GSVector4i(int a,int b,int c,int d):x(a),y(b),z(c),w(d){}};
int uploads=0;bool fail_update=false;
struct GSTexture{enum class Format{Color};int id=0;std::vector<u32> pixels;bool Update(GSVector4i r,const void*p,int pitch){++uploads;if(r.z!=16||r.w!=1||pitch!=64)return false;pixels.assign((const u32*)p,(const u32*)p+16);return !fail_update;}};
struct Device{GSTexture textures[32];int creates=0,next=0;bool fail_alloc=false;std::vector<int> recycled;GSTexture* CreateTexture(int w,int h,int levels,GSTexture::Format){++creates;if(fail_alloc||w!=16||h!=1||levels!=1)return nullptr;auto*t=&textures[next++];t->id=next;return t;}void Recycle(GSTexture*t){recycled.push_back(t->id);}}device;
Device* g_gs_device=&device;
struct GSTextureCache{struct ClearPatternEntry{GSTexture*texture=nullptr;u32 raw_clear=0;};ClearPatternEntry m_clear_patterns[2];GSTexture* GetClearPatternTexture(bool,u32);void ReleaseClearPatterns();~GSTextureCache(){ReleaseClearPatterns();}};
__HELPER__
int main(){
 {GSTextureCache c;
 auto* a=c.GetClearPatternTexture(false,0x80102030);if(!a||uploads!=1||device.creates!=1)return 1;
 if(c.GetClearPatternTexture(false,0x80102030)!=a||uploads!=1||device.creates!=1)return 2;
 auto* b=c.GetClearPatternTexture(true,0x80102030);if(!b||b==a||uploads!=2)return 3;
 if(c.GetClearPatternTexture(true,0x80102030)!=b||uploads!=2)return 4;
 device.fail_alloc=true;if(c.GetClearPatternTexture(false,0x12345678)||c.m_clear_patterns[0].texture!=a||c.m_clear_patterns[0].raw_clear!=0x80102030||uploads!=2)return 5;
 device.fail_alloc=false;fail_update=true;if(c.GetClearPatternTexture(false,0x12345678)||c.m_clear_patterns[0].texture!=a||c.m_clear_patterns[0].raw_clear!=0x80102030||uploads!=3||device.recycled.size()!=1)return 6;
 if(c.GetClearPatternTexture(false,0x80102030)!=a||uploads!=3)return 7;
 fail_update=false;auto*d=c.GetClearPatternTexture(false,0x12345678);if(!d||d==a||device.recycled.size()!=2||device.recycled.back()!=a->id||uploads!=4)return 8;
 if(c.GetClearPatternTexture(true,0x80102030)!=b||uploads!=4)return 9;
 c.ReleaseClearPatterns();if(device.recycled.size()!=4||c.m_clear_patterns[0].texture||c.m_clear_patterns[1].texture)return 10;
 c.ReleaseClearPatterns();if(device.recycled.size()!=4)return 11;
 c.GetClearPatternTexture(false,0);c.GetClearPatternTexture(true,1);
 }
 if(device.recycled.size()!=6)return 12;
 std::puts("PASS 12 production immutable-pattern cache hit/miss/failure/teardown cases + reset/device lifecycle ordering");}
'''.replace('__HELPER__', helper)
        compiler = shutil.which('g++') or shutil.which('clang++') or 'C:/msys64/ucrt64/bin/g++.exe'
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='emucorex-pattern-cache-') as temp:
            cpp = Path(temp) / 'test.cpp'
            exe = Path(temp) / 'test.exe'
            cpp.write_text(fixture, encoding='utf-8')
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_clear_pattern_upload(self):
        path = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW/GSTextureCache.cpp'
        self.assertTrue('bool GSTextureCache::UploadClearPattern(' in path.read_text(), 'Known clears still upload full staging textures')

        helper = function(path, 'GSTexture* GSTextureCache::GetClearPatternTexture(') + function(path, 'void GSTextureCache::ReleaseClearPatterns(') + function(path, 'bool GSTextureCache::UploadClearPattern(') + function(path, 'bool GSTextureCache::TryReuseClearPatternTarget(')
        fixture = r'''
#include <vector>
#include <algorithm>
#include <cstdio>
using u32=unsigned;enum {PSMCT32=0,PSMCT16=2,PSMCT16S=10,PSMZ32=48,PSMZ16=50,PSMZ16S=58};enum {Nearest};
struct GSVector2i;
struct GSVector4i{int x,y,z,w;GSVector4i(int a=0,int b=0,int c=0,int d=0):x(a),y(b),z(c),w(d){}static GSVector4i loadh(GSVector2i);};
struct GSVector2i{int x=64,y=64;};
GSVector4i GSVector4i::loadh(GSVector2i s){return{0,0,s.x,s.y};}
struct GSVector4{float x,y,z,w;GSVector4(float a=0,float b=0,float c=0,float d=0):x(a),y(b),z(c),w(d){}};
struct GIFRegTEX0{u32 TBP0=0,TBW=1,PSM=PSMCT16;};
using GIFRegTEXA = int; // Zero-value storage seam; native build checks register overloads.
struct GSTexture{enum class Format{Color,DepthStencil};Format fmt=Format::Color;int width=64,height=64;int GetWidth(){return width;}int GetHeight(){return height;}bool success=true;std::vector<u32> pixels;Format GetFormat(){return fmt;}bool Update(GSVector4i,const void* p,int pitch){if(pitch!=64)return false;pixels.assign((const u32*)p,(const u32*)p+16);return success;}};
enum class ShaderConvert{COPY,RTA_CORRECTION,RGB5A1_TO_DEPTH16};struct ShaderConvertSelector{ShaderConvert mode;bool depth=false;ShaderConvertSelector(ShaderConvert m):mode(m){}ShaderConvertSelector SetDepthOutput(bool d){depth=d;return *this;}};
struct Target{GIFRegTEX0 m_TEX0;float m_scale=1;struct{int x=64,y=64;}m_unscaled_size;int m_type=0;GSTexture tex;GSTexture* m_texture=&tex;std::vector<int> m_dirty{1};bool m_rt_alpha_scale=true,m_valid_rgb=false,m_valid_alpha_low=false,m_valid_alpha_high=false,m_alpha_range=false;GSVector4i m_drawn_since_read,valid;int m_TEXA=9,m_age=9,readbacks_since_draw=9;bool m_shared_texture=false,m_32_bits_fmt=true,m_was_dst_matched=true,m_downscaled=false,m_is_frame=true,m_used=false;unsigned m_end_block=0,m_last_draw=999;float OffsetHack_modxy=5;void ResizeValidity(GSVector4i r){valid=r;m_end_block=m_TEX0.TBP0+unsigned(r.z*r.w/128)-1;}int m_alpha_min=-1,m_alpha_max=-1;void UpdateValidity(GSVector4i r){valid=r;}};
struct GSDevice{struct MultiStretchRect{GSVector4 src_rect,dst_rect;GSTexture* src;int filter;struct{unsigned char wrgba;}wmask;};GSTexture texture;bool fail_alloc=false,fail_update=false;int creates=0,batches=0,recycles=0;std::vector<MultiStretchRect> rects;ShaderConvertSelector shader{ShaderConvert::COPY};GSTexture* CreateTexture(int w,int h,int n,GSTexture::Format f){++creates;if(w!=16||h!=1||n!=1||f!=GSTexture::Format::Color)return nullptr;texture.success=!fail_update;return fail_alloc?nullptr:&texture;}void Recycle(GSTexture*){++recycles;}void DrawMultiStretchRects(const MultiStretchRect* r,u32 n,GSTexture*,ShaderConvertSelector s){++batches;rects.assign(r,r+n);shader=s;}}device;
GSDevice* g_gs_device=&device;
struct Transfer{bool zero_clear=true;struct{unsigned DBP=0,DBW=1;}blit;};
struct Renderer{float up=1;float GetUpscaleMultiplier(){return up;}std::vector<Transfer> m_draw_transfers;}renderer;
Renderer* g_gs_renderer=&renderer;
struct GSRendererHW{static Renderer* GetInstance(){return &renderer;}};
struct GSState{static constexpr unsigned s_n=100;};
struct GSTextureCache{enum{RenderTarget,DepthStencil};struct ClearPatternEntry{GSTexture* texture=nullptr;u32 raw_clear=0;};ClearPatternEntry m_clear_patterns[2];GSTexture* GetClearPatternTexture(bool,u32);void ReleaseClearPatterns();~GSTextureCache(){ReleaseClearPatterns();}GSTexture* temp=nullptr;GSTexture* GetTemporaryZ(){return temp;}int invalidations=0;void InvalidateSourcesFromTarget(Target*){++invalidations;}bool TryReuseClearPatternTarget(Target*,GIFRegTEX0,const GSVector2i&,float,u32);bool UploadClearPattern(Target*,const GIFRegTEX0&,const GSVector4i&,u32);};
__HELPER__
int main(){for(int v=0;v<21;++v){device=GSDevice{};GSTextureCache c;Target t;GIFRegTEX0 old;old.PSM=PSMCT32;GSVector4i drawn(0,0,64,32);bool ok=v<=3||v==19;
 if(v==1)t.m_TEX0.PSM=PSMCT16S;
 if(v==2||v==3||v==20){old.PSM=PSMZ32;t.m_TEX0.PSM=v==2?PSMZ16:PSMZ16S;t.m_type=1;t.tex.fmt=GSTexture::Format::DepthStencil;}
 if(v==4)t.m_scale=2;if(v==5)t.m_unscaled_size.x=0;if(v==6)t.m_unscaled_size.y=0;if(v==7)t.m_TEX0.TBW=0;if(v==8)t.m_unscaled_size.x=65;if(v==9)old.TBP0=1;if(v==10)old.TBW=2;if(v==11)old.PSM=PSMCT16;if(v==12)t.m_TEX0.PSM=PSMCT32;if(v==13)t.m_type=1;if(v==14)t.tex.fmt=GSTexture::Format::DepthStencil;if(v==15)drawn.x=1;if(v==16)drawn.z=63;if(v==17)drawn.w=31;
 if(v==20)c.temp=&t.tex;
 if(v==18)device.fail_alloc=true;if(v==19){t.m_unscaled_size.x=65;t.m_TEX0.TBW=old.TBW=2;drawn.z=128;}
 if(c.UploadClearPattern(&t,old,drawn,0x80102030)!=ok){std::printf("FAIL pattern policy %d\n",v);return 1;}
 if(!ok){if(device.batches||t.m_dirty.empty()||!t.m_rt_alpha_scale||t.m_alpha_min!=-1)return 2;continue;}
 if(device.batches!=1||device.recycles!=0||device.rects.size()!=(unsigned)((t.m_unscaled_size.x+15)/16))return 3;
 if(device.shader.depth!=(v==2||v==3)||device.shader.mode!=((v==2||v==3)?ShaderConvert::RGB5A1_TO_DEPTH16:ShaderConvert::RTA_CORRECTION))return 4;
 if(!t.m_dirty.empty()||t.m_rt_alpha_scale!=(v!=2&&v!=3)||!t.m_valid_rgb||!t.m_valid_alpha_low||!t.m_valid_alpha_high||t.valid.z!=t.m_unscaled_size.x||t.m_drawn_since_read.w!=64||t.m_alpha_min!=0||t.m_alpha_max!=128||!t.m_alpha_range)return 5;
 for(auto& r:device.rects){if(r.filter!=Nearest||r.wmask.wrgba!=15||r.dst_rect.y!=0||r.dst_rect.w!=64)return 6;}
 for(int x=0;x<t.m_unscaled_size.x;++x){auto& r=device.rects[x/16];int p=int(((x+.5f-r.dst_rect.x)/(r.dst_rect.z-r.dst_rect.x))*(r.src_rect.z-r.src_rect.x)*16);u32 rgba=device.texture.pixels[p];u32 raw=((rgba&0xf8)>>3)|((rgba&0xf800)>>6)|((rgba&0xf80000)>>9)|((rgba&0x80000000)>>16);if(raw!=((0x80102030u>>((x&8)?16:0))&65535))return 7;}
 }
 device=GSDevice{};GSTextureCache c;device.fail_update=true;Target t;GIFRegTEX0 old;old.PSM=PSMCT32;if(c.UploadClearPattern(&t,old,GSVector4i(0,0,64,32),0)||device.batches||device.recycles!=1||t.m_dirty.empty()||!t.m_rt_alpha_scale)return 8;
 std::puts("PASS 22 production pattern policy/batch/metadata/allocation-failure cases");
 c.ReleaseClearPatterns();
 for(int v=0;v<24;++v){device=GSDevice{};renderer=Renderer{};GSTextureCache reuse;Target old_target;old_target.m_TEX0.PSM=PSMCT32;old_target.m_valid_rgb=old_target.m_valid_alpha_low=old_target.m_valid_alpha_high=true;old_target.m_drawn_since_read={0,0,64,64};old_target.valid={0,0,64,64};GIFRegTEX0 requested;GSVector2i size;float scale=1;bool expected=v<8;
 if(v==1)requested.PSM=PSMCT16S;
 if(v>=2&&v<=6){renderer.m_draw_transfers.push_back({});if(v==3)renderer.m_draw_transfers.back().zero_clear=false;if(v==4)renderer.m_draw_transfers.back().blit.DBP=1;if(v==5)renderer.m_draw_transfers.back().blit.DBW=2;if(v==6){renderer.m_draw_transfers.push_back({});renderer.m_draw_transfers.back().zero_clear=false;}}
 if(v==7)renderer.up=2;
 if(v==8)device.fail_update=true;
 if(v==9)old_target.m_type=1;if(v==10)old_target.m_shared_texture=true;if(v==11)old_target.m_TEX0.PSM=PSMCT16;if(v==12)requested.PSM=PSMCT32;if(v==13)old_target.tex.fmt=GSTexture::Format::DepthStencil;if(v==14)old_target.m_valid_rgb=false;if(v==15)old_target.m_valid_alpha_low=false;if(v==16)old_target.m_valid_alpha_high=false;if(v==17)scale=2;if(v==18)old_target.m_scale=2;if(v==19)old_target.m_unscaled_size.x=63;if(v==20)old_target.m_unscaled_size.y=63;if(v==21)old_target.tex.width=63;if(v==22)old_target.tex.height=63;if(v==23)old_target.m_drawn_since_read={0,0,64,31};
 auto old_psm=old_target.m_TEX0.PSM;auto* backing=old_target.m_texture;unsigned transfers=renderer.m_draw_transfers.size();
 if(reuse.TryReuseClearPatternTarget(&old_target,requested,size,scale,0x80102030)!=expected){std::printf("FAIL reuse policy %d\n",v);return 9;}
 if(old_target.m_texture!=backing)return 10;
 if(expected){if(old_target.m_TEX0.PSM!=requested.PSM||old_target.m_32_bits_fmt||old_target.m_was_dst_matched||old_target.m_TEXA||old_target.m_age||old_target.readbacks_since_draw||old_target.m_is_frame||!old_target.m_used||old_target.OffsetHack_modxy||old_target.m_downscaled!=(v==7)||old_target.m_last_draw!=(v==2?100u:101u)||old_target.valid.z!=64||old_target.valid.w!=64||old_target.m_end_block!=31||old_target.m_alpha_min!=0||old_target.m_alpha_max!=128||!old_target.m_rt_alpha_scale||reuse.invalidations!=1||device.batches!=1||renderer.m_draw_transfers.size()!=transfers-(v==2)){std::printf("FAIL fresh-target metadata/EE clear equivalence %d\n",v);return 11;}}
 else if(old_target.m_TEX0.PSM!=old_psm||old_target.m_TEXA!=9||old_target.m_last_draw!=999||old_target.m_dirty.empty()||reuse.invalidations||device.batches||renderer.m_draw_transfers.size()!=transfers)return 12;
 }
 std::puts("PASS 24 production in-place target metadata/EE-clear/rollback cases");}
'''.replace('__HELPER__', helper)
        compiler = shutil.which('g++') or shutil.which('clang++') or 'C:/msys64/ucrt64/bin/g++.exe'
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='emucorex-clear-pattern-') as temp:
            cpp = Path(temp) / 'test.cpp'
            exe = Path(temp) / 'test.exe'
            cpp.write_text(fixture, encoding='utf-8')
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

    def test_raw_depth_cpu_read(self):
        path = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW/GSTextureCache.cpp'
        signature = 'bool GSTextureCache::TryReadRawDepthTarget('
        self.assertTrue(signature in path.read_text(), 'Z32 CPU reads skip genuine Z16 targets')
        helper = function(path, signature)
        caller = function(path, 'void GSTextureCache::InvalidateLocalMem(')
        self.assertLess(caller.index('TryReadRawDepthTarget(t, bp, bw, psm, r, force_synchronous)'), caller.index('t->m_TEX0.PSM = PSMZ32'))
        fixture = r'''
#include <vector>
#include <cstdio>
using u32=unsigned;
enum { PSMZ32=48,PSMZ16=50,PSMZ16S=58 };
enum class GSHardwareDownloadMode { Enabled, Disabled, Asynchronous };
bool IsHardwareDownloadReadbackEnabled(GSHardwareDownloadMode m){return m!=GSHardwareDownloadMode::Disabled;}
struct {GSHardwareDownloadMode HWDownloadMode=GSHardwareDownloadMode::Enabled;} GSConfig;
struct GSVector4i { int x=0,y=0,z=4,w=8;bool rempty()const{return x>=z||y>=w;}static GSVector4i zero(){return{0,0,0,0};} };
struct GIFRegTEX0 {u32 TBP0=16,TBW=1,PSM=PSMZ16;};
struct GSTexture {enum class Format{Color,DepthStencil};Format fmt=Format::DepthStencil;Format GetFormat(){return fmt;}};
struct Target {int m_type=1;bool m_32_bits_fmt=false,m_valid_rgb=true,m_valid_alpha_low=true,m_valid_alpha_high=true;GIFRegTEX0 m_TEX0;GSVector4i m_drawn_since_read;GSTexture tex;GSTexture* m_texture=&tex;int updates=0,readbacks_since_draw=0;bool overlap_old=false,overlap_read=false;void Update(){++updates;}bool Overlaps(u32,u32,u32 psm,GSVector4i){return psm==PSMZ32?overlap_read:overlap_old;}};
struct GSTextureCache {enum{RenderTarget,DepthStencil};std::vector<Target*> m_dst[2];bool read_ok=true,clear=false;int reads=0;u32 read_psm=0;GSVector4i read_rect;bool ReadClearTarget(Target*,GSVector4i){return clear;}bool ReadWithStatus(Target*t,GSVector4i r,bool sync){++reads;read_psm=t->m_TEX0.PSM;read_rect=r;return read_ok;}bool TryReadRawDepthTarget(Target*,u32,u32,u32,const GSVector4i&,bool);};
__HELPER__
int main(){for(int v=0;v<21;++v){GSTextureCache c;Target t,other;c.m_dst[1].push_back(&t);u32 bp=16,bw=1,psm=PSMZ32;GSVector4i r{1,1,2,2};bool ok=v==0||v==1||v==2||v==3||v==17||v==18||v==19;GSConfig.HWDownloadMode=GSHardwareDownloadMode::Enabled;
 if(v==1)t.m_TEX0.PSM=PSMZ16S;if(v==2)c.clear=true;if(v==3)t.m_drawn_since_read=GSVector4i::zero();
 if(v==4)c.read_ok=false;if(v==5)bp++;if(v==6)bw++;if(v==7)psm=PSMZ16;if(v==8)t.m_32_bits_fmt=true;if(v==9)t.m_type=0;if(v==10)t.m_TEX0.PSM=PSMZ32;if(v==11)t.m_valid_rgb=false;if(v==12)t.m_valid_alpha_low=false;if(v==13)t.m_valid_alpha_high=false;if(v==14)t.tex.fmt=GSTexture::Format::Color;
 if(v==15||v==16){c.m_dst[v==15?0:1].push_back(&other);other.overlap_old=v==15;other.overlap_read=v==16;}
 if(v==17||v==19)GSConfig.HWDownloadMode=GSHardwareDownloadMode::Asynchronous;
 if(v==18){other.overlap_old=other.overlap_read=false;c.m_dst[0].push_back(&other);}
 if(v==20)GSConfig.HWDownloadMode=GSHardwareDownloadMode::Disabled;
 bool actual=c.TryReadRawDepthTarget(&t,bp,bw,psm,r,v==19);if(actual!=ok){std::printf("FAIL policy %d\n",v);return 1;}
 if(t.m_drawn_since_read.rempty()!=(ok&&v!=17)){std::printf("FAIL pending %d\n",v);return 2;}
 if(c.reads&&(c.read_psm!=t.m_TEX0.PSM||c.read_rect.x!=0||c.read_rect.z!=4||c.read_rect.w!=8))return 3;
 if(v>=5&&v<=16&&(t.updates||c.reads))return 4;
 if(v==3&&(t.updates||c.reads))return 5;
 }
 std::puts("PASS 21 production raw-depth CPU-read policy/ordering/failure cases");}
'''.replace('__HELPER__', helper)
        compiler = shutil.which('g++') or shutil.which('clang++') or 'C:/msys64/ucrt64/bin/g++.exe'
        env = dict(os.environ)
        env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
        with tempfile.TemporaryDirectory(prefix='emucorex-raw-depth-') as temp:
            cpp = Path(temp) / 'test.cpp'
            exe = Path(temp) / 'test.exe'
            cpp.write_text(fixture, encoding='utf-8')
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
