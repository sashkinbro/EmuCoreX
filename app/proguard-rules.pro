# Keep source/line information so Crashlytics and mapped stack traces stay useful.
-keepattributes SourceFile,LineNumberTable,*Annotation*,InnerClasses,EnclosingMethod,Signature

# Native JNI entry points and Java callbacks looked up from C++ must keep stable
# class and method names. This protects the emulator core bridge, RetroAchievements,
# Discord, and SDL/HID integration from obfuscation-related breakage.
-keepclasseswithmembers class com.sbro.emucorex.** {
    native <methods>;
}

-keep class com.sbro.emucorex.core.NativeApp { *; }
-keep class com.sbro.emucorex.core.utils.RetroAchievementsBridge { *; }
-keep class com.sbro.emucorex.core.utils.DiscordBridge { *; }
-keep class com.sbro.emucorex.core.utils.SDLControllerManager { *; }
-keep class com.sbro.emucorex.core.hid.HIDDeviceManager { *; }

# Methods called from native via FindClass/GetStaticMethodID.
-keepclassmembers class com.sbro.emucorex.core.NativeApp {
    public static void onPadVibration(int, float, float);
    public static void ensureResourceSubdirectoryCopied(java.lang.String);
    public static void nativeLog(java.lang.String);
    public static void onPerformanceMetrics(java.lang.String, float, float);
    public static int openContentUri(java.lang.String);
}

-keepclassmembers class com.sbro.emucorex.core.utils.RetroAchievementsBridge {
    public static void notifyLoginRequested(int);
    public static void notifyLoginSuccess(java.lang.String, int, int, int);
    public static void notifyStateChanged(boolean, boolean, java.lang.String, java.lang.String, java.lang.String, int, int, int, boolean, boolean, boolean, java.lang.String, java.lang.String, java.lang.String, int, int, int, int, int, boolean, boolean, boolean);
    public static void notifyHardcoreModeChanged(boolean);
}

-keepclassmembers class com.sbro.emucorex.core.utils.SDLControllerManager {
    public static void pollInputDevices();
    public static void pollHapticDevices();
    public static void hapticRun(int, float, int);
    public static void hapticRumble(int, float, float, int);
    public static void hapticStop(int);
}

-keepclassmembers class com.sbro.emucorex.core.hid.HIDDeviceManager {
    public static boolean initialize(boolean, boolean);
    public static boolean openDevice(int);
    public static int writeReport(int, byte[], boolean);
    public static boolean readReport(int, byte[], boolean);
    public static void closeDevice(int);
}
