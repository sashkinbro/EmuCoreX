package com.sbro.emucorex.ui.common

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.calculateEndPadding
import androidx.compose.foundation.layout.calculateStartPadding
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalLayoutDirection

@Composable
fun navigationBarsHorizontalPaddingValues(): PaddingValues {
    val layoutDirection = LocalLayoutDirection.current
    val navigationBarsPadding = WindowInsets.navigationBars.asPaddingValues()
    return PaddingValues(
        start = navigationBarsPadding.calculateStartPadding(layoutDirection),
        end = navigationBarsPadding.calculateEndPadding(layoutDirection)
    )
}
