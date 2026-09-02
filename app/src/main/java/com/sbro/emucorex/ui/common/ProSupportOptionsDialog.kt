package com.sbro.emucorex.ui.common

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Star
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.core.ProProductOffer
import com.sbro.emucorex.core.ProPurchaseTier
import com.sbro.emucorex.ui.theme.neon.neonShape

@Composable
fun ProSupportOptionsDialog(
    offers: List<ProProductOffer>,
    purchaseInProgress: Boolean,
    onPurchase: (ProPurchaseTier) -> Unit,
    onDismiss: () -> Unit
) {
    SettingsStyledDialog(
        title = stringResource(R.string.settings_pro_support_dialog_title),
        eyebrow = stringResource(R.string.settings_pro_support_dialog_eyebrow),
        icon = Icons.Rounded.Star,
        onDismissRequest = onDismiss
    ) {
        Text(
            text = stringResource(R.string.settings_pro_support_dialog_desc),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        offers.forEach { offer ->
            val title = stringResource(
                when (offer.tier) {
                    ProPurchaseTier.SUPPORTER -> R.string.settings_pro_supporter_title
                    ProPurchaseTier.PATRON -> R.string.settings_pro_patron_title
                    ProPurchaseTier.BASE -> R.string.settings_pro_title
                }
            )
            val description = stringResource(
                when (offer.tier) {
                    ProPurchaseTier.SUPPORTER -> R.string.settings_pro_supporter_desc
                    ProPurchaseTier.PATRON -> R.string.settings_pro_patron_desc
                    ProPurchaseTier.BASE -> R.string.settings_pro_locked_body
                }
            )
            val interactionSource = remember(offer.tier) { MutableInteractionSource() }
            val shape = neonShape(18.dp)
            Surface(
                modifier = Modifier
                    .fillMaxWidth()
                    .gamepadFocusableCard(
                        enabled = !purchaseInProgress,
                        shape = shape,
                        interactionSource = interactionSource,
                        addFocusTarget = false
                    ),
                enabled = !purchaseInProgress,
                onClick = { onPurchase(offer.tier) },
                interactionSource = interactionSource,
                shape = shape,
                color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.34f),
                border = BorderStroke(
                    1.dp,
                    MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.62f)
                )
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(42.dp)
                            .clip(neonShape(14.dp))
                            .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            imageVector = Icons.Rounded.Star,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary
                        )
                    }
                    Column(
                        modifier = Modifier.weight(1f),
                        verticalArrangement = Arrangement.spacedBy(2.dp)
                    ) {
                        Text(
                            text = title,
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                        Text(
                            text = description,
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                    Text(
                        text = offer.formattedPrice,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.primary,
                        maxLines = 1
                    )
                }
            }
        }
        Text(
            text = stringResource(R.string.settings_pro_support_same_features),
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant
        )
        TextButton(
            onClick = onDismiss,
            modifier = Modifier.fillMaxWidth()
        ) {
            Text(stringResource(android.R.string.cancel))
        }
    }
}
