// SPDX-License-Identifier: GPL-3.0+
#include "common/TextureCodec.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <jni.h>

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorex_core_NativeApp_convertTexture(JNIEnv *env, jclass,
                                                     jstring source,
                                                     jstring destination,
                                                     jint block) {
  if (!source || !destination)
    return env->NewStringUTF("Missing texture path");
  const char *src = env->GetStringUTFChars(source, nullptr);
  if (!src)
    return nullptr;
  const char *dst = env->GetStringUTFChars(destination, nullptr);
  if (!dst) {
    env->ReleaseStringUTFChars(source, src);
    return nullptr;
  }
  std::string error;
  const bool success = TextureCodec::Convert(src, dst, block, 4, error);
  env->ReleaseStringUTFChars(source, src);
  env->ReleaseStringUTFChars(destination, dst);
  return success ? nullptr : env->NewStringUTF(error.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_validateOptimizedTexture(JNIEnv *env,
                                                               jclass,
                                                               jstring path,
                                                               jint block) {
  if (!path)
    return false;
  const char *value = env->GetStringUTFChars(path, nullptr);
  if (!value)
    return false;
  TextureCodec::Image image;
  std::string error;
  bool valid =
      TextureCodec::Read(value, image, false, error) && image.block == block;
  env->ReleaseStringUTFChars(path, value);
  return valid;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorex_core_NativeApp_supportsAstcTextures(JNIEnv *, jclass) {
  // Runs on an IO worker with no current rendering context. Never terminate the
  // display: the emulator or another graphics component may also be using it.
  if (eglGetCurrentContext() != EGL_NO_CONTEXT)
    return false;
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
    return false;
  const EGLenum previousApi = eglQueryAPI();
  eglBindAPI(EGL_OPENGL_ES_API);
  const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                               EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                               EGL_NONE};
  EGLConfig config{};
  EGLint count = 0;
  if (!eglChooseConfig(display, attributes, &config, 1, &count) || count == 0) {
    eglBindAPI(previousApi);
    return false;
  }
  const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext context =
      eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
  const EGLint surfaceAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  EGLSurface surface =
      eglCreatePbufferSurface(display, config, surfaceAttributes);
  bool supported = false;
  if (context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE &&
      eglMakeCurrent(display, surface, surface, context)) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; i++) {
      const auto *extension =
          reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i));
      if (extension &&
          !std::strcmp(extension, "GL_KHR_texture_compression_astc_ldr"))
        supported = true;
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  }
  if (surface != EGL_NO_SURFACE)
    eglDestroySurface(display, surface);
  if (context != EGL_NO_CONTEXT)
    eglDestroyContext(display, context);
  eglBindAPI(previousApi);
  return supported;
}
