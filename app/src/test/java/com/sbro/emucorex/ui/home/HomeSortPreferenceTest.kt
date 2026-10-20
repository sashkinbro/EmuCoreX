package com.sbro.emucorex.ui.home

import org.junit.Assert.assertEquals
import org.junit.Test

class HomeSortPreferenceTest {
    @Test fun savedNamesRestoreEverySortChoiceIncludingPlatforms() {
        HomeSortOption.entries.forEach { option ->
            assertEquals(option, HomeSortOption.fromPreferenceValue(option.name))
        }
    }

    @Test fun missingOrUnknownChoicesKeepTheExistingTitleDefault() {
        listOf(null, "", "UNKNOWN", "7").forEach { value ->
            assertEquals(HomeSortOption.TITLE_ASC, HomeSortOption.fromPreferenceValue(value))
        }
    }
}
