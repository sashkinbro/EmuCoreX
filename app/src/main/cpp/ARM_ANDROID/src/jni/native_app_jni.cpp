#include "emucorex/android_runtime.h"
#include "emucorex/android_crash_diagnostics.h"

#include "GS/GS.h"
#include "MTGS.h"
#include "common/FileSystem.h"
#include "common/HostSys.h"
#include "common/HTTPDownloaderCurl.h"
#include "arm64/OaknutHelpers-arm64.h"
#include "emucorex/retro_achievements_android.h"
#include "pcsx2/Achievements.h"
#include "pcsx2/BuildVersion.h"
#include "pcsx2/HangTrace.h"
#include "pcsx2/Host.h"
#include "pcsx2/JitProfiler.h"
#include "pcsx2/ps2/BiosTools.h"

#include <android/log.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <zip.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

using emucorex::android::AndroidRuntime;
using emucorex::android::JStringToString;
using emucorex::android::StringToJString;

namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";

std::mutex s_callback_mutex;
JavaVM* s_java_vm = nullptr;
jclass s_native_app_class = nullptr;

void LogUnsupported(const char* feature)
{
	__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "Unsupported native feature requested in Phase 1: %s", feature);
}

jbyteArray ReadSaveStateScreenshot(JNIEnv* env, const std::string& path)
{
	if (path.empty())
		return nullptr;

	int error = 0;
	zip_t* zip = zip_open(path.c_str(), ZIP_RDONLY, &error);
	if (!zip)
	{
		__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "failed to open savestate zip for screenshot: %s", path.c_str());
		return nullptr;
	}

	constexpr const char* SCREENSHOT_ENTRY = "Screenshot.png";
	zip_stat_t stat = {};
	if (zip_stat(zip, SCREENSHOT_ENTRY, 0, &stat) != 0 || stat.size == 0 ||
		stat.size > static_cast<zip_uint64_t>(32 * 1024 * 1024))
	{
		zip_close(zip);
		return nullptr;
	}

	zip_file_t* file = zip_fopen(zip, SCREENSHOT_ENTRY, 0);
	if (!file)
	{
		zip_close(zip);
		return nullptr;
	}

	std::vector<jbyte> bytes(static_cast<size_t>(stat.size));
	zip_uint64_t offset = 0;
	while (offset < stat.size)
	{
		const zip_int64_t read = zip_fread(file, bytes.data() + offset, stat.size - offset);
		if (read <= 0)
		{
			zip_fclose(file);
			zip_close(zip);
			return nullptr;
		}
		offset += static_cast<zip_uint64_t>(read);
	}

	zip_fclose(file);
	zip_close(zip);

	jbyteArray result = env->NewByteArray(static_cast<jsize>(bytes.size()));
	if (!result)
		return nullptr;
	env->SetByteArrayRegion(result, 0, static_cast<jsize>(bytes.size()), bytes.data());
	return result;
}

bool RunExecutableMemorySmokeTest()
{
#if defined(__aarch64__)
	const long page_size_long = sysconf(_SC_PAGESIZE);
	const size_t page_size = page_size_long > 0 ? static_cast<size_t>(page_size_long) : static_cast<size_t>(4096);
	void* code = mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (code == MAP_FAILED)
		return false;

	const uint32_t instructions[] = {0x52800540u, 0xd65f03c0u};
	std::memcpy(code, instructions, sizeof(instructions));
	HostSys::FlushInstructionCache(code, sizeof(instructions));

	if (mprotect(code, page_size, PROT_READ | PROT_EXEC) != 0)
	{
		munmap(code, page_size);
		return false;
	}

	using SmokeFn = int (*)();
	const int result = reinterpret_cast<SmokeFn>(code)();
	const bool ok = result == 42;
	mprotect(code, page_size, PROT_NONE);
	munmap(code, page_size);
	if (!ok)
		return false;

	void* oak_code = mmap(nullptr, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (oak_code == MAP_FAILED)
		return false;

	oakSetAsmPtr(oak_code, page_size);
	void* oak_start = oakStartBlock();
	oakEmitSmokeReturn42();
	oakEndBlock();

	if (mprotect(oak_code, page_size, PROT_READ | PROT_EXEC) != 0)
	{
		munmap(oak_code, page_size);
		return false;
	}

	const int oak_result = reinterpret_cast<SmokeFn>(oak_start)();
	const bool oak_ok = oak_result == 42;
	mprotect(oak_code, page_size, PROT_NONE);
	munmap(oak_code, page_size);
	return oak_ok;
#else
	return false;
#endif
}
}

namespace emucorex::android
{
void ConfigureNativeAppCallbacks(JNIEnv* env, jclass native_app_class)
{
	std::lock_guard lock(s_callback_mutex);
	env->GetJavaVM(&s_java_vm);

	if (s_native_app_class)
		env->DeleteGlobalRef(s_native_app_class);
	s_native_app_class = static_cast<jclass>(env->NewGlobalRef(native_app_class));
}

JavaVM* GetJavaVM()
{
	std::lock_guard lock(s_callback_mutex);
	return s_java_vm;
}

void DispatchPadVibration(int pad_index, float large_motor, float small_motor)
{
	JavaVM* java_vm = nullptr;
	jclass native_app_class = nullptr;
	{
		std::lock_guard lock(s_callback_mutex);
		java_vm = s_java_vm;
		native_app_class = s_native_app_class;
	}

	if (!java_vm || !native_app_class)
		return;

	JNIEnv* env = nullptr;
	bool did_attach = false;
	if (java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
	{
		if (java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
			return;
		did_attach = true;
	}

	jmethodID method = env->GetStaticMethodID(native_app_class, "onPadVibration", "(IFF)V");
	if (method)
		env->CallStaticVoidMethod(native_app_class, method, static_cast<jint>(pad_index),
			static_cast<jfloat>(large_motor), static_cast<jfloat>(small_motor));

	if (env->ExceptionCheck())
		env->ExceptionClear();

	if (did_attach)
		java_vm->DetachCurrentThread();
}

void DispatchRetroAchievementsNotification(const char* kind, const char* title, const char* message, const char* image_path)
{
	JavaVM* java_vm = nullptr;
	jclass native_app_class = nullptr;
	{
		std::lock_guard lock(s_callback_mutex);
		java_vm = s_java_vm;
		native_app_class = s_native_app_class;
	}

	if (!java_vm || !native_app_class)
		return;

	JNIEnv* env = nullptr;
	bool did_attach = false;
	if (java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
	{
		if (java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
			return;
		did_attach = true;
	}

	jmethodID method = env->GetStaticMethodID(native_app_class, "onRetroAchievementsNotification",
		"(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
	if (method)
	{
		jstring j_kind = env->NewStringUTF(kind ? kind : "");
		jstring j_title = env->NewStringUTF(title ? title : "");
		jstring j_message = env->NewStringUTF(message ? message : "");
		jstring j_image_path = env->NewStringUTF(image_path ? image_path : "");
		if (j_kind && j_title && j_message && j_image_path)
			env->CallStaticVoidMethod(native_app_class, method, j_kind, j_title, j_message, j_image_path);

		if (j_kind)
			env->DeleteLocalRef(j_kind);
		if (j_title)
			env->DeleteLocalRef(j_title);
		if (j_message)
			env->DeleteLocalRef(j_message);
		if (j_image_path)
			env->DeleteLocalRef(j_image_path);
	}

	if (env->ExceptionCheck())
		env->ExceptionClear();

	if (did_attach)
		java_vm->DetachCurrentThread();
}

bool DispatchRetroAchievementsSound(const char* path)
{
	JavaVM* java_vm = nullptr;
	jclass native_app_class = nullptr;
	{
		std::lock_guard lock(s_callback_mutex);
		java_vm = s_java_vm;
		native_app_class = s_native_app_class;
	}

	if (!java_vm || !native_app_class || !path || !*path)
		return false;

	JNIEnv* env = nullptr;
	bool did_attach = false;
	if (java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
	{
		if (java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
			return false;
		did_attach = true;
	}

	bool dispatched = false;
	jmethodID method = env->GetStaticMethodID(native_app_class, "onRetroAchievementsSound", "(Ljava/lang/String;)V");
	if (method)
	{
		jstring j_path = env->NewStringUTF(path);
		if (j_path)
		{
			env->CallStaticVoidMethod(native_app_class, method, j_path);
			dispatched = !env->ExceptionCheck();
			env->DeleteLocalRef(j_path);
		}
	}

	if (env->ExceptionCheck())
		env->ExceptionClear();

	if (did_attach)
		java_vm->DetachCurrentThread();
	return dispatched;
}

}

int FileSystem::OpenFDFileContent(const char* filename)
{
	if (!filename)
		return -1;

	JavaVM* java_vm = nullptr;
	jclass native_app_class = nullptr;
	{
		std::lock_guard lock(s_callback_mutex);
		java_vm = s_java_vm;
		native_app_class = s_native_app_class;
	}

	if (!java_vm || !native_app_class)
		return -1;

	JNIEnv* env = nullptr;
	bool did_attach = false;
	if (java_vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK)
	{
		if (java_vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env)
			return -1;
		did_attach = true;
	}

	jint fd = -1;
	jmethodID method = env->GetStaticMethodID(native_app_class, "openContentUri", "(Ljava/lang/String;)I");
	if (method)
	{
		jstring j_filename = env->NewStringUTF(filename);
		if (j_filename)
		{
			fd = env->CallStaticIntMethod(native_app_class, method, j_filename);
			env->DeleteLocalRef(j_filename);
		}
	}

	if (env->ExceptionCheck())
	{
		env->ExceptionClear();
		fd = -1;
	}

	if (did_attach)
		java_vm->DetachCurrentThread();

	return fd;
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_initialize(JNIEnv* env, jclass clazz, jstring path, jint api_ver) { emucorex::android::ConfigureNativeAppCallbacks(env, clazz); AndroidRuntime::Instance().Initialize(JStringToString(env, path), api_ver); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_reloadDataRoot(JNIEnv* env, jclass, jstring path) { AndroidRuntime::Instance().ReloadDataRoot(JStringToString(env, path)); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setSystemCaBundlePath(JNIEnv* env, jclass, jstring path) { HTTPDownloaderCurl::SetCABundlePath(JStringToString(env, path)); }
extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_NativeApp_getGameTitle(JNIEnv* env, jclass, jstring path) { return StringToJString(env, AndroidRuntime::Instance().GetGameTitle(JStringToString(env, path))); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_isBiosPath(JNIEnv* env, jclass, jstring path)
{
	u32 version = 0;
	u32 region = 0;
	std::string description;
	std::string zone;
	return IsBIOS(JStringToString(env, path).c_str(), version, description, region, zone) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_isBiosFd(JNIEnv*, jclass, jint fd)
{
	if (fd < 0)
		return JNI_FALSE;

	char path[64];
	std::snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
	u32 version = 0;
	u32 region = 0;
	std::string description;
	std::string zone;
	const bool valid = IsBIOS(path, version, description, region, zone);
	close(fd);
	return valid ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setPadVibration(JNIEnv*, jclass, jboolean enabled) { AndroidRuntime::Instance().SetSetting("InputSources", "PadVibration", "bool", enabled == JNI_TRUE ? "true" : "false"); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setPerformanceMetricsEnabled(JNIEnv*, jclass, jboolean visible, jboolean detailed, jboolean gpu_timing)
{
	emucorex::android::SetPerformanceMetricsCallbackEnabled(
		visible == JNI_TRUE, detailed == JNI_TRUE, gpu_timing == JNI_TRUE);
}
extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_NativeApp_getPerformanceMetricsSnapshot(JNIEnv* env, jclass)
{
	return StringToJString(env, emucorex::android::GetPerformanceMetricsSnapshot());
}
extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_NativeApp_getCoreVersion(JNIEnv* env, jclass)
{
	return StringToJString(env, BuildVersion::GitRev ? BuildVersion::GitRev : "Unknown");
}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_queueGsDump(JNIEnv*, jclass, jint frames)
{
	const u32 dump_frames = frames > 0 ? static_cast<u32>(frames) : 0;
	Host::RunOnGSThread([dump_frames]() {
		GSConfig.GSDumpCompression = GSDumpCompressionMethod::Uncompressed;
		GSQueueSnapshot(std::string(), dump_frames);
	});
}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setPadButton(JNIEnv*, jclass, jint pad_index, jint index, jint range, jboolean pressed) { AndroidRuntime::Instance().SetPadButton(pad_index, index, range, pressed == JNI_TRUE); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setPadPressureModifierAmount(JNIEnv*, jclass, jint amount_percent) { AndroidRuntime::Instance().SetPadPressureModifierAmount(amount_percent); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_onHostKeyEvent(JNIEnv*, jclass, jint key_code, jboolean pressed) { AndroidRuntime::Instance().OnHostKeyEvent(key_code, pressed == JNI_TRUE); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_onHostMousePosition(JNIEnv*, jclass, jfloat x, jfloat y) { AndroidRuntime::Instance().OnHostMousePosition(x, y); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_onHostMouseButton(JNIEnv*, jclass, jint button, jboolean pressed) { AndroidRuntime::Instance().OnHostMouseButton(button, pressed == JNI_TRUE); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_onHostMouseWheel(JNIEnv*, jclass, jfloat horizontal, jfloat vertical) { AndroidRuntime::Instance().OnHostMouseWheel(horizontal, vertical); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_resetKeyStatus(JNIEnv*, jclass) { AndroidRuntime::Instance().ResetKeyStatus(); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_resetPadState(JNIEnv*, jclass, jint pad_index) { AndroidRuntime::Instance().ResetPadState(pad_index); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setAspectRatio(JNIEnv*, jclass, jint type)
{
	const char* value = "Auto 4:3/3:2";
	switch (type)
	{
		case 2: value = "4:3"; break;
		case 3: value = "16:9"; break;
		case 4: value = "10:7"; break;
		default: break;
	}
	AndroidRuntime::Instance().SetSetting("EmuCore/GS", "AspectRatio", "string", value);
}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_renderUpscalemultiplier(JNIEnv*, jclass, jfloat value) { AndroidRuntime::Instance().SetSetting("EmuCore/GS", "upscale_multiplier", "float", std::to_string(value)); }
extern "C" JNIEXPORT jint JNICALL Java_com_sbro_emucorex_core_NativeApp_getMaxUpscaleMultiplier(JNIEnv*, jclass, jint renderer)
{
	const GSRendererType resolved_renderer = renderer <= 0 ? GSRendererType::OGL : static_cast<GSRendererType>(renderer);
	const std::vector<GSAdapterInfo> adapters = GSGetAdapterInfo(resolved_renderer);
	u32 max_multiplier = 12;
	if (!adapters.empty())
		max_multiplier = std::max<u32>(adapters.front().max_upscale_multiplier, 1);
	return static_cast<jint>(max_multiplier);
}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_renderGpu(JNIEnv*, jclass, jint value) { AndroidRuntime::Instance().SetSetting("EmuCore/GS", "Renderer", "int", std::to_string(value)); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setCustomDriverPath(JNIEnv* env, jclass, jstring path) { AndroidRuntime::Instance().SetSetting("EmuCoreX", "CustomDriverPath", "string", JStringToString(env, path)); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setNativeLibraryDir(JNIEnv* env, jclass, jstring path) { AndroidRuntime::Instance().SetNativeLibraryDir(JStringToString(env, path)); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_beginSettingsBatch(JNIEnv*, jclass) { AndroidRuntime::Instance().BeginSettingsBatch(); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_endSettingsBatch(JNIEnv*, jclass) { AndroidRuntime::Instance().EndSettingsBatch(); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setSetting(JNIEnv* env, jclass, jstring section, jstring key, jstring type, jstring value) { AndroidRuntime::Instance().SetSetting(JStringToString(env, section), JStringToString(env, key), JStringToString(env, type), JStringToString(env, value)); }
extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_NativeApp_getSetting(JNIEnv* env, jclass, jstring section, jstring key, jstring) { return StringToJString(env, AndroidRuntime::Instance().GetSetting(JStringToString(env, section), JStringToString(env, key))); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setFrameSkip(JNIEnv*, jclass, jint frames) { GSSetManualFrameSkip(static_cast<u32>(std::clamp<jint>(frames, 0, 4))); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setFrameLimitEnabled(JNIEnv*, jclass, jboolean enabled) { AndroidRuntime::Instance().SetFrameLimitEnabled(enabled == JNI_TRUE); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setTurboModeEnabled(JNIEnv*, jclass, jboolean enabled) { AndroidRuntime::Instance().SetTurboModeEnabled(enabled == JNI_TRUE); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_reloadPatches(JNIEnv*, jclass) { AndroidRuntime::Instance().ReloadPatches(); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_onNativeSurfaceCreated(JNIEnv*, jclass) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_onNativeSurfaceChanged(JNIEnv* env, jclass, jobject surface, jint width, jint height)
{
	ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;
	AndroidRuntime::Instance().SetNativeSurface(window, width, height);
}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_onNativeSurfaceDestroyed(JNIEnv*, jclass) { AndroidRuntime::Instance().ClearSurface(); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_runVMThread(JNIEnv* env, jclass, jstring path) { return AndroidRuntime::Instance().StartVm(JStringToString(env, path), false, 0) ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT jint JNICALL Java_com_sbro_emucorex_core_NativeApp_runBootSmokeProbe(JNIEnv* env, jclass, jstring path, jint steps) { return AndroidRuntime::Instance().StartVm(JStringToString(env, path), false, steps) ? 1 : 0; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_runJitExecutableMemorySmokeTest(JNIEnv*, jclass) { return RunExecutableMemorySmokeTest() ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_bootElf(JNIEnv* env, jclass, jstring path) { return AndroidRuntime::Instance().StartVm(JStringToString(env, path), true, 0) ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_bootIrx(JNIEnv* env, jclass, jstring path) { return AndroidRuntime::Instance().StartVm(JStringToString(env, path), false, 0, true) ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_pause(JNIEnv*, jclass) { AndroidRuntime::Instance().Pause(); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_resume(JNIEnv*, jclass) { AndroidRuntime::Instance().Resume(); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_shutdown(JNIEnv*, jclass) { AndroidRuntime::Instance().Shutdown(); }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_refreshBIOS(JNIEnv*, jclass) { AndroidRuntime::Instance().SetSetting("EmuCoreX", "RefreshBIOS", "bool", "true"); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_hasValidVm(JNIEnv*, jclass) { return AndroidRuntime::Instance().HasValidVm() ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_NativeApp_getGameSerial(JNIEnv* env, jclass) { return StringToJString(env, AndroidRuntime::Instance().GetGameSerial()); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_saveStateToSlot(JNIEnv*, jclass, jint slot) { return AndroidRuntime::Instance().SaveStateToSlot(slot) ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_loadStateFromSlot(JNIEnv*, jclass, jint slot) { return AndroidRuntime::Instance().LoadStateFromSlot(slot) ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_NativeApp_getSaveStatePathForFile(JNIEnv* env, jclass, jstring path, jint slot) { return StringToJString(env, AndroidRuntime::Instance().GetSaveStatePathForFile(JStringToString(env, path), slot)); }
extern "C" JNIEXPORT jbyteArray JNICALL Java_com_sbro_emucorex_core_NativeApp_getSaveStateScreenshot(JNIEnv* env, jclass, jstring path)
{
	return ReadSaveStateScreenshot(env, JStringToString(env, path));
}
extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_NativeApp_getRetroAchievementGameData(JNIEnv* env, jclass, jstring)
{
	const std::string json = Achievements::GetActiveGameDataJSON();
	return json.empty() ? nullptr : StringToJString(env, json);
}
extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_NativeApp_getRetroAchievementsAccountData(JNIEnv* env, jclass)
{
	const std::string json = Achievements::GetAccountProgressJSON();
	return json.empty() ? nullptr : StringToJString(env, json);
}
extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorex_core_NativeApp_listMemoryCards(JNIEnv* env, jclass) { return StringToJString(env, AndroidRuntime::Instance().ListMemoryCards()); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_createMemoryCard(JNIEnv* env, jclass, jstring name, jint type, jint file_type) { return AndroidRuntime::Instance().CreateMemoryCard(JStringToString(env, name), type, file_type) ? JNI_TRUE : JNI_FALSE; }
extern "C" JNIEXPORT jint JNICALL Java_com_sbro_emucorex_core_NativeApp_convertIsoToChd(JNIEnv*, jclass, jstring)
{
	LogUnsupported("native ISO to CHD conversion");
	return -1;
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_startJitProfiler(JNIEnv*, jclass)
{
	JitProfiler::Start();
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_stopJitProfiler(JNIEnv*, jclass)
{
	JitProfiler::Stop();
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_isJitProfilerActive(JNIEnv*, jclass)
{
	return JitProfiler::IsActive() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_startHangTrace(JNIEnv*, jclass)
{
	HangTrace::Start();
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_stopHangTrace(JNIEnv*, jclass)
{
	HangTrace::Stop();
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_NativeApp_isHangTraceActive(JNIEnv*, jclass)
{
	return HangTrace::IsActive() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_NativeApp_setNativeCrashLogFilePath(JNIEnv* env, jclass, jstring path)
{
	emucorex::android::SetNativeCrashLogFilePath(JStringToString(env, path).c_str());
	emucorex::android::InstallNativeCrashSignalHandler();
}
