# EmuCoreX release/R8 rules.
#
# The release build uses minification and resource shrinking. Keep this file focused on
# runtime entry points that are reached from Android manifests, JNI, Kotlin serialization,
# or library reflection. Do not blanket-keep the whole app; that hides real shrinker issues.

# Keep useful crash context for native-adjacent stack traces.
-keepattributes SourceFile,LineNumberTable
-keepattributes Signature,*Annotation*,InnerClasses,EnclosingMethod

# Android framework entry points. Manifest components are normally handled by AGP, but
# keeping the app/activity names explicit protects startup if the manifest or tooling
# changes around the custom native bootstrap.
-keep class com.sbro.emucorex.EmuCoreXApp { *; }
-keep class com.sbro.emucorex.MainActivity { *; }

# FileProvider is used by the in-app updater to hand the downloaded APK to Android's
# package installer.
-keep class androidx.core.content.FileProvider { *; }

# JNI entry points. Native libraries use static JNI names such as
# Java_com_sbro_emucorex_core_NativeApp_initialize, so classes and native method names
# must remain stable.
-keepclasseswithmembers,includedescriptorclasses class com.sbro.emucorex.** {
    native <methods>;
}

-keep,includedescriptorclasses class com.sbro.emucorex.core.NativeApp { *; }

# WorkManager persists these class names across process restarts and app updates.
# Keep the reflective constructors and stable names, while allowing method optimization.
-keep,allowoptimization class com.sbro.emucorex.data.Texture*Worker {
    public <init>(android.content.Context, androidx.work.WorkerParameters);
}
-keep,includedescriptorclasses class com.sbro.emucorex.core.utils.RetroAchievementsBridge { *; }
-keep,includedescriptorclasses class com.sbro.emucorex.discord.DiscordNative { *; }
-keep class com.discord.socialsdk.** { *; }
-keep,includedescriptorclasses class com.sbro.emucorex.core.utils.SDLControllerManager { *; }
-keep,includedescriptorclasses class com.sbro.emucorex.core.hid.HIDDeviceManager { *; }

# DEV9 reads AdapterInfo and RouteInfo fields with JNI GetFieldID using their exact
# source names. Keep the complete bridge model stable in minified release builds.
-keep,includedescriptorclasses class com.sbro.emucorex.core.utils.NetworkAdapterCollector { *; }
-keep,includedescriptorclasses class com.sbro.emucorex.core.utils.NetworkAdapterCollector$AdapterInfo { *; }
-keep,includedescriptorclasses class com.sbro.emucorex.core.utils.NetworkAdapterCollector$AdapterInfo$RouteInfo { *; }

# Shared gamepad UI navigation is reached from Activity input callbacks and Compose
# registration lambdas. Keep the small common Gamepad* surface explicit so release
# minification cannot fold away router singletons or top-level Compose helper entry points.
-keep,includedescriptorclasses class com.sbro.emucorex.ui.common.Gamepad* { *; }


# Methods looked up from C++ with GetStaticMethodID/CallStatic* must keep their Java
# names and signatures even when the surrounding Kotlin code is optimized.
-keepclassmembers,includedescriptorclasses class com.sbro.emucorex.core.NativeApp {
    public static void onPadVibration(int, float, float);
    public static int openContentUri(java.lang.String);
}

-keepclassmembers,includedescriptorclasses class com.sbro.emucorex.core.utils.RetroAchievementsBridge {
    public static void notifyLoginRequested(int);
    public static void notifyLoginSuccess(java.lang.String, int, int, int);
    public static void notifyStateChanged(boolean, boolean, java.lang.String, java.lang.String, java.lang.String, int, int, int, boolean, boolean, boolean, java.lang.String, java.lang.String, java.lang.String, int, int, int, int, int, boolean, boolean, boolean);
    public static void notifyHardcoreModeChanged(boolean);
    public static void notifySettingsChanged(java.lang.String, java.lang.String, java.lang.String);
}

-keepclassmembers,includedescriptorclasses class com.sbro.emucorex.core.utils.SDLControllerManager {
    public static void pollInputDevices();
    public static void pollHapticDevices();
    public static void hapticRun(int, float, int);
    public static void hapticRumble(int, float, float, int);
    public static void hapticStop(int);
}

-keepclassmembers,includedescriptorclasses class com.sbro.emucorex.core.hid.HIDDeviceManager {
    public static boolean initialize(boolean, boolean);
    public static boolean openDevice(int);
    public static int writeReport(int, byte[], boolean);
    public static boolean readReport(int, byte[], boolean);
    public static void closeDevice(int);
}

# The app compiles SDL's Java glue directly from the bundled SDL Android project.
# SDL's native JNI_OnLoad registers classes by hard-coded names such as
# org/libsdl/app/SDLActivity and org/libsdl/app/SDLInputConnection. Keep the full
# glue package stable so optimized release builds cannot rename a class or method
# that native code resolves by string.
-keep,includedescriptorclasses class org.libsdl.app.** { *; }

# Typed Navigation Compose routes are kotlinx-serializable. Their generated serializers
# are used by navigation to encode/decode route arguments, including after process death.
-keep,includedescriptorclasses class com.sbro.emucorex.navigation.** { *; }
-keep,includedescriptorclasses class com.sbro.emucorex.navigation.**$$serializer { *; }
-keepclassmembers class com.sbro.emucorex.navigation.** {
    public static ** serializer(...);
    public static **[] values();
    public static ** valueOf(java.lang.String);
}

# Keep Kotlin serialization generated companions/serializers generally. This is narrow
# enough for release, but avoids R8 removing serializer singletons that are reached
# through generated lookup paths.
-keepclassmembers class ** {
    *** Companion;
}
-keepclassmembers class **$Companion {
    kotlinx.serialization.KSerializer serializer(...);
}
-keep,includedescriptorclasses class **$$serializer { *; }
-keepclassmembers class **$$serializer {
    public static ** INSTANCE;
}

# DataStore stores preferences by string keys, so no app classes need to be kept for it.
# JSON parsing in the app is manual org.json; no Gson/Moshi reflection model rules needed.

# Firebase and AndroidX ship their own consumer rules. These dontwarn entries
# keep release output focused when optional integrations are absent from a variant.
-dontwarn com.google.firebase.**
-dontwarn com.google.android.gms.**
-dontwarn com.pierfrancescosoffritti.androidyoutubeplayer.**
-dontwarn kotlinx.coroutines.debug.**

# WebRTC and jni_zero (used by WebRTC natively)
-keep class org.webrtc.** { *; }
-keep class org.jni_zero.** { *; }
