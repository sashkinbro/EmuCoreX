"""Compile actual target resize functions against tracked host device seams.

Checks the exact physical-size no-op predicate, slow-path effects, and that the
caller tests dimensions before entering the out-of-line resize implementation.
Native ARM64 code generation and graphics oracle runs remain separate checks.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from test_ee_store_address import function

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT / 'app/src/main/cpp/pcsx2/pcsx2/GS/Renderers/HW'

PRELUDE = r'''
#include <algorithm>
#include <cmath>
#include <cstdio>
#define __fi inline __attribute__((always_inline))
struct GSVector2i {
 int x,y;
 GSVector2i(int x=0,int y=0):x(x),y(y){}
 GSVector2i min(GSVector2i v) const {return {std::min(x,v.x),std::min(y,v.y)};}
};
struct GSVector4i {
 int x=0,y=0,z=0,w=0;
 static GSVector4i zero(){return {};}
 static GSVector4i loadh(GSVector2i s){return {0,0,s.x,s.y};}
};
struct GSVector4 {GSVector4i value;GSVector4(GSVector4i v):value(v){}};
enum {Nearest,Biln};
struct GSTexture {
 enum class State {Dirty,Cleared,Invalid};
 GSVector2i size; State state=State::Dirty; bool depth=false; int memory=100;
 unsigned color=0x81234567; float clear_depth=0.75f;
 GSVector2i GetSize() const{return size;}
 State GetState() const{return state;}
 bool IsDepthLike()const{return depth;}
 unsigned GetClearColor()const{return color;}
 float GetClearDepth()const{return clear_depth;}
 int GetMemUsage()const{return memory;}
};
struct Device {
 int allocations=0,copies=0,stretches=0,color_clears=0,depth_clears=0,invalidates=0,recycles=0;
 bool fail=false,clear=false;int filter=-1;GSVector4i rect;
 GSTexture* CreateCompatible(GSTexture* old,GSVector2i size,bool clear,bool){
  ++allocations;this->clear=clear;
  if(fail)return nullptr;
  return new GSTexture{size,old->state,old->depth,200};
 }
 void StretchRectAuto(GSTexture*,GSTexture*,GSVector4 r,int f){++stretches;rect=r.value;filter=f;}
 void CopyRect(GSTexture*,GSTexture*,GSVector4i r,int,int){++copies;rect=r;}
 void ClearDepth(GSTexture*,float d){++depth_clears;if(d!=0.75f)std::abort();}
 void ClearRenderTarget(GSTexture*,unsigned c){++color_clears;if(c!=0x81234567)std::abort();}
 void InvalidateRenderTarget(GSTexture*){++invalidates;}
 void Recycle(GSTexture*){++recycles;}
} device;
Device* g_gs_device=&device;
struct Renderer {float GetUpscaleMultiplier() const{return 2.0f;}} renderer;
Renderer* g_gs_renderer=&renderer;
struct {template<class... T> void Error(const char*,T...) {}} Console;
bool PreferReusedLabelledTexture(){return false;}
class GSTextureCache {
public:
 int m_target_memory_usage=100;
 class Target {
 public:
 GSTexture* m_texture;GSVector2i m_unscaled_size;float m_scale;bool m_downscaled;
 int debug_names=0;
 @RESIZE@
 @SLOW_DECL@
 void UpdateTextureDebugName(){++debug_names;}
 };
 @SCALE@
} cache;
GSTextureCache* g_texture_cache=&cache;
extern "C" __attribute__((noinline)) bool ResizeProbe(GSTextureCache::Target* t,int w,int h){
 return t->ResizeTexture(w,h);
}
@DEFINITIONS@
'''

MAIN = r'''
int main(){
 int cases=0,failures=0;
 for(float scale:{0.5f,1.0f,1.25f,1.3f,1.5f,2.0f,2.5f})
 for(int width:{1,3,127}) for(int height:{1,5})
 for(bool stale_logical:{false,true})
 for(int axis:{0,1}) for(int physical_delta:{-1,0,1}) for(bool force:{false,true})
 for(bool fail:{false,true}) for(int state:{0,1,2})
 for(bool depth:{false,true}) for(bool recycle:{false,true})
 for(bool keep:{false,true}) for(bool downscaled:{false,true}){
  device={};device.fail=fail;cache.m_target_memory_usage=100;
  const GSVector2i requested(width,height);
  // Independent rounding oracle, deliberately never read from logical size.
  const int scaled_w=int(std::ceil(double(float(width)*scale)));
  const int scaled_h=int(std::ceil(double(float(height)*scale)));
  const GSVector2i physical(scaled_w+(axis==0?physical_delta:0),scaled_h+(axis==1?physical_delta:0));
  auto* old=new GSTexture{physical,GSTexture::State(state),depth};
  const GSVector2i logical(width+(stale_logical?17:0),height+(stale_logical?9:0));
  GSTextureCache::Target target{old,logical,scale,downscaled};
  const GSVector4i rect{1,2,3,4};
  const bool noop=physical_delta==0&&!force;
  const bool changed=!noop&&!fail;
  const bool result=target.ResizeTexture(width,height,recycle,force,rect,keep);
  bool ok=result==(noop||!fail);
  ok &= device.allocations==int(!noop);
  ok &= (target.m_texture==old)==!changed;
  ok &= target.debug_names==int(changed);
  ok &= target.m_unscaled_size.x==(changed?width:logical.x);
  ok &= target.m_unscaled_size.y==(changed?height:logical.y);
  ok &= target.m_scale==scale&&target.m_downscaled==downscaled;
  ok &= cache.m_target_memory_usage==(changed?(keep?300:200):100);
  ok &= device.recycles==int(changed&&!keep&&recycle);
  ok &= device.copies==int(changed&&state==0&&!depth&&!force);
  ok &= device.stretches==int(changed&&state==0&&(depth||force));
  ok &= device.depth_clears==int(changed&&state==1&&depth);
  ok &= device.color_clears==int(changed&&state==1&&!depth);
  ok &= device.invalidates==int(changed&&state==2);
  if(!noop)ok &= device.clear==(physical_delta<0);
  if(changed){
   ok &= target.m_texture->size.x==scaled_w&&target.m_texture->size.y==scaled_h;
   if(state==0){
    const GSVector4i expected=force?rect:GSVector4i{0,0,std::min(physical.x,scaled_w),std::min(physical.y,scaled_h)};
    ok &= device.rect.x==expected.x&&device.rect.y==expected.y&&device.rect.z==expected.z&&device.rect.w==expected.w;
    if(depth||force)ok &= device.filter==(depth&&downscaled&&scale<2.0f?Biln:Nearest);
   }
  }
  ++cases;
  if(!ok&&failures++<5)std::printf("FAIL scale=%g requested=%dx%d axis=%d delta=%d stale=%d force=%d fail=%d state=%d depth=%d recycle=%d keep=%d\n",scale,width,height,axis,physical_delta,stale_logical,force,fail,state,depth,recycle,keep);
  if(changed){delete target.m_texture;if(keep||recycle)delete old;}
  else delete old;
 }
 std::printf("GS production target resize: %d cases, %d failures\n",cases,failures);
 return failures?1:0;
}
'''


def production_program():
    header = (BASE / 'GSTextureCache.h').read_text()
    source = (BASE / 'GSTextureCache.cpp').read_text()
    slow_signature = 'bool GSTextureCache::Target::ResizeTextureImpl('
    if '__fi bool ResizeTexture(' in header:
        resize = function(header, '__fi bool ResizeTexture(')
        scale = function(header, 'static __fi GSVector2i ScaleRenderTargetSize(')
        slow_decl = re.search(r'\bbool ResizeTextureImpl\([^;]+;', header).group()
        definitions = function(source, slow_signature)
    else:
        resize = re.search(r'\bbool ResizeTexture\([^;]+;', header).group()
        scale = 'static GSVector2i ScaleRenderTargetSize(const GSVector2i&,float);'
        slow_decl = ''
        definitions = (function(source, 'GSVector2i GSTextureCache::ScaleRenderTargetSize(')
                       + '\n' + function(source, 'bool GSTextureCache::Target::ResizeTexture('))
    # Model the actual translation-unit call boundary: do not let a host compiler
    # accidentally inline the old entire allocation body or the new slow helper.
    definitions = definitions.replace('bool GSTextureCache::Target::',
                                      '__attribute__((noinline)) bool GSTextureCache::Target::')
    return (PRELUDE.replace('@SCALE@', scale).replace('@RESIZE@', resize)
            .replace('@SLOW_DECL@', slow_decl).replace('@DEFINITIONS@', definitions))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=shutil.which('g++') or shutil.which('clang++'))
    args = parser.parse_args()
    if not args.compiler:
        parser.error('pass --compiler with a C++17 host compiler')
    environment = os.environ.copy()
    environment['PATH'] = str(Path(args.compiler).resolve().parent) + os.pathsep + environment['PATH']
    with tempfile.TemporaryDirectory(prefix='emucorex-target-resize-') as tmp:
        cpp, asm, exe = Path(tmp)/'test.cpp', Path(tmp)/'test.s', Path(tmp)/('test.exe' if os.name=='nt' else 'test')
        cpp.write_text(production_program()+MAIN)
        command = [args.compiler, '-std=c++17', '-O2', str(cpp)]
        subprocess.run(command+['-o', str(exe)], check=True, env=environment)
        subprocess.run([str(exe)], check=True, env=environment)
        subprocess.run(command+['-S', '-o', str(asm)], check=True, env=environment)
        probe = asm.read_text().split('ResizeProbe:', 1)[1]
        probe = re.split(r'\n\s*(?:\.seh_endproc|\.size\s+ResizeProbe|\.cfi_endproc)', probe, maxsplit=1)[0]
        # x86 host codegen must branch on dimensions BEFORE any resize call/tail
        # jump. This fails on the old unconditional out-of-line call, while the
        # independent behavioral matrix above passes on both implementations.
        branch = re.search(r'\b(?:j(?:ne|e|nz|z)|b\.(?:ne|eq))\b', probe)
        call = re.search(r'\b(?:call\w*|jmp|bl|b)\s+[^\n]*ResizeTexture', probe)
        if not branch or (call and branch.start() > call.start()):
            raise AssertionError('ResizeProbe enters out-of-line resize before testing physical dimensions')
        print('GS resize caller codegen: physical-size branch precedes slow call')


if __name__ == '__main__':
    main()
