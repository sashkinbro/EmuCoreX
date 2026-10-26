// SPDX-License-Identifier: GPL-3.0+
// Runs the production CommitClear body against GLES, with a minimal texture/state adapter.
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <array>
#include <utility>
#include <algorithm>
using u32=uint32_t;
#define GLAD_GL_VERSION_4_3 0
#define GLAD_GL_ES_VERSION_3_0 1
struct GSVector4 { union { float v[4]; uint32_t U32[4]; int32_t I32[4]; }; };
struct GSTexture {
 enum class Type { Texture=0, DepthStencil=1, RenderTarget=2, FeedbackTarget=6, FeedbackDepth=5, ShaderWriteTarget=14 };
 enum class State { Dirty, Cleared, Invalidated };
 Type usage; State state=State::Cleared; GLuint id; int size; bool depthColor=false;
 bool IsRenderTarget()const{return (int(usage)&2)!=0;} bool IsDepthStencil()const{return (int(usage)&1)!=0;}
 bool IsRenderTargetOrDepthStencil()const{return IsRenderTarget()||IsDepthStencil();}
 Type GetType()const{return usage;} State GetState()const{return state;} void SetState(State s){state=s;}
 std::pair<int,int> GetSize()const{return {size,size};} GLuint GetID()const{return id;}
 float GetClearDepth()const{return .25f;}
 GSVector4 GetClearForFormat()const { GSVector4 c; c.v[0]=.25f;c.v[1]=.5f;c.v[2]=.75f;c.v[3]=1;return c; }
 bool IsIntegerFormat()const{return false;} bool IsUnsignedFormat()const{return false;}
};
using GSTextureOGL=GSTexture;
namespace GLState { GSTexture* rt=nullptr; GSTexture* ds=nullptr; GSTexture* ds_as_rt=nullptr; bool depth_mask=true;u32 wrgba=15;GLuint fbo; }
struct OMColorMaskSelector {u32 mask;explicit OMColorMaskSelector(u32 v=15):mask(v){} };
struct GSDeviceOGL {
 GLuint m_fbo,m_fbo_write;
 void CommitClear(GSTexture*,bool);
 void OMSetFBO(GLuint f){glBindFramebuffer(GL_DRAW_FRAMEBUFFER,f);GLState::fbo=f;}
 void OMAttachRt(GSTexture* t){GLState::rt=t;glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t?t->id:0,0);}
 void OMAttachDs(GSTexture* t){GLState::ds=t;glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,t?t->id:0,0);}
 void OMSetColorMaskState(OMColorMaskSelector s=OMColorMaskSelector{}){GLState::wrgba=s.mask;glColorMaski(0,s.mask&1,s.mask&2,s.mask&4,s.mask&8);}
};
static bool GSIsHauntingGround() { return false; }
#include "CommitClear.inc"
static void Require(bool ok,const char* s){if(!ok){printf("ERROR %s GL=%x\n",s,glGetError());exit(2);}}
int main(){
 EGLDisplay d=eglGetDisplay(EGL_DEFAULT_DISPLAY);Require(eglInitialize(d,nullptr,nullptr),"EGL");
 EGLint a[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT,EGL_NONE};EGLConfig cfg;EGLint count;eglChooseConfig(d,a,&cfg,1,&count);
 EGLint ca[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE},sa[]={EGL_WIDTH,8,EGL_HEIGHT,8,EGL_NONE};auto ctx=eglCreateContext(d,cfg,EGL_NO_CONTEXT,ca);auto surf=eglCreatePbufferSurface(d,cfg,sa);Require(eglMakeCurrent(d,surf,surf,ctx),"current");
 GSDeviceOGL dev;glGenFramebuffers(1,&dev.m_fbo);glGenFramebuffers(1,&dev.m_fbo_write);
 int failures=0,cases=0;
 for(int usage:{2,6,14})for(bool write:{false,true})for(int mode:{0,1,2}) {
  // mode 1 retains a smaller MRT; mode 2 clears depth-as-color in slot 1.
  GLuint ids[2];glGenTextures(2,ids);
  for(int i=0;i<2;i++){glBindTexture(GL_TEXTURE_2D,ids[i]);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA8,i?4:8,i?4:8);}
  GSTexture t{GSTexture::Type(usage),GSTexture::State::Cleared,ids[0],8};
  GSTexture other{GSTexture::Type::RenderTarget,GSTexture::State::Dirty,ids[1],4};
  dev.OMSetFBO(dev.m_fbo);dev.OMAttachRt(&t);dev.OMAttachDs(nullptr);GLState::ds_as_rt=nullptr;
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,0,0);
  GLenum db[]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1};glDrawBuffers(1,db);dev.OMSetColorMaskState();glDisable(GL_SCISSOR_TEST);
  dev.OMAttachRt(&other);float green[]={0,1,0,1};glClearBufferfv(GL_COLOR,0,green);dev.OMAttachRt(&t);
  float red[]={1,0,0,1};glClearBufferfv(GL_COLOR,0,red);
  if(mode){
   GLState::ds_as_rt=mode==1?&other:&t;
   dev.OMAttachRt(mode==1?&t:&other);
   glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,GL_COLOR_ATTACHMENT1,GL_TEXTURE_2D,GLState::ds_as_rt->id,0);glDrawBuffers(2,db);
  }
  dev.OMSetColorMaskState(OMColorMaskSelector(1));GLState::depth_mask=false;glDepthMask(false);glEnable(GL_SCISSOR_TEST);glScissor(2,2,1,1);
  while(glGetError()){};
  dev.CommitClear(&t,write);GLenum error=glGetError();GLint bound;glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&bound);
  bool preserved=bound==int(dev.m_fbo)&&GLState::wrgba==1&&!GLState::depth_mask&&glIsEnabled(GL_SCISSOR_TEST);
  if(mode==2)preserved &= GLState::rt==&other;
  glBindFramebuffer(GL_READ_FRAMEBUFFER,dev.m_fbo_write);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,t.id,0);glReadBuffer(GL_COLOR_ATTACHMENT0);
  unsigned char pixels[8*8*4];glReadPixels(0,0,8,8,GL_RGBA,GL_UNSIGNED_BYTE,pixels);bool good=error==0&&preserved;
  const int expected[]={64,128,191,255};for(int i=0;i<256;i++)good &= std::abs(int(pixels[i])-expected[i%4])<=1;
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,other.id,0);
  unsigned char retained[4*4*4];glReadPixels(0,0,4,4,GL_RGBA,GL_UNSIGNED_BYTE,retained);
  for(int i=0;i<64;i++)good &= retained[i]==(i%4==1||i%4==3?255:0);
  good &= glGetError()==GL_NO_ERROR;
  printf("%s usage=%d write=%d mrt=%d error=%x state=%d first=%u,%u,%u,%u\n",good?"PASS":"FAIL",usage,write,mode,error,preserved,pixels[0],pixels[1],pixels[2],pixels[3]);failures+=!good;cases++;
  glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glDeleteTextures(2,ids);
 }
 printf("%d/%d production clear cases passed\n",cases-failures,cases);return failures?1:0;
}
