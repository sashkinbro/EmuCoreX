package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class AndroidNativeCoreSelectorTest {
    @Test
    fun selectsFourKilobyteCoreForStandardAndroidPages() {
        assertEquals("emucore_mali_4k", selectAndroidNativeCoreLibrary(4_096L))
    }

    @Test
    fun selectsSixteenKilobyteCoreForLargeAndroidPages() {
        assertEquals("emucore_mali_16k", selectAndroidNativeCoreLibrary(16_384L))
    }

    @Test
    fun selectsAdrenoCoreForFreshAdrenoRendererDetectionAtBothPageSizes() {
        assertEquals("emucore_adreno_4k", selectAndroidNativeCoreLibrary(4_096L, "Qualcomm Adreno (TM) 750"))
        assertEquals("emucore_adreno_16k", selectAndroidNativeCoreLibrary(16_384L, "Adreno 830"))
    }

    @Test
    fun mapsEveryNonAdrenoAndUnknownRendererToMaliCore() {
        listOf("Mali-G57", "PowerVR GE8320", "Xclipse 940", "SwiftShader", "NotAdreno virtual", "", null).forEach { renderer ->
            assertEquals("renderer=$renderer", "emucore_mali_4k", selectAndroidNativeCoreLibrary(4_096L, renderer))
            assertEquals("renderer=$renderer", "emucore_mali_16k", selectAndroidNativeCoreLibrary(16_384L, renderer))
        }
    }

    @Test
    fun rejectsUnsupportedPageSizesInsteadOfLoadingAnIncompatibleCore() {
        assertThrows(IllegalStateException::class.java) {
            selectAndroidNativeCoreLibrary(0L, "Adreno 750")
        }
        assertThrows(IllegalStateException::class.java) {
            selectAndroidNativeCoreLibrary(65_536L, "Mali-G57")
        }
    }
}
