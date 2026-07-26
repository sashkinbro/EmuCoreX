package com.sbro.emucorex.ui.controls

import android.graphics.Color as AndroidColor
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Add
import androidx.compose.material.icons.rounded.Check
import androidx.compose.material.icons.rounded.ContentCopy
import androidx.compose.material.icons.rounded.Delete
import androidx.compose.material.icons.rounded.Lock
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardCapitalization
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import com.sbro.emucorex.R
import com.sbro.emucorex.data.CustomTouchControl
import com.sbro.emucorex.data.CustomTouchControlContent
import com.sbro.emucorex.data.CustomTouchControlLibrary
import com.sbro.emucorex.data.CustomTouchControlShape
import com.sbro.emucorex.ui.common.ScreenTopBar
import com.sbro.emucorex.ui.common.appScreenTopPadding
import java.util.UUID
import kotlin.math.roundToInt

private val CreatorCardShape = RoundedCornerShape(28.dp)
private val CreatorControlShape = RoundedCornerShape(18.dp)

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun TouchControlCreatorScreen(
    initialLibrary: CustomTouchControlLibrary,
    isProUnlocked: Boolean,
    onPurchasePro: () -> Unit,
    onSave: (CustomTouchControlLibrary) -> Unit,
    onBackClick: () -> Unit
) {
    val initial = remember(initialLibrary) { initialLibrary.sanitized() }
    val previewSeed = remember {
        CustomTouchControl(
            id = "preview-only",
            name = "Custom button",
            positionX = 0.72f,
            positionY = 0.62f
        )
    }
    var library by remember(initial) { mutableStateOf(initial) }
    var selectedId by remember(initial) {
        mutableStateOf(initial.controls.firstOrNull()?.id ?: previewSeed.id)
    }
    var draft by remember(initial) {
        mutableStateOf(initial.controls.firstOrNull { it.id == selectedId } ?: previewSeed)
    }
    var deleteCandidate by remember { mutableStateOf<CustomTouchControl?>(null) }

    fun updateDraft(transform: (CustomTouchControl) -> CustomTouchControl) {
        val updated = transform(draft)
        draft = updated
        val index = library.controls.indexOfFirst { it.id == draft.id }
        if (index >= 0) {
            library = library.copy(
                controls = library.controls.toMutableList().apply { set(index, updated) }
            )
        }
    }

    fun select(control: CustomTouchControl) {
        selectedId = control.id
        draft = control
    }

    fun createControl(source: CustomTouchControl? = null) {
        if (!isProUnlocked || library.controls.size >= CustomTouchControlLibrary.MAX_CONTROLS) return
        val now = System.currentTimeMillis()
        val index = library.controls.size + 1
        val created = (source ?: CustomTouchControl()).copy(
            id = UUID.randomUUID().toString(),
            name = if (source == null) {
                "Custom button $index"
            } else {
                "${source.name} copy".take(CustomTouchControl.MAX_NAME_LENGTH)
            },
            createdAtMillis = now,
            updatedAtMillis = now
        )
        library = library.copy(controls = library.controls + created).sanitized()
        select(created)
    }

    fun deleteControl(control: CustomTouchControl) {
        library = library.copy(controls = library.controls.filterNot { it.id == control.id }).sanitized()
        val replacement = library.controls.firstOrNull() ?: previewSeed
        select(replacement)
    }

    deleteCandidate?.let { candidate ->
        AlertDialog(
            onDismissRequest = { deleteCandidate = null },
            title = { Text(stringResource(R.string.touch_control_creator_delete_title)) },
            text = {
                Text(
                    stringResource(
                        R.string.touch_control_creator_delete_message,
                        candidate.name
                    )
                )
            },
            confirmButton = {
                TextButton(
                    onClick = {
                        deleteControl(candidate)
                        deleteCandidate = null
                    }
                ) {
                    Text(stringResource(R.string.touch_control_creator_delete))
                }
            },
            dismissButton = {
                TextButton(onClick = { deleteCandidate = null }) {
                    Text(stringResource(R.string.theme_manager_cancel))
                }
            }
        )
    }

    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    LazyColumn(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .testTag("touch_control_creator_list"),
        contentPadding = PaddingValues(
            start = 24.dp,
            end = 24.dp,
            top = appScreenTopPadding(),
            bottom = bottomInset + 28.dp
        ),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        item {
            ScreenTopBar(
                title = stringResource(R.string.touch_control_creator_title),
                onBackClick = onBackClick
            )
        }
        if (!isProUnlocked) {
            item {
                Surface(
                    shape = CreatorCardShape,
                    color = MaterialTheme.colorScheme.primaryContainer,
                    contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                    border = BorderStroke(
                        1.dp,
                        MaterialTheme.colorScheme.primary.copy(alpha = 0.38f)
                    )
                ) {
                    Row(
                        modifier = Modifier.padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        Icon(Icons.Rounded.Lock, contentDescription = null)
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                stringResource(R.string.touch_control_creator_preview_mode),
                                fontWeight = FontWeight.Bold
                            )
                            Text(
                                stringResource(R.string.touch_control_creator_pro_desc),
                                style = MaterialTheme.typography.bodySmall
                            )
                        }
                        TextButton(onClick = onPurchasePro) {
                            Text(stringResource(R.string.touch_control_creator_unlock))
                        }
                    }
                }
            }
        }
        item {
            CreatorSection(
                title = stringResource(R.string.touch_control_creator_library),
                trailing = {
                    IconButton(
                        onClick = { createControl() },
                        enabled = isProUnlocked &&
                            library.controls.size < CustomTouchControlLibrary.MAX_CONTROLS
                    ) {
                        Icon(
                            Icons.Rounded.Add,
                            contentDescription = stringResource(R.string.touch_control_creator_create)
                        )
                    }
                }
            ) {
                Text(
                    stringResource(
                        R.string.touch_control_creator_count,
                        library.controls.size,
                        CustomTouchControlLibrary.MAX_CONTROLS
                    ),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                if (library.controls.isEmpty()) {
                    Surface(
                        modifier = Modifier.fillMaxWidth(),
                        shape = CreatorControlShape,
                        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.35f)
                    ) {
                        Column(
                            modifier = Modifier.padding(16.dp),
                            verticalArrangement = Arrangement.spacedBy(4.dp)
                        ) {
                            Text(
                                stringResource(R.string.touch_control_creator_empty),
                                fontWeight = FontWeight.SemiBold
                            )
                            Text(
                                stringResource(R.string.touch_control_creator_empty_desc),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                    }
                } else {
                    LazyRow(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        items(library.controls, key = { it.id }) { control ->
                            ControlLibraryCard(
                                control = if (control.id == draft.id) draft else control,
                                selected = control.id == draft.id,
                                onClick = { select(control) }
                            )
                        }
                    }
                }
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    OutlinedButton(
                        onClick = { createControl(draft) },
                        enabled = isProUnlocked &&
                            draft.id != previewSeed.id &&
                            library.controls.size < CustomTouchControlLibrary.MAX_CONTROLS,
                        shape = CreatorControlShape,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Rounded.ContentCopy, contentDescription = null)
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.touch_control_creator_duplicate))
                    }
                    OutlinedButton(
                        onClick = { deleteCandidate = draft },
                        enabled = isProUnlocked && draft.id != previewSeed.id,
                        shape = CreatorControlShape,
                        modifier = Modifier.weight(1f)
                    ) {
                        Icon(Icons.Rounded.Delete, contentDescription = null)
                        Spacer(Modifier.size(8.dp))
                        Text(stringResource(R.string.touch_control_creator_delete))
                    }
                }
            }
        }
        item {
            CreatorSection(title = stringResource(R.string.touch_control_creator_live_canvas)) {
                Text(
                    stringResource(R.string.touch_control_creator_drag_hint),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                ControlCanvasPreview(
                    controls = if (library.controls.isEmpty()) {
                        listOf(draft)
                    } else {
                        library.controls.map { if (it.id == draft.id) draft else it }
                    },
                    selectedId = draft.id,
                    onPositionChange = { x, y ->
                        updateDraft {
                            it.copy(
                                positionX = x.coerceIn(0f, 1f),
                                positionY = y.coerceIn(0f, 1f),
                                updatedAtMillis = System.currentTimeMillis()
                            )
                        }
                    }
                )
            }
        }
        item {
            CreatorSection(title = stringResource(R.string.touch_control_creator_identity)) {
                OutlinedTextField(
                    value = draft.name,
                    onValueChange = { value ->
                        updateDraft {
                            it.copy(
                                name = value.take(CustomTouchControl.MAX_NAME_LENGTH),
                                updatedAtMillis = System.currentTimeMillis()
                            )
                        }
                    },
                    label = { Text(stringResource(R.string.touch_control_creator_name)) },
                    singleLine = true,
                    shape = CreatorControlShape,
                    modifier = Modifier.fillMaxWidth()
                )
                Text(
                    stringResource(R.string.touch_control_creator_action),
                    style = MaterialTheme.typography.labelLarge,
                    fontWeight = FontWeight.SemiBold
                )
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    CustomTouchControl.ALLOWED_ACTION_IDS.forEach { action ->
                        FilterChip(
                            selected = draft.actionId == action,
                            onClick = {
                                updateDraft {
                                    it.copy(
                                        actionId = action,
                                        label = CustomTouchControl.defaultLabelFor(action),
                                        updatedAtMillis = System.currentTimeMillis()
                                    )
                                }
                            },
                            label = { Text(actionLabel(action)) },
                            leadingIcon = if (draft.actionId == action) {
                                { Icon(Icons.Rounded.Check, contentDescription = null) }
                            } else {
                                null
                            },
                            shape = RoundedCornerShape(14.dp)
                        )
                    }
                }
            }
        }
        item {
            CreatorSection(title = stringResource(R.string.touch_control_creator_content)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(10.dp)
                ) {
                    FilterChip(
                        selected = draft.content == CustomTouchControlContent.SYMBOL,
                        onClick = {
                            updateDraft { it.copy(content = CustomTouchControlContent.SYMBOL) }
                        },
                        label = { Text(stringResource(R.string.touch_control_creator_content_symbol)) },
                        shape = CreatorControlShape,
                        modifier = Modifier.weight(1f)
                    )
                    FilterChip(
                        selected = draft.content == CustomTouchControlContent.TEXT,
                        onClick = {
                            updateDraft { it.copy(content = CustomTouchControlContent.TEXT) }
                        },
                        label = { Text(stringResource(R.string.touch_control_creator_content_text)) },
                        shape = CreatorControlShape,
                        modifier = Modifier.weight(1f)
                    )
                }
                OutlinedTextField(
                    value = draft.label,
                    onValueChange = { value ->
                        updateDraft { it.copy(label = value.take(CustomTouchControl.MAX_LABEL_LENGTH)) }
                    },
                    label = { Text(stringResource(R.string.touch_control_creator_label)) },
                    enabled = draft.content == CustomTouchControlContent.TEXT,
                    singleLine = true,
                    shape = CreatorControlShape,
                    modifier = Modifier.fillMaxWidth()
                )
                Text(
                    stringResource(R.string.touch_control_creator_shape),
                    style = MaterialTheme.typography.labelLarge,
                    fontWeight = FontWeight.SemiBold
                )
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    CustomTouchControlShape.entries.forEach { shape ->
                        FilterChip(
                            selected = draft.shape == shape,
                            onClick = { updateDraft { it.copy(shape = shape) } },
                            label = { Text(stringResource(shape.titleRes())) },
                            shape = RoundedCornerShape(14.dp)
                        )
                    }
                }
            }
        }
        item {
            CreatorSection(title = stringResource(R.string.touch_control_creator_geometry)) {
                IntCreatorSlider(
                    title = stringResource(R.string.touch_control_creator_width),
                    value = draft.widthDp,
                    range = CustomTouchControl.MIN_SIZE_DP..CustomTouchControl.MAX_SIZE_DP,
                    suffix = " dp",
                    onValueChange = { value ->
                        updateDraft { control -> control.copy(widthDp = value) }
                    }
                )
                IntCreatorSlider(
                    title = stringResource(R.string.touch_control_creator_height),
                    value = draft.heightDp,
                    range = CustomTouchControl.MIN_SIZE_DP..CustomTouchControl.MAX_SIZE_DP,
                    suffix = " dp",
                    onValueChange = { value ->
                        updateDraft { control -> control.copy(heightDp = value) }
                    }
                )
                IntCreatorSlider(
                    title = stringResource(R.string.touch_control_creator_corner),
                    value = draft.cornerDp,
                    range = 0..CustomTouchControl.MAX_CORNER_DP,
                    suffix = " dp",
                    onValueChange = { value ->
                        updateDraft { control -> control.copy(cornerDp = value) }
                    }
                )
                PercentCreatorSlider(
                    title = stringResource(R.string.touch_control_creator_position_x),
                    value = draft.positionX,
                    onValueChange = { updateDraft { control -> control.copy(positionX = it) } }
                )
                PercentCreatorSlider(
                    title = stringResource(R.string.touch_control_creator_position_y),
                    value = draft.positionY,
                    onValueChange = { updateDraft { control -> control.copy(positionY = it) } }
                )
            }
        }
        item {
            CreatorSection(title = stringResource(R.string.touch_control_creator_appearance)) {
                CreatorColorEditor(
                    title = stringResource(R.string.touch_control_creator_fill),
                    value = draft.fillColor,
                    onValueChange = { updateDraft { control -> control.copy(fillColor = it) } }
                )
                CreatorColorEditor(
                    title = stringResource(R.string.touch_control_creator_foreground),
                    value = draft.contentColor,
                    onValueChange = { updateDraft { control -> control.copy(contentColor = it) } }
                )
                CreatorColorEditor(
                    title = stringResource(R.string.touch_control_creator_border),
                    value = draft.borderColor,
                    onValueChange = { updateDraft { control -> control.copy(borderColor = it) } }
                )
                FloatCreatorSlider(
                    title = stringResource(R.string.touch_control_creator_border_width),
                    value = draft.borderWidthDp,
                    range = 0f..CustomTouchControl.MAX_BORDER_DP,
                    suffix = " dp",
                    onValueChange = { updateDraft { control -> control.copy(borderWidthDp = it) } }
                )
                IntCreatorSlider(
                    title = stringResource(R.string.touch_control_creator_opacity),
                    value = draft.opacity,
                    range = CustomTouchControl.MIN_OPACITY..CustomTouchControl.MAX_OPACITY,
                    suffix = "%",
                    onValueChange = { value ->
                        updateDraft { control -> control.copy(opacity = value) }
                    }
                )
            }
        }
        item {
            CreatorSection(title = stringResource(R.string.touch_control_creator_interaction)) {
                IntCreatorSlider(
                    title = stringResource(R.string.touch_control_creator_pressed_scale),
                    value = draft.pressedScalePercent,
                    range = CustomTouchControl.MIN_PRESSED_SCALE_PERCENT..
                        CustomTouchControl.MAX_PRESSED_SCALE_PERCENT,
                    suffix = "%",
                    onValueChange = { value ->
                        updateDraft { control -> control.copy(pressedScalePercent = value) }
                    }
                )
                CreatorSwitchRow(
                    title = stringResource(R.string.touch_control_creator_haptics),
                    description = stringResource(R.string.touch_control_creator_haptics_desc),
                    checked = draft.haptics,
                    onCheckedChange = { checked ->
                        updateDraft { it.copy(haptics = checked) }
                    }
                )
                CreatorSwitchRow(
                    title = stringResource(R.string.touch_control_creator_enabled),
                    description = stringResource(R.string.touch_control_creator_enabled_desc),
                    checked = draft.enabled,
                    onCheckedChange = { checked ->
                        updateDraft { it.copy(enabled = checked) }
                    }
                )
            }
        }
        item {
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp)
            ) {
                OutlinedButton(
                    onClick = {
                        updateDraft {
                            CustomTouchControl(
                                id = it.id,
                                name = it.name,
                                createdAtMillis = it.createdAtMillis,
                                updatedAtMillis = System.currentTimeMillis()
                            )
                        }
                    },
                    shape = CreatorControlShape,
                    modifier = Modifier.weight(1f)
                ) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null)
                    Spacer(Modifier.size(8.dp))
                    Text(stringResource(R.string.touch_control_creator_reset))
                }
                Button(
                    onClick = { onSave(library.sanitized()) },
                    enabled = isProUnlocked && library.controls.isNotEmpty(),
                    shape = CreatorControlShape,
                    modifier = Modifier
                        .weight(1f)
                        .testTag("touch_control_creator_save")
                ) {
                    Icon(Icons.Rounded.Check, contentDescription = null)
                    Spacer(Modifier.size(8.dp))
                    Text(stringResource(R.string.touch_control_creator_save))
                }
            }
        }
    }
}

@Composable
private fun CreatorSection(
    title: String,
    trailing: @Composable (() -> Unit)? = null,
    content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = CreatorCardShape,
        color = MaterialTheme.colorScheme.surface,
        border = BorderStroke(
            1.dp,
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.45f)
        ),
        tonalElevation = 2.dp
    ) {
        Column(
            modifier = Modifier.padding(18.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    title,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                    modifier = Modifier.weight(1f)
                )
                trailing?.invoke()
            }
            content()
        }
    }
}

@Composable
private fun ControlLibraryCard(
    control: CustomTouchControl,
    selected: Boolean,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .width(176.dp)
            .clickable(onClick = onClick),
        shape = CreatorControlShape,
        color = if (selected) {
            MaterialTheme.colorScheme.primaryContainer
        } else {
            MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.38f)
        },
        border = BorderStroke(
            1.dp,
            if (selected) MaterialTheme.colorScheme.primary
            else MaterialTheme.colorScheme.outlineVariant
        )
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(9.dp)
        ) {
            Box(modifier = Modifier.height(58.dp).fillMaxWidth(), contentAlignment = Alignment.Center) {
                CustomControlVisual(control, pressed = false, modifier = Modifier.size(54.dp))
            }
            Text(control.name, fontWeight = FontWeight.SemiBold, maxLines = 1)
            Text(
                actionLabel(control.actionId),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}

@Composable
private fun ControlCanvasPreview(
    controls: List<CustomTouchControl>,
    selectedId: String,
    onPositionChange: (Float, Float) -> Unit
) {
    BoxWithConstraints(
        modifier = Modifier
            .fillMaxWidth()
            .aspectRatio(16f / 9f)
            .clip(RoundedCornerShape(22.dp))
            .background(Color(0xFF080B12))
    ) {
        val density = androidx.compose.ui.platform.LocalDensity.current
        val canvasWidthPx = with(density) { maxWidth.toPx() }
        val canvasHeightPx = with(density) { maxHeight.toPx() }
        Box(
            Modifier
                .fillMaxSize()
                .background(
                    androidx.compose.ui.graphics.Brush.linearGradient(
                        listOf(Color(0xFF101A2A), Color(0xFF090B10), Color(0xFF1C1022))
                    )
                )
        )
        Text(
            "GAME PREVIEW",
            modifier = Modifier.align(Alignment.TopCenter).padding(top = 12.dp),
            style = MaterialTheme.typography.labelSmall,
            color = Color.White.copy(alpha = 0.4f),
            letterSpacing = androidx.compose.ui.unit.TextUnit.Unspecified
        )
        controls.filter { it.enabled || it.id == selectedId }.forEach { control ->
            val widthPx = with(density) { control.widthDp.dp.toPx() }
            val heightPx = with(density) { control.heightDp.dp.toPx() }
            val travelX = (canvasWidthPx - widthPx).coerceAtLeast(1f)
            val travelY = (canvasHeightPx - heightPx).coerceAtLeast(1f)
            val selected = control.id == selectedId
            CustomControlVisual(
                control = control,
                pressed = selected,
                modifier = Modifier
                    .offset {
                        IntOffset(
                            (control.positionX * travelX).roundToInt(),
                            (control.positionY * travelY).roundToInt()
                        )
                    }
                    .size(control.widthDp.dp, control.heightDp.dp)
                    .pointerInput(control.id, travelX, travelY) {
                        if (!selected) return@pointerInput
                        detectDragGestures { change, dragAmount ->
                            change.consume()
                            onPositionChange(
                                control.positionX + dragAmount.x / travelX,
                                control.positionY + dragAmount.y / travelY
                            )
                        }
                    }
            )
        }
    }
}

@Composable
fun CustomControlVisual(
    control: CustomTouchControl,
    pressed: Boolean,
    modifier: Modifier = Modifier
) {
    val shape = control.composeShape()
    val opacity = control.opacity / 100f
    Surface(
        modifier = modifier.scale(if (pressed) control.pressedScalePercent / 100f else 1f),
        shape = shape,
        color = Color(control.fillColor).copy(alpha = Color(control.fillColor).alpha * opacity),
        contentColor = Color(control.contentColor).copy(alpha = opacity),
        border = control.borderWidthDp.takeIf { it > 0f }?.let {
            BorderStroke(
                it.dp,
                Color(control.borderColor).copy(alpha = Color(control.borderColor).alpha * opacity)
            )
        }
    ) {
        Box(contentAlignment = Alignment.Center) {
            Text(
                text = if (control.content == CustomTouchControlContent.SYMBOL) {
                    CustomTouchControl.defaultLabelFor(control.actionId)
                } else {
                    control.label
                },
                color = Color(control.contentColor).copy(alpha = opacity),
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center,
                maxLines = 1,
                style = MaterialTheme.typography.labelLarge
            )
        }
    }
}

fun CustomTouchControl.composeShape(): Shape = when (shape) {
    CustomTouchControlShape.CIRCLE -> CircleShape
    CustomTouchControlShape.ROUNDED -> RoundedCornerShape(cornerDp.dp)
    CustomTouchControlShape.SQUARE -> RoundedCornerShape(0.dp)
    CustomTouchControlShape.PILL -> RoundedCornerShape(50)
}

@Composable
private fun CreatorColorEditor(
    title: String,
    value: Int,
    onValueChange: (Int) -> Unit
) {
    var hex by remember(value) { mutableStateOf(value.toRgbHex()) }
    val color = Color(value)
    Surface(
        shape = CreatorControlShape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.32f)
    ) {
        Column(
            modifier = Modifier.padding(14.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(12.dp)
            ) {
                Box(Modifier.size(38.dp).clip(CircleShape).background(color))
                Text(title, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                Text(value.toRgbHex(), color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            OutlinedTextField(
                value = hex,
                onValueChange = { input ->
                    hex = input.take(7).uppercase()
                    input.parseRgbHex()?.let(onValueChange)
                },
                label = { Text(stringResource(R.string.touch_control_creator_hex)) },
                singleLine = true,
                keyboardOptions = KeyboardOptions(capitalization = KeyboardCapitalization.Characters),
                shape = CreatorControlShape,
                modifier = Modifier.fillMaxWidth()
            )
            val red = (value ushr 16) and 0xFF
            val green = (value ushr 8) and 0xFF
            val blue = value and 0xFF
            ColorChannelSlider("R", red) { onValueChange(AndroidColor.rgb(it, green, blue)) }
            ColorChannelSlider("G", green) { onValueChange(AndroidColor.rgb(red, it, blue)) }
            ColorChannelSlider("B", blue) { onValueChange(AndroidColor.rgb(red, green, it)) }
        }
    }
}

@Composable
private fun ColorChannelSlider(label: String, value: Int, onValueChange: (Int) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("$label  $value", modifier = Modifier.width(58.dp))
        Slider(
            value = value.toFloat(),
            onValueChange = { onValueChange(it.roundToInt().coerceIn(0, 255)) },
            valueRange = 0f..255f,
            modifier = Modifier.weight(1f)
        )
    }
}

@Composable
private fun IntCreatorSlider(
    title: String,
    value: Int,
    range: IntRange,
    suffix: String,
    onValueChange: (Int) -> Unit
) {
    Column {
        CreatorSliderHeader(title, "$value$suffix")
        Slider(
            value = value.toFloat(),
            onValueChange = { onValueChange(it.roundToInt().coerceIn(range)) },
            valueRange = range.first.toFloat()..range.last.toFloat()
        )
    }
}

@Composable
private fun FloatCreatorSlider(
    title: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    suffix: String,
    onValueChange: (Float) -> Unit
) {
    Column {
        CreatorSliderHeader(title, "${"%.1f".format(value)}$suffix")
        Slider(value = value, onValueChange = onValueChange, valueRange = range)
    }
}

@Composable
private fun PercentCreatorSlider(
    title: String,
    value: Float,
    onValueChange: (Float) -> Unit
) {
    Column {
        CreatorSliderHeader(title, "${(value * 100).roundToInt()}%")
        Slider(value = value, onValueChange = onValueChange, valueRange = 0f..1f)
    }
}

@Composable
private fun CreatorSliderHeader(title: String, value: String) {
    Row(modifier = Modifier.fillMaxWidth()) {
        Text(title, modifier = Modifier.weight(1f))
        Text(value, color = MaterialTheme.colorScheme.primary, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
private fun CreatorSwitchRow(
    title: String,
    description: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = CreatorControlShape,
        color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.32f)
    ) {
        Row(
            modifier = Modifier.padding(14.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(title, fontWeight = FontWeight.SemiBold)
                Text(
                    description,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Switch(checked = checked, onCheckedChange = onCheckedChange)
        }
    }
}

private fun CustomTouchControlShape.titleRes(): Int = when (this) {
    CustomTouchControlShape.CIRCLE -> R.string.touch_control_creator_shape_circle
    CustomTouchControlShape.ROUNDED -> R.string.touch_control_creator_shape_rounded
    CustomTouchControlShape.SQUARE -> R.string.touch_control_creator_shape_square
    CustomTouchControlShape.PILL -> R.string.touch_control_creator_shape_pill
}

fun actionLabel(actionId: String): String = when (actionId) {
    "up" -> "D-pad Up"
    "down" -> "D-pad Down"
    "left" -> "D-pad Left"
    "right" -> "D-pad Right"
    "triangle", "cross", "square", "circle" ->
        actionId.replaceFirstChar { it.uppercase() }
    "select", "start", "pressure" -> actionId.replaceFirstChar { it.uppercase() }
    else -> actionId.uppercase()
}

private fun Int.toRgbHex(): String = "#%06X".format(this and 0x00FFFFFF)

private fun String.parseRgbHex(): Int? {
    val normalized = trim().removePrefix("#")
    if (normalized.length != 6) return null
    return normalized.toLongOrNull(16)?.let { (it or 0xFF000000L).toInt() }
}
