package com.sbro.emucorex.ps2ui

/**
 * JNI bridge for the native GLES 3.0 PlayStation 2 BIOS shell renderer.
 * Nothing in this module touches the emulation core.
 */
object Ps2ShellNative {
    init {
        System.loadLibrary("emucorex_ps2ui")
    }

    external fun nativeInit(): Boolean

    external fun nativeSetUiScale(scale: Float)

    external fun nativeResize(width: Int, height: Int)

    external fun nativeDraw()

    external fun nativeShutdown()

    external fun nativeKey(key: Int)

    external fun nativeTouchDown(normX: Float, normY: Float)

    external fun nativeTouchMove(normX: Float, normY: Float)

    external fun nativeTouchUp(normX: Float, normY: Float)

    external fun nativeTouchCancel()

    external fun nativeTouchTwoFingerBack()

    external fun nativeTouchLongPress(normX: Float, normY: Float)

    external fun nativeSetStrings(keys: Array<String>, values: Array<String>)

    external fun nativeSetGames(
        titles: Array<String>,
        paths: Array<String>,
        serials: Array<String>,
        covers: Array<String>,
        sizes: Array<String>,
        dates: Array<String>,
        fileSizesBytes: LongArray,
        saveCounts: IntArray,
        latestSaveSlots: IntArray,
        compatStatuses: Array<String>,
    )

    external fun nativeSetSaves(
        titles: Array<String>,
        slots: Array<String>,
        slotNums: IntArray,
        dates: Array<String>,
        sizes: Array<String>,
        serials: Array<String>,
        previews: Array<String>,
        savePaths: Array<String>,
        gamePaths: Array<String>,
    )

    external fun nativeSetMemoryCards(
        slot1Name: String,
        slot1Meta: String,
        slot1Status: String,
        slot2Name: String,
        slot2Meta: String,
        slot2Status: String,
        cardNames: Array<String>,
        cardSizes: Array<String>,
        cardDates: Array<String>,
        cardIsFolder: BooleanArray,
        cardIsDefault: BooleanArray,
    )

    external fun nativeSetMemoryCardContents(
        slot: Int,
        cardName: String,
        freeKb: Int,
        totalKb: Int,
        dirNames: Array<String>,
        titles: Array<String>,
        sizesKb: IntArray,
        dates: Array<String>,
        colorsRgb: IntArray,
        iconMeshes: Array<FloatArray?>,
    )

    external fun nativeSetSettings(
        theme: Int,
        aspectMode: Int,
        orbsMode: Int,
        towersMode: Int,
        scanlinesMode: Int,
        bootAnimMode: Int,
        keyTone: Boolean,
        ambientSound: Boolean,
        swapConfirm: Boolean,
        touchControls: Boolean,
        time24h: Boolean,
        dateFormat: Int,
        keepScreenOn: Boolean,
    )

    external fun nativeSetSystemStatus(
        batteryLevel: Int,
        batteryCharging: Boolean,
        wifiConnected: Boolean,
        bluetoothConnected: Boolean,
        audioMuted: Boolean,
        freeStorage: String,
        deviceName: String,
        appVersion: String,
        biosStatus: String,
        biosValid: Boolean,
    )

    external fun nativeSetTouchMode(active: Boolean, showTutorial: Boolean)

    external fun nativeSetGamesLoading(loading: Boolean)

    external fun nativeSetSavesLoading(loading: Boolean)

    external fun nativeTriggerTransition(kind: Int)

    external fun nativeGetState(): IntArray

    external fun nativeSetState(screen: Int, mainIndex: Int, gameIndex: Int, saveIndex: Int)

    external fun nativeSelectGameByPath(path: String)

    /** Recently played game paths, most recent first; drives the shell's "Recent" games tab. */
    external fun nativeSetRecentGames(paths: Array<String>)

    /** Parses the user's PS2 BIOS ROM image and keeps the boot-intro textures (thread-safe). */
    external fun nativeLoadBiosAssets(rom: ByteArray): Boolean
}
