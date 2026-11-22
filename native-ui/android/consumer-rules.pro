# Consumer R8/ProGuard rules for the :native-ui:android module (libemucorex_ps2ui.so).
# JNI_OnLoad in ps2_jni.cpp resolves these classes and their @JvmStatic methods by name,
# and Ps2ShellNative exports external native entry points.
-keep class com.sbro.emucorex.ps2ui.Ps2ShellNative { *; }
-keep class com.sbro.emucorex.ps2ui.Ps2ShellHost { *; }
-keep class com.sbro.emucorex.ps2ui.Ps2TextRasterizer { *; }
-keep class com.sbro.emucorex.ps2ui.Ps2ImageDecoder { *; }
-keep class com.sbro.emucorex.ps2ui.Ps2ShellAudio { *; }
-keep class com.sbro.emucorex.ps2ui.Ps2ShellSurfaceRenderer { *; }
