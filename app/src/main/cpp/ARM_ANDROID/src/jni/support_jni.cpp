#include <android/log.h>
#include <jni.h>

namespace
{
constexpr const char* LOG_TAG = "EmuCoreX";

void LogUnsupported(const char* feature)
{
	__android_log_print(ANDROID_LOG_WARN, LOG_TAG, "Unsupported native feature requested in Phase 1: %s", feature);
}
}

extern "C" JNIEXPORT jint JNICALL Java_com_sbro_emucorex_core_utils_SDLControllerManager_nativeSetupJNI(JNIEnv*, jclass) { return 0; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_utils_SDLControllerManager_onNativePadDown(JNIEnv*, jclass, jint, jint) { return JNI_FALSE; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_sbro_emucorex_core_utils_SDLControllerManager_onNativePadUp(JNIEnv*, jclass, jint, jint) { return JNI_FALSE; }
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_SDLControllerManager_onNativeJoy(JNIEnv*, jclass, jint, jint, jfloat) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_SDLControllerManager_onNativeHat(JNIEnv*, jclass, jint, jint, jint, jint) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_SDLControllerManager_nativeAddJoystick(JNIEnv*, jclass, jint, jstring, jstring, jint, jint, jint, jint, jint, jint, jboolean) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_SDLControllerManager_nativeRemoveJoystick(JNIEnv*, jclass, jint) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_SDLControllerManager_nativeAddHaptic(JNIEnv*, jclass, jint, jstring) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_utils_SDLControllerManager_nativeRemoveHaptic(JNIEnv*, jclass, jint) {}

extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_hid_HIDDeviceManager_HIDDeviceRegisterCallback(JNIEnv*, jclass) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_hid_HIDDeviceManager_HIDDeviceReleaseCallback(JNIEnv*, jclass) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_hid_HIDDeviceManager_HIDDeviceConnected(JNIEnv*, jclass, jint, jstring, jint, jint, jstring, jint, jstring, jstring, jint, jint, jint, jint, jboolean) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_hid_HIDDeviceManager_HIDDeviceOpenPending(JNIEnv*, jclass, jint) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_hid_HIDDeviceManager_HIDDeviceOpenResult(JNIEnv*, jclass, jint, jboolean) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_hid_HIDDeviceManager_HIDDeviceDisconnected(JNIEnv*, jclass, jint) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_hid_HIDDeviceManager_HIDDeviceInputReport(JNIEnv*, jclass, jint, jbyteArray) {}
extern "C" JNIEXPORT void JNICALL Java_com_sbro_emucorex_core_hid_HIDDeviceManager_HIDDeviceReportResponse(JNIEnv*, jclass, jint, jbyteArray) {}
