package com.sbro.emucorex.ui.settings

import android.annotation.SuppressLint
import android.content.pm.ActivityInfo
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.displayCutoutPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Refresh
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material3.Button
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.ui.common.NavigationBackButton
import kotlinx.coroutines.launch
import kotlin.math.roundToInt

@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun ControlsEditorScreen(
    onBackClick: () -> Unit,
    viewModel: ControlsEditorViewModel = viewModel()
) {
    val layoutState by viewModel.layoutState.collectAsState()
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    LocalConfiguration.current
    LocalDensity.current

    LaunchedEffect(Unit) {
        val activity = context as? android.app.Activity ?: return@LaunchedEffect
        activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE
    }

    DisposableEffect(Unit) {
        val activity = context as? android.app.Activity
        val originalOrientation = activity?.requestedOrientation
        onDispose {
            activity?.requestedOrientation = originalOrientation ?: ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED
        }
    }

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(Color.Black)
    ) {
        maxWidth
        maxHeight

        Box(
            modifier = Modifier
                .fillMaxSize()
                .background(
                    Brush.verticalGradient(
                        colors = listOf(Color(0xFF1A1A1A), Color(0xFF0D0D0D))
                    )
                )
        )

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .statusBarsPadding()
                .displayCutoutPadding()
                .padding(horizontal = 16.dp, vertical = 8.dp)
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(Color.Black.copy(alpha = 0.7f))
                    .padding(horizontal = 8.dp, vertical = 6.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                NavigationBackButton(
                    onClick = onBackClick,
                    containerColor = Color.White.copy(alpha = 0.08f),
                    contentColor = Color.White,
                    tonalElevation = 0.dp,
                    shadowElevation = 0.dp
                )

                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = stringResource(R.string.controls_editor_title),
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.Bold),
                        color = Color.White
                    )
                }

                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color.White.copy(alpha = 0.1f))
                        .padding(horizontal = 8.dp, vertical = 4.dp)
                ) {
                    Text(
                        text = "Size", 
                        style = MaterialTheme.typography.labelSmall, 
                        color = Color.White.copy(alpha = 0.7f)
                    )
                    Slider(
                        value = layoutState.stickScale.toFloat(),
                        onValueChange = { viewModel.updateStickScale(it.toInt()) },
                        valueRange = 50f..200f,
                        modifier = Modifier.width(80.dp),
                        colors = SliderDefaults.colors(thumbColor = Color.White, activeTrackColor = MaterialTheme.colorScheme.primary)
                    )
                }

                IconButton(
                    onClick = { viewModel.resetLayout() },
                    modifier = Modifier
                        .size(36.dp)
                        .clip(RoundedCornerShape(10.dp))
                        .background(Color.White.copy(alpha = 0.1f))
                ) {
                    Icon(Icons.Rounded.Refresh, contentDescription = null, tint = Color.White, modifier = Modifier.size(18.dp))
                }

                Button(
                    onClick = { 
                        scope.launch {
                            try { viewModel.saveLayout(); onBackClick() } catch (_: Exception) { onBackClick() }
                        }
                    },
                    modifier = Modifier.height(36.dp),
                    shape = RoundedCornerShape(10.dp),
                    contentPadding = PaddingValues(horizontal = 12.dp)
                ) {
                    Icon(Icons.Rounded.Save, contentDescription = null, modifier = Modifier.size(16.dp))
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(stringResource(R.string.controls_editor_save), fontSize = 12.sp, fontWeight = FontWeight.Bold)
                }
            }
        }

        Box(modifier = Modifier.fillMaxSize()) {
            DraggableControl(
                initialOffset = layoutState.dpadOffset,
                onOffsetChange = { viewModel.updateDpadOffset(it) },
                modifier = Modifier.align(Alignment.BottomStart)
            ) {
                Box(
                    modifier = Modifier
                        .size(140.dp)
                        .clip(RoundedCornerShape(24.dp))
                        .background(Color.White.copy(alpha = 0.1f))
                        .border(1.dp, Color.White.copy(alpha = 0.2f), RoundedCornerShape(24.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Text("D-PAD", color = Color.White.copy(alpha = 0.5f), fontWeight = FontWeight.Bold)
                }
            }
            
            // Left Stick
            DraggableControl(
                initialOffset = layoutState.lstickOffset,
                onOffsetChange = { viewModel.updateLstickOffset(it) },
                modifier = Modifier.align(Alignment.BottomStart).offset(x = 150.dp)
            ) {
                val size = (120 * (layoutState.stickScale / 100f)).dp
                Box(
                    modifier = Modifier
                        .size(size)
                        .clip(CircleShape)
                        .background(Color.White.copy(alpha = 0.1f))
                        .border(1.dp, Color.White.copy(alpha = 0.2f), CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    Text("L-STICK", color = Color.White.copy(alpha = 0.5f), fontSize = 10.sp)
                }
            }
            
            // Right Stick
            DraggableControl(
                initialOffset = layoutState.rstickOffset,
                onOffsetChange = { viewModel.updateRstickOffset(it) },
                modifier = Modifier.align(Alignment.BottomEnd).offset(x = (-150).dp)
            ) {
                val size = (120 * (layoutState.stickScale / 100f)).dp
                Box(
                    modifier = Modifier
                        .size(size)
                        .clip(CircleShape)
                        .background(Color.White.copy(alpha = 0.1f))
                        .border(1.dp, Color.White.copy(alpha = 0.2f), CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    Text("R-STICK", color = Color.White.copy(alpha = 0.5f), fontSize = 10.sp)
                }
            }
            
            // Actions
            DraggableControl(
                initialOffset = layoutState.actionOffset,
                onOffsetChange = { viewModel.updateActionOffset(it) },
                modifier = Modifier.align(Alignment.BottomEnd)
            ) {
                Box(
                    modifier = Modifier
                        .size(140.dp)
                        .clip(RoundedCornerShape(24.dp))
                        .background(Color.White.copy(alpha = 0.1f))
                        .border(1.dp, Color.White.copy(alpha = 0.2f), RoundedCornerShape(24.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Text("ACTIONS", color = Color.White.copy(alpha = 0.5f), fontWeight = FontWeight.Bold)
                }
            }
            
            // L-Buttons (L1, L2)
            DraggableControl(
                initialOffset = layoutState.lbtnOffset,
                onOffsetChange = { viewModel.updateLbtnOffset(it) },
                modifier = Modifier.align(Alignment.TopStart).padding(top = 80.dp)
            ) {
                Box(
                    modifier = Modifier
                        .width(140.dp)
                        .height(50.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color.White.copy(alpha = 0.1f))
                        .border(1.dp, Color.White.copy(alpha = 0.2f), RoundedCornerShape(12.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Text("L1 / L2", color = Color.White.copy(alpha = 0.5f))
                }
            }
            
            // R-Buttons (R1, R2)
            DraggableControl(
                initialOffset = layoutState.rbtnOffset,
                onOffsetChange = { viewModel.updateRbtnOffset(it) },
                modifier = Modifier.align(Alignment.TopEnd).padding(top = 80.dp)
            ) {
                Box(
                    modifier = Modifier
                        .width(140.dp)
                        .height(50.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color.White.copy(alpha = 0.1f))
                        .border(1.dp, Color.White.copy(alpha = 0.2f), RoundedCornerShape(12.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Text("R1 / R2", color = Color.White.copy(alpha = 0.5f))
                }
            }
            
            // Center (Start, Select)
            DraggableControl(
                initialOffset = layoutState.centerOffset,
                onOffsetChange = { viewModel.updateCenterOffset(it) },
                modifier = Modifier.align(Alignment.BottomCenter).padding(bottom = 80.dp)
            ) {
                Box(
                    modifier = Modifier
                        .width(120.dp)
                        .height(60.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(Color.White.copy(alpha = 0.1f))
                        .border(1.dp, Color.White.copy(alpha = 0.2f), RoundedCornerShape(12.dp)),
                    contentAlignment = Alignment.Center
                ) {
                    Text("SELECT / START", color = Color.White.copy(alpha = 0.4f), fontSize = 10.sp)
                }
            }
        }
    }
}

@Composable
private fun DraggableControl(
    initialOffset: Pair<Float, Float>,
    onOffsetChange: (Pair<Float, Float>) -> Unit,
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit
) {
    var offsetX by remember(initialOffset) { mutableFloatStateOf(initialOffset.first) }
    var offsetY by remember(initialOffset) { mutableFloatStateOf(initialOffset.second) }

    Box(
        modifier = modifier
            .offset { IntOffset(offsetX.roundToInt(), offsetY.roundToInt()) }
            .pointerInput(initialOffset) {
                detectDragGestures(
                    onDragEnd = { onOffsetChange(offsetX to offsetY) }
                ) { change, dragAmount ->
                    change.consume()
                    offsetX += dragAmount.x
                    offsetY += dragAmount.y
                }
            }
    ) {
        content()
    }
}
