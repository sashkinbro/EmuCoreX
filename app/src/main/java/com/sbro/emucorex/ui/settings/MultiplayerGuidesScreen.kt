// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+

package com.sbro.emucorex.ui.settings

import androidx.annotation.StringRes
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Link
import androidx.compose.material.icons.rounded.Public
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.appScreenTopPadding
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorex.ui.theme.neon.neonShape

internal enum class MultiplayerGuideId { Online, LocalLink, InternetLink }

@Composable
internal fun MultiplayerGuidesPanel(onOpenGuide: (MultiplayerGuideId) -> Unit) {
    SettingsSection(title = stringResource(R.string.network_guides_title)) {
        SettingsInlineNote(stringResource(R.string.network_guides_intro))
        MultiplayerGuideId.entries.forEach { guide ->
            SettingsItem(
                icon = multiplayerGuideIcon(guide),
                label = stringResource(multiplayerGuideTitle(guide)),
                value = stringResource(multiplayerGuideSummary(guide)),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f)),
                onClick = { onOpenGuide(guide) }
            )
        }
    }
}

@Composable
internal fun MultiplayerGuideDetailScreen(
    guide: MultiplayerGuideId,
    onBackClick: () -> Unit
) {
    val topInset = appScreenTopPadding()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .padding(navigationBarsHorizontalPaddingValues()),
        contentPadding = PaddingValues(
            start = ScreenHorizontalPadding,
            top = topInset,
            end = ScreenHorizontalPadding,
            bottom = bottomInset + 24.dp
        ),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item(key = "guide-top-bar") {
            ScreenTopBar(
                title = stringResource(multiplayerGuideTitle(guide)),
                onBackClick = onBackClick,
                modifier = Modifier.fillMaxWidth().widthIn(max = 920.dp)
            )
        }
        item(key = "guide-intro") {
            Card(
                modifier = Modifier.fillMaxWidth().widthIn(max = 820.dp),
                shape = neonShape(26.dp),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainer),
                border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.72f))
            ) {
                Column(
                    modifier = Modifier.padding(22.dp),
                    verticalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Icon(
                        imageVector = multiplayerGuideIcon(guide),
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary
                    )
                    Text(
                        text = stringResource(multiplayerGuideSummary(guide)),
                        style = MaterialTheme.typography.titleMedium
                    )
                }
            }
        }
        guideSection(
            key = "purpose",
            title = R.string.network_guide_purpose,
            body = multiplayerGuidePurpose(guide)
        )
        guideSection(
            key = "requirements",
            title = R.string.network_guide_requirements,
            body = multiplayerGuideRequirements(guide)
        )
        guideSection(
            key = "steps",
            title = R.string.network_guide_steps,
            body = multiplayerGuideSteps(guide)
        )
        guideSection(
            key = "limits",
            title = R.string.network_guide_limits,
            body = multiplayerGuideLimits(guide)
        )
    }
}

private fun androidx.compose.foundation.lazy.LazyListScope.guideSection(
    key: String,
    @StringRes title: Int,
    @StringRes body: Int
) {
    item(key = key) {
        Card(
            modifier = Modifier.fillMaxWidth().widthIn(max = 820.dp),
            shape = neonShape(22.dp),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceContainerLow),
            border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.68f))
        ) {
            Column(
                modifier = Modifier.padding(20.dp),
                verticalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                Text(stringResource(title), style = MaterialTheme.typography.titleLarge)
                Text(
                    text = stringResource(body),
                    style = MaterialTheme.typography.bodyLarge,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}

private fun multiplayerGuideIcon(guide: MultiplayerGuideId): ImageVector = when (guide) {
    MultiplayerGuideId.Online -> Icons.Rounded.Public
    MultiplayerGuideId.LocalLink -> Icons.Rounded.Link
    MultiplayerGuideId.InternetLink -> Icons.Rounded.Public
}

@StringRes
private fun multiplayerGuideTitle(guide: MultiplayerGuideId): Int = when (guide) {
    MultiplayerGuideId.Online -> R.string.network_online_title
    MultiplayerGuideId.LocalLink -> R.string.network_local_link_title
    MultiplayerGuideId.InternetLink -> R.string.network_internet_link_title
}

@StringRes
private fun multiplayerGuideSummary(guide: MultiplayerGuideId): Int = when (guide) {
    MultiplayerGuideId.Online -> R.string.network_online_desc
    MultiplayerGuideId.LocalLink -> R.string.network_local_link_desc
    MultiplayerGuideId.InternetLink -> R.string.network_internet_link_desc
}

@StringRes
private fun multiplayerGuidePurpose(guide: MultiplayerGuideId): Int = when (guide) {
    MultiplayerGuideId.Online -> R.string.network_guide_online_purpose
    MultiplayerGuideId.LocalLink -> R.string.network_guide_local_purpose
    MultiplayerGuideId.InternetLink -> R.string.network_guide_internet_purpose
}

@StringRes
private fun multiplayerGuideRequirements(guide: MultiplayerGuideId): Int = when (guide) {
    MultiplayerGuideId.Online -> R.string.network_guide_online_requirements
    MultiplayerGuideId.LocalLink -> R.string.network_guide_local_requirements
    MultiplayerGuideId.InternetLink -> R.string.network_guide_internet_requirements
}

@StringRes
private fun multiplayerGuideSteps(guide: MultiplayerGuideId): Int = when (guide) {
    MultiplayerGuideId.Online -> R.string.network_guide_online_steps
    MultiplayerGuideId.LocalLink -> R.string.network_guide_local_steps
    MultiplayerGuideId.InternetLink -> R.string.network_guide_internet_steps
}

@StringRes
private fun multiplayerGuideLimits(guide: MultiplayerGuideId): Int = when (guide) {
    MultiplayerGuideId.Online -> R.string.network_guide_online_limits
    MultiplayerGuideId.LocalLink -> R.string.network_guide_local_limits
    MultiplayerGuideId.InternetLink -> R.string.network_guide_internet_limits
}
