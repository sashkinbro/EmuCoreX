package com.sbro.emucorex.navigation

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class AdaptiveShellDrawerPolicyTest {
    @Test
    fun feedbackWithBackNavigationDoesNotUseModalDrawer() {
        assertFalse(
            shouldUseCompactModalDrawer(
                drawerEnabled = true,
                selected = PrimaryDestination.Feedback,
                hasBackClick = true
            )
        )
    }

    @Test
    fun settingsWithBackNavigationDoesNotUseModalDrawer() {
        assertFalse(
            shouldUseCompactModalDrawer(
                drawerEnabled = true,
                selected = PrimaryDestination.Settings,
                hasBackClick = true
            )
        )
    }

    @Test
    fun hubUsesStandardBackNavigationInsteadOfReopeningTheDrawer() {
        assertFalse(
            shouldUseCompactModalDrawer(
                drawerEnabled = true,
                selected = PrimaryDestination.Hub,
                hasBackClick = true
            )
        )
    }

    @Test
    fun homeKeepsModalDrawer() {
        assertTrue(
            shouldUseCompactModalDrawer(
                drawerEnabled = true,
                selected = PrimaryDestination.Home,
                hasBackClick = false
            )
        )
    }

    @Test
    fun disabledDrawerCannotBeCreated() {
        assertFalse(
            shouldUseCompactModalDrawer(
                drawerEnabled = false,
                selected = PrimaryDestination.Home,
                hasBackClick = false
            )
        )
    }

    @Test
    fun drawerInteractionWaitsForDestinationToSettle() {
        assertFalse(shouldEnableDrawerInteraction(useModalDrawer = true, destinationSettled = false))
        assertTrue(shouldEnableDrawerInteraction(useModalDrawer = true, destinationSettled = true))
        assertFalse(shouldEnableDrawerInteraction(useModalDrawer = false, destinationSettled = true))
    }
}
