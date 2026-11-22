package com.sbro.emucorex.ps2ui

import android.content.Context
import android.opengl.GLSurfaceView
import android.util.Log
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * GLSurfaceView renderer driving the native PS2 BIOS UI scene.
 */
class Ps2ShellSurfaceRenderer(
    context: Context,
    private val onReady: (() -> Unit)? = null,
) : GLSurfaceView.Renderer {
    private val appContext = context.applicationContext
    private var initialized = false

    override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
        try {
            Ps2ShellAudio.initialize(appContext)
            initialized = Ps2ShellNative.nativeInit()
            if (!initialized) {
                Log.e(TAG, "PS2 native UI failed to initialize")
            } else {
                onReady?.invoke()
            }
        } catch (error: Throwable) {
            Log.e(TAG, "PS2 surface initialization failed", error)
        }
    }

    override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
        if (initialized) {
            Ps2ShellNative.nativeResize(width, height)
        }
    }

    override fun onDrawFrame(gl: GL10?) {
        if (initialized) {
            Ps2ShellNative.nativeDraw()
        }
    }

    private companion object {
        const val TAG = "Ps2ShellSurface"
    }
}
