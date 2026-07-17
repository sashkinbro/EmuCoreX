package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class AndroidNativeCoreSelectorTest {
    @Test
    fun selectsFourKilobyteCoreForStandardAndroidPages() {
        assertEquals("emucore_4k", selectAndroidNativeCoreLibrary(4_096L))
    }

    @Test
    fun selectsSixteenKilobyteCoreForLargeAndroidPages() {
        assertEquals("emucore_16k", selectAndroidNativeCoreLibrary(16_384L))
    }

    @Test
    fun rejectsUnsupportedPageSizesInsteadOfLoadingAnIncompatibleCore() {
        assertThrows(IllegalStateException::class.java) {
            selectAndroidNativeCoreLibrary(0L)
        }
        assertThrows(IllegalStateException::class.java) {
            selectAndroidNativeCoreLibrary(65_536L)
        }
    }
}
