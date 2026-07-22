package com.sbro.emucorex.core

import org.junit.Assert.assertFalse
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class TvUiPolicyTest {
    @Test
    fun autoUsesTvInterfaceOnlyForTvSignals() {
        assertTrue(TvUiPolicy.shouldUseTvInterface(TvInterfaceMode.AUTO, true, false))
        assertTrue(TvUiPolicy.shouldUseTvInterface(TvInterfaceMode.AUTO, false, true))
        assertFalse(TvUiPolicy.shouldUseTvInterface(TvInterfaceMode.AUTO, false, false))
    }

    @Test
    fun manualStandardOverridesTvDetection() {
        assertFalse(TvUiPolicy.shouldUseTvInterface(TvInterfaceMode.STANDARD, true, true))
    }

    @Test
    fun manualTvWorksOnRegularAndroidDevices() {
        assertTrue(TvUiPolicy.shouldUseTvInterface(TvInterfaceMode.TV, false, false))
    }

    @Test
    fun tvShellUsesScreenSpecificPaddingInsteadOfGlobalOverscan() {
        assertEquals(0, TvUiMetrics.safeHorizontalDp(640))
        assertEquals(0, TvUiMetrics.safeVerticalDp(360))
        assertEquals(0, TvUiMetrics.safeHorizontalDp(960))
        assertEquals(0, TvUiMetrics.safeVerticalDp(540))
        assertEquals(0, TvUiMetrics.safeHorizontalDp(1920))
        assertEquals(0, TvUiMetrics.safeVerticalDp(1080))
    }

    @Test
    fun tvNavigationAndContentReservationStayBounded() {
        assertEquals(248, TvUiMetrics.navigationWidthDp(640))
        assertEquals(288, TvUiMetrics.navigationWidthDp(960))
        assertEquals(288, TvUiMetrics.contentReservedWidthDp(960))
        assertEquals(360, TvUiMetrics.navigationWidthDp(1920))
    }
}
