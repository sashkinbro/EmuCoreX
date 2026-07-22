package com.sbro.emucorex.ui.home

import com.sbro.emucorex.core.TvUiMetrics
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class HomeGridLayoutTest {
    @Test
    fun portraitPhoneColumnsTrackCoverScale() {
        val compact = calculateHomeGridColumnCount(390, 844, 390, 0.60f)
        val default = calculateHomeGridColumnCount(390, 844, 390, 1.00f)
        val large = calculateHomeGridColumnCount(390, 844, 390, 1.60f)

        assertEquals(5, compact)
        assertEquals(3, default)
        assertEquals(2, large)
        assertTrue(compact > default && default > large)
    }

    @Test
    fun wideLayoutExcludesNavigationRailFromGridWidth() {
        val columns = calculateHomeGridColumnCount(1200, 800, 800, 1.00f)

        assertEquals(8, columns)
    }

    @Test
    fun tvGridUsesActualSafeContentWidth() {
        val widthDp = 960
        val columns = calculateHomeGridColumnCount(
            screenWidthDp = widthDp,
            screenHeightDp = 540,
            smallestScreenWidthDp = 540,
            gridScale = 1f,
            contentReservedWidthDp = TvUiMetrics.contentReservedWidthDp(widthDp)
        )

        assertEquals(6, columns)
    }

    @Test
    fun gridAlwaysKeepsAtLeastOneColumn() {
        assertEquals(1, calculateHomeGridColumnCount(1, 1, 1, 10f))
    }

    @Test
    fun measuredWindowWinsOverStaleLandscapeConfigurationAfterGameExit() {
        val metrics = resolveHomeWindowMetrics(
            configurationWidthDp = 844,
            configurationHeightDp = 390,
            measuredWidthDp = 390,
            measuredHeightDp = 844
        )

        assertEquals(HomeWindowMetrics(widthDp = 390, heightDp = 844), metrics)
        assertEquals(
            3,
            calculateHomeGridColumnCount(
                screenWidthDp = metrics.widthDp,
                screenHeightDp = metrics.heightDp,
                smallestScreenWidthDp = 390,
                gridScale = 1f
            )
        )
    }

    @Test
    fun invalidMeasuredWindowFallsBackToConfiguration() {
        assertEquals(
            HomeWindowMetrics(widthDp = 390, heightDp = 844),
            resolveHomeWindowMetrics(
                configurationWidthDp = 390,
                configurationHeightDp = 844,
                measuredWidthDp = 0,
                measuredHeightDp = 0
            )
        )
    }
}
