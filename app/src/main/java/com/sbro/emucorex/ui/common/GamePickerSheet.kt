package com.sbro.emucorex.ui.common

import androidx.compose.animation.core.tween
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.Close
import androidx.compose.material.icons.rounded.Search
import androidx.compose.material.icons.rounded.SportsEsports
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.sbro.emucorex.R
import com.sbro.emucorex.data.GameItem
import com.sbro.emucorex.ui.theme.neon.neonButtonShape
import com.sbro.emucorex.ui.theme.neon.neonShape
import com.sbro.emucorex.ui.theme.ScreenHorizontalPadding
import kotlinx.coroutines.launch

/**
 * Full-screen dialog for selecting a game from the library with search & fast letter filtering.
 * Everything scrolls naturally together. Manual top padding is used (no status bars padding).
 */
@Composable
fun GamePickerSheet(
    visible: Boolean,
    games: List<GameItem>,
    selectedPath: String?,
    onSelected: (GameItem) -> Unit,
    onDismiss: () -> Unit,
    extraBadge: ((GameItem) -> String?)? = null
) {
    if (!visible) return

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(
            usePlatformDefaultWidth = false,
            decorFitsSystemWindows = false
        )
    ) {
        GamePickerSheetContent(
            games = games,
            selectedPath = selectedPath,
            onSelected = { game ->
                onSelected(game)
                onDismiss()
            },
            onDismiss = onDismiss,
            extraBadge = extraBadge
        )
    }
}

@Composable
private fun GamePickerSheetContent(
    games: List<GameItem>,
    selectedPath: String?,
    onSelected: (GameItem) -> Unit,
    onDismiss: () -> Unit,
    extraBadge: ((GameItem) -> String?)?
) {
    var searchQuery by rememberSaveable { mutableStateOf("") }
    var selectedLetter by rememberSaveable { mutableStateOf<String?>(null) }
    val listState = rememberLazyListState()
    val scope = rememberCoroutineScope()
    val focusRequester = remember { FocusRequester() }
    val keyboardController = LocalSoftwareKeyboardController.current

    // Extract all unique first letters available across games for fast navigation
    val availableLetters = remember(games) {
        games.mapNotNull { game ->
            val firstChar = game.title.trim().firstOrNull()?.uppercaseChar()
            when {
                firstChar == null -> null
                firstChar in 'A'..'Z' -> firstChar.toString()
                firstChar.isDigit() -> "#"
                else -> "#"
            }
        }.distinct().sortedWith { a, b ->
            if (a == "#") -1 else if (b == "#") 1 else a.compareTo(b)
        }
    }

    val filteredGames by remember(games, searchQuery, selectedLetter) {
        derivedStateOf {
            val query = searchQuery.trim()
            if (query.isNotEmpty()) {
                games.filter { game ->
                    game.title.contains(query, ignoreCase = true) ||
                        game.serial?.contains(query, ignoreCase = true) == true ||
                        game.fileName.contains(query, ignoreCase = true)
                }
            } else if (selectedLetter != null) {
                games.filter { game ->
                    val firstChar = game.title.trim().firstOrNull()?.uppercaseChar()
                    if (selectedLetter == "#") {
                        firstChar == null || firstChar.isDigit() || firstChar !in 'A'..'Z'
                    } else {
                        firstChar?.toString().equals(selectedLetter, ignoreCase = true)
                    }
                }
            } else {
                games
            }
        }
    }

    val topInset = appScreenTopPadding()
    val navBarPadding = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()

    Surface(
        modifier = Modifier.fillMaxSize(),
        color = MaterialTheme.colorScheme.background
    ) {
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            state = listState,
            // Horizontal padding is applied per item (not here) so the letter row and
            // TV focus highlights can draw edge-to-edge while scrolling.
            contentPadding = PaddingValues(
                top = topInset,
                bottom = 24.dp + navBarPadding
            ),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            // ── Search Field ──
            item(key = "picker-search") {
                OutlinedTextField(
                    value = searchQuery,
                    onValueChange = { query ->
                        searchQuery = query
                        selectedLetter = null
                        scope.launch { listState.scrollToItem(0) }
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = ScreenHorizontalPadding)
                        .focusRequester(focusRequester),
                    textStyle = MaterialTheme.typography.bodyMedium,
                    placeholder = {
                        Text(
                            text = stringResource(R.string.game_picker_search_hint),
                            style = MaterialTheme.typography.bodyMedium,
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis
                        )
                    },
                    leadingIcon = {
                        Icon(
                            Icons.Rounded.Search,
                            contentDescription = null,
                            modifier = Modifier.size(20.dp),
                            tint = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    },
                    trailingIcon = if (searchQuery.isNotEmpty()) {
                        {
                            IconButton(
                                onClick = {
                                    searchQuery = ""
                                    scope.launch { listState.scrollToItem(0) }
                                },
                                modifier = Modifier.size(36.dp)
                            ) {
                                Icon(
                                    Icons.Rounded.Close,
                                    contentDescription = stringResource(R.string.home_search_clear),
                                    modifier = Modifier.size(18.dp)
                                )
                            }
                        }
                    } else null,
                    singleLine = true,
                    shape = neonShape(14.dp),
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f),
                        unfocusedContainerColor = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.4f)
                    )
                )
            }

            // ── Quick Letter Filter Row ──
            if (searchQuery.isBlank() && availableLetters.size > 1 && games.size >= 8) {
                item(key = "picker-letters") {
                    LazyRow(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 2.dp, bottom = 2.dp)
                            .tvFocusGroup(),
                        contentPadding = PaddingValues(horizontal = ScreenHorizontalPadding),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        item(key = "letter-filter-all") {
                            LetterFilterChip(
                                label = stringResource(R.string.game_picker_all_filter),
                                isSelected = selectedLetter == null,
                                onClick = {
                                    selectedLetter = null
                                    scope.launch { listState.scrollToItem(0) }
                                }
                            )
                        }
                        items(availableLetters, key = { "letter-$it" }) { letter ->
                            LetterFilterChip(
                                label = letter,
                                isSelected = selectedLetter == letter,
                                onClick = {
                                    selectedLetter = if (selectedLetter == letter) null else letter
                                    scope.launch { listState.scrollToItem(0) }
                                }
                            )
                        }
                    }
                }
            }

            // ── Games List or Empty State ──
            if (filteredGames.isEmpty()) {
                item(key = "picker-empty") {
                    GamePickerEmptyState(
                        isSearch = searchQuery.isNotBlank() || selectedLetter != null,
                        onClearSearch = {
                            searchQuery = ""
                            selectedLetter = null
                            scope.launch { listState.scrollToItem(0) }
                        },
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = ScreenHorizontalPadding)
                            .padding(top = 40.dp)
                    )
                }
            } else {
                items(filteredGames, key = GameItem::path) { game ->
                    val isSelected = game.path == selectedPath
                    val extraBadgeText = extraBadge?.invoke(game)
                    GamePickerItem(
                        game = game,
                        isSelected = isSelected,
                        extraBadge = extraBadgeText,
                        onClick = {
                            keyboardController?.hide()
                            onSelected(game)
                        },
                        modifier = Modifier.animateItem(
                            fadeInSpec = tween(180),
                            placementSpec = tween(180),
                            fadeOutSpec = tween(140)
                        )
                    )
                }
            }
        }
    }
}

@Composable
private fun LetterFilterChip(
    label: String,
    isSelected: Boolean,
    onClick: () -> Unit
) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(10.dp)
    Surface(
        shape = shape,
        color = if (isSelected) {
            MaterialTheme.colorScheme.primary
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.5f)
        },
        border = BorderStroke(
            width = 1.dp,
            color = if (isSelected) {
                MaterialTheme.colorScheme.primary
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.35f)
            }
        ),
        modifier = Modifier
            .tvGamepadFocusableCard(
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            )
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick
            )
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium,
            fontWeight = if (isSelected) FontWeight.Bold else FontWeight.Medium,
            color = if (isSelected) {
                MaterialTheme.colorScheme.onPrimary
            } else {
                MaterialTheme.colorScheme.onSurfaceVariant
            },
            modifier = Modifier.padding(horizontal = 11.dp, vertical = 6.dp)
        )
    }
}

@Composable
private fun GamePickerItem(
    game: GameItem,
    isSelected: Boolean,
    extraBadge: String?,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    val interactionSource = remember { MutableInteractionSource() }
    val shape = neonShape(18.dp)

    Surface(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = ScreenHorizontalPadding)
            .padding(vertical = 2.dp)
            .tvGamepadFocusableCard(
                shape = shape,
                interactionSource = interactionSource,
                addFocusTarget = false
            )
            .clickable(
                interactionSource = interactionSource,
                indication = null,
                onClick = onClick
            ),
        shape = shape,
        color = if (isSelected) {
            MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.65f)
        } else {
            MaterialTheme.colorScheme.surface
        },
        border = BorderStroke(
            width = if (isSelected) 2.dp else 1.dp,
            color = if (isSelected) {
                MaterialTheme.colorScheme.primary
            } else {
                MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
            }
        )
    ) {
        Row(
            modifier = Modifier.padding(12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            GameCoverArt(
                coverPath = game.coverArtPath,
                fallbackTitle = game.title,
                contentScale = gameCoverContentScale(game.coverArtPath, ContentScale.Crop),
                modifier = Modifier
                    .size(width = 54.dp, height = 76.dp)
                    .clip(gameCoverShape(game.coverArtPath, neonShape(12.dp)))
            )
            Spacer(Modifier.width(14.dp))
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                Text(
                    text = game.title,
                    style = MaterialTheme.typography.titleSmall,
                    fontWeight = if (isSelected) FontWeight.Bold else FontWeight.SemiBold,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis,
                    color = if (isSelected) {
                        MaterialTheme.colorScheme.onPrimaryContainer
                    } else {
                        MaterialTheme.colorScheme.onSurface
                    }
                )
                Row(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    game.serial?.takeIf { it.isNotBlank() }?.let { serial ->
                        Surface(
                            shape = neonShape(8.dp),
                            color = if (isSelected) {
                                MaterialTheme.colorScheme.primary.copy(alpha = 0.18f)
                            } else {
                                MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.7f)
                            }
                        ) {
                            Text(
                                text = serial,
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.SemiBold,
                                color = if (isSelected) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.onSurfaceVariant
                                },
                                modifier = Modifier.padding(horizontal = 7.dp, vertical = 2.dp)
                            )
                        }
                    }
                    if (extraBadge != null) {
                        Surface(
                            shape = neonShape(8.dp),
                            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.15f)
                        ) {
                            Text(
                                text = extraBadge,
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.Bold,
                                color = MaterialTheme.colorScheme.primary,
                                modifier = Modifier.padding(horizontal = 7.dp, vertical = 2.dp)
                            )
                        }
                    }
                }
            }
            if (isSelected) {
                Spacer(Modifier.width(8.dp))
                Box(
                    modifier = Modifier
                        .size(24.dp)
                        .background(
                            MaterialTheme.colorScheme.primary,
                            CircleShape
                        ),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = Icons.Rounded.Check,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp),
                        tint = MaterialTheme.colorScheme.onPrimary
                    )
                }
            }
        }
    }
}

@Composable
private fun GamePickerEmptyState(
    isSearch: Boolean,
    onClearSearch: () -> Unit,
    modifier: Modifier = Modifier
) {
    Box(
        modifier = modifier.padding(32.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Icon(
                imageVector = if (isSearch) Icons.Rounded.Search else Icons.Rounded.SportsEsports,
                contentDescription = null,
                modifier = Modifier.size(48.dp),
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.5f)
            )
            Text(
                text = stringResource(
                    if (isSearch) R.string.game_picker_no_results
                    else R.string.content_library_empty
                ),
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            if (isSearch) {
                Button(
                    shape = neonButtonShape(),
                    onClick = onClearSearch,
                    colors = ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant,
                        contentColor = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                ) {
                    Icon(
                        Icons.Rounded.Close,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(Modifier.width(6.dp))
                    Text(stringResource(R.string.game_picker_clear_search))
                }
            }
        }
    }
}
