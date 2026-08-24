package com.sbro.emucorex.ui.common

import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.OverlayControlLayout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class OverlayCanvasResponsiveLayoutTest {
    private val density = Density(3f)

    @Test
    fun `default controls fit without overlap on common screen shapes`() {
        listOf(
            ScreenCase("narrow landscape phone", 640.dp, 280.dp),
            ScreenCase("16 by 9 phone", 800.dp, 450.dp),
            ScreenCase("20 by 9 phone", 900.dp, 405.dp),
            ScreenCase("4 by 3 tablet landscape", 1024.dp, 768.dp),
            ScreenCase("4 by 3 tablet portrait", 768.dp, 1024.dp)
        ).forEach { screen ->
            assertResponsiveLayout(screen)
        }
    }

    @Test
    fun `default layout keeps the independent dpad beside the left stick`() {
        listOf(
            ScreenCase("narrow landscape phone", 640.dp, 280.dp),
            ScreenCase("20 by 9 phone", 900.dp, 405.dp),
            ScreenCase("4 by 3 tablet landscape", 1024.dp, 768.dp)
        ).forEach { screen ->
            val layout = buildLayout(screen)
            val dpad = requireNotNull(layout.dpadCluster)
            val leftStick = requireNotNull(layout.leftStick)
            val rightStick = requireNotNull(layout.rightStick)
            val leftGap = leftStick.x - (dpad.x + dpad.size)
            val rightGap = layout.actionButtons.minOf { it.x } - (rightStick.x + rightStick.size)
            val leftShoulderBottom = layout.leftShoulders.maxOf { it.y + it.height }

            assertTrue("${screen.name}: independent dpad must be visible", dpad.visible)
            assertTrue("${screen.name}: left stick must remain visible", leftStick.visible)
            assertTrue("${screen.name}: dpad must stay left of the left stick", dpad.x + dpad.size < leftStick.x)
            assertTrue("${screen.name}: left shoulders must stay above the dpad", leftShoulderBottom < dpad.y)
            assertEquals("${screen.name}: primary control gaps must match", leftGap.value, rightGap.value, EPSILON)
        }
    }

    @Test
    fun `explicitly hidden independent dpad remains hidden`() {
        val controls = AppPreferences.defaultOverlayControlLayouts().toMutableMap().apply {
            this["dpad_cluster"] = requireNotNull(this["dpad_cluster"]).copy(visible = false)
        }

        val layout = buildLayout(ScreenCase("custom hidden dpad", 800.dp, 450.dp), controls)

        assertTrue(layout.dpadCluster == null)
        assertTrue(layout.leftStick?.visible == true)
    }

    @Test
    fun `asymmetric cutout and navigation insets define the actual safe area`() {
        val screen = ScreenCase(
            name = "asymmetric insets",
            width = 720.dp,
            height = 360.dp,
            leftInset = 54.dp,
            rightInset = 18.dp,
            topInset = 28.dp,
            bottomInset = 32.dp
        )

        val layout = buildLayout(screen)
        assertInsideSafeArea(screen, layout)
        assertNoOverlaps(screen.name, layout)

        val left = requireNotNull(layout.leftStick)
        val independentDpad = requireNotNull(layout.dpadCluster)
        val rightActionEdge = layout.actionButtons.maxOf { it.x + it.width }
        assertTrue(left.x >= screen.leftInset)
        assertTrue(rightActionEdge <= screen.width - screen.rightInset)
        assertEquals(
            "physical side margins must remain symmetric",
            independentDpad.x.value,
            (screen.width - rightActionEdge).value,
            EPSILON
        )

        val leftTopEdge = layout.leftShoulders.minOf { it.x }
        val rightShoulderEdge = layout.rightShoulders.maxOf { it.x + it.width }
        assertEquals(
            "top control margins must remain symmetric",
            leftTopEdge.value,
            (screen.width - rightShoulderEdge).value,
            EPSILON
        )

        val centerLeft = layout.centerButtons.minOf { it.x }
        val centerRight = layout.centerButtons.maxOf { it.x + it.width }
        assertEquals(
            "center controls must stay on the physical screen center",
            screen.width.value / 2f,
            ((centerLeft + centerRight) / 2f).value,
            EPSILON
        )
    }

    @Test
    fun `visible right stick is placed without colliding in landscape and portrait`() {
        val controls = AppPreferences.defaultOverlayControlLayouts().toMutableMap().apply {
            this["right_stick"] = requireNotNull(this["right_stick"]).copy(visible = true)
        }
        listOf(
            ScreenCase("right stick landscape", 640.dp, 280.dp),
            ScreenCase("right stick portrait", 360.dp, 800.dp),
            ScreenCase("right stick tablet", 1024.dp, 768.dp)
        ).forEach { screen ->
            val layout = buildLayout(screen, controls)
            assertInsideSafeArea(screen, layout)
            assertNoOverlaps(screen.name, layout)
        }
    }

    @Test
    fun `wide analog touch surfaces participate in responsive spacing`() {
        val screen = ScreenCase("wide analog surfaces", 640.dp, 280.dp)
        val controls = AppPreferences.defaultOverlayControlLayouts().toMutableMap().apply {
            this["left_stick"] = requireNotNull(this["left_stick"]).copy(
                surfaceOnly = true,
                widthScale = 200
            )
            this["right_stick"] = requireNotNull(this["right_stick"]).copy(
                visible = true,
                surfaceOnly = true,
                widthScale = 200
            )
        }
        val layout = buildLayout(screen, controls)
        val left = requireNotNull(layout.leftStick)
        val right = requireNotNull(layout.rightStick)
        val leftPanelLeft = left.x - (left.size * (left.widthScale / 100f) - left.size) / 2f
        val leftPanelRight = leftPanelLeft + left.size * (left.widthScale / 100f)
        val rightPanelLeft = right.x - (right.size * (right.widthScale / 100f) - right.size) / 2f
        val rightPanelRight = rightPanelLeft + right.size * (right.widthScale / 100f)
        val actionLeft = layout.actionButtons.minOf { it.x }

        assertTrue(leftPanelLeft >= screen.leftInset)
        assertTrue(leftPanelRight < rightPanelLeft)
        assertTrue(rightPanelRight < actionLeft)
    }

    @Test
    fun `custom per-control offsets remain relative to responsive anchors`() {
        val screen = ScreenCase("custom", 800.dp, 450.dp)
        val defaults = buildLayout(screen)
        val customControls = AppPreferences.defaultOverlayControlLayouts().toMutableMap().apply {
            this["triangle"] = requireNotNull(this["triangle"]).copy(offset = 33f to -18f)
        }
        val customized = buildLayout(screen, customControls)
        val defaultTriangle = requireNotNull(defaults.button("triangle"))
        val customTriangle = requireNotNull(customized.button("triangle"))

        assertEquals(11f, (customTriangle.x - defaultTriangle.x).value, 0.001f)
        assertEquals(-6f, (customTriangle.y - defaultTriangle.y).value, 0.001f)
    }

    @Test
    fun `large requested scale is reduced only where the safe area requires it`() {
        listOf(
            ScreenCase("large scale narrow", 640.dp, 280.dp),
            ScreenCase("large scale inset", 720.dp, 360.dp, 48.dp, 24.dp, 20.dp, 30.dp),
            ScreenCase("large scale tablet", 1024.dp, 768.dp)
        ).forEach { screen ->
            val layout = buildLayout(screen, overlayScale = 1.6f)
            assertInsideSafeArea(screen, layout)
            assertNoOverlaps(screen.name, layout)
        }
    }

    @Test
    fun `legacy group positions remain as deltas from responsive defaults`() {
        val screen = ScreenCase("group offset", 800.dp, 450.dp)
        val defaults = buildLayout(screen)
        val moved = buildLayout(
            screen = screen,
            actionOffset = (AppPreferences.DEFAULT_ACTION_OFFSET_X + 30f) to
                (AppPreferences.DEFAULT_ACTION_OFFSET_Y - 15f)
        )
        val defaultTriangle = requireNotNull(defaults.button("triangle"))
        val movedTriangle = requireNotNull(moved.button("triangle"))

        assertEquals(10f, (movedTriangle.x - defaultTriangle.x).value, 0.001f)
        assertEquals(-5f, (movedTriangle.y - defaultTriangle.y).value, 0.001f)
    }

    private fun assertResponsiveLayout(screen: ScreenCase) {
        val layout = buildLayout(screen)
        assertInsideSafeArea(screen, layout)
        assertNoOverlaps(screen.name, layout)
    }

    private fun buildLayout(
        screen: ScreenCase,
        controls: Map<String, OverlayControlLayout> = AppPreferences.defaultOverlayControlLayouts(),
        overlayScale: Float = 1f,
        actionOffset: Pair<Float, Float> =
            AppPreferences.DEFAULT_ACTION_OFFSET_X to AppPreferences.DEFAULT_ACTION_OFFSET_Y
    ): OverlayCanvasLayout = buildOverlayCanvasLayout(
        canvasWidth = screen.width,
        canvasHeight = screen.height,
        density = density,
        scaleFactor = overlayScale,
        stickScaleFactor = 1f,
        dpadOffset = AppPreferences.DEFAULT_DPAD_OFFSET_X to AppPreferences.DEFAULT_DPAD_OFFSET_Y,
        lstickOffset = AppPreferences.DEFAULT_LSTICK_OFFSET_X to AppPreferences.DEFAULT_LSTICK_OFFSET_Y,
        rstickOffset = AppPreferences.DEFAULT_RSTICK_OFFSET_X to AppPreferences.DEFAULT_RSTICK_OFFSET_Y,
        actionOffset = actionOffset,
        lbtnOffset = AppPreferences.DEFAULT_LBTN_OFFSET_X to AppPreferences.DEFAULT_LBTN_OFFSET_Y,
        rbtnOffset = AppPreferences.DEFAULT_RBTN_OFFSET_X to AppPreferences.DEFAULT_RBTN_OFFSET_Y,
        centerOffset = AppPreferences.DEFAULT_CENTER_OFFSET_X to AppPreferences.DEFAULT_CENTER_OFFSET_Y,
        controlLayouts = controls,
        safeLeftInset = screen.leftInset,
        safeRightInset = screen.rightInset,
        safeTopInset = screen.topInset,
        safeBottomInset = screen.bottomInset
    )

    private fun assertInsideSafeArea(screen: ScreenCase, layout: OverlayCanvasLayout) {
        val bounds = bounds(layout)
        bounds.forEach { item ->
            assertTrue("${screen.name}: ${item.id} crosses left inset", item.left >= screen.leftInset.value - EPSILON)
            assertTrue("${screen.name}: ${item.id} crosses top inset", item.top >= screen.topInset.value - EPSILON)
            assertTrue(
                "${screen.name}: ${item.id} crosses right inset",
                item.right <= (screen.width - screen.rightInset).value + EPSILON
            )
            assertTrue(
                "${screen.name}: ${item.id} crosses bottom inset",
                item.bottom <= (screen.height - screen.bottomInset).value + EPSILON
            )
        }
    }

    private fun assertNoOverlaps(name: String, layout: OverlayCanvasLayout) {
        val items = bounds(layout)
        for (firstIndex in items.indices) {
            for (secondIndex in firstIndex + 1 until items.size) {
                val first = items[firstIndex]
                val second = items[secondIndex]
                val overlaps = first.left < second.right - EPSILON &&
                    first.right > second.left + EPSILON &&
                    first.top < second.bottom - EPSILON &&
                    first.bottom > second.top + EPSILON
                assertFalse("$name: ${first.id} overlaps ${second.id}", overlaps)
            }
        }
    }

    private fun bounds(layout: OverlayCanvasLayout): List<Bounds> = buildList {
        layout.allButtons.filter { it.visible }.forEach {
            add(Bounds(it.id, it.x.value, it.y.value, (it.x + it.width).value, (it.y + it.height).value))
        }
        layout.leftStick?.takeIf { it.visible }?.let {
            add(Bounds(it.id, it.x.value, it.y.value, (it.x + it.size).value, (it.y + it.size).value))
        }
        layout.rightStick?.takeIf { it.visible }?.let {
            add(Bounds(it.id, it.x.value, it.y.value, (it.x + it.size).value, (it.y + it.size).value))
        }
        layout.dpadCluster?.takeIf { it.visible }?.let {
            add(Bounds(it.id, it.x.value, it.y.value, (it.x + it.size).value, (it.y + it.size).value))
        }
    }

    private data class ScreenCase(
        val name: String,
        val width: Dp,
        val height: Dp,
        val leftInset: Dp = 0.dp,
        val rightInset: Dp = 0.dp,
        val topInset: Dp = 0.dp,
        val bottomInset: Dp = 0.dp
    )

    private data class Bounds(
        val id: String,
        val left: Float,
        val top: Float,
        val right: Float,
        val bottom: Float
    )

    private companion object {
        const val EPSILON = 0.01f
    }
}
