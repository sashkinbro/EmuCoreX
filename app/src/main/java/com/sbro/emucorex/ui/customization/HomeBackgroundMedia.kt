package com.sbro.emucorex.ui.customization

import android.graphics.ImageDecoder
import android.graphics.drawable.AnimatedImageDrawable
import android.media.MediaPlayer
import android.os.PowerManager
import android.view.ViewGroup
import android.widget.ImageView
import android.widget.VideoView
import androidx.annotation.DrawableRes
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.sbro.emucorex.R
import com.sbro.emucorex.data.HomeBackgroundPreset
import com.sbro.emucorex.data.HomeBackgroundType
import java.io.File
import kotlin.math.max

@Composable
fun HomeBackgroundMedia(
    type: HomeBackgroundType,
    file: File?,
    preset: HomeBackgroundPreset = HomeBackgroundPreset.OLYMPUS,
    modifier: Modifier = Modifier,
    revision: Int = 0
) {
    if (type == HomeBackgroundType.BUILT_IN) {
        BoxWithConstraints(modifier = modifier) {
            Image(
                painter = painterResource(preset.drawableResource),
                contentDescription = null,
                contentScale = ContentScale.Crop,
                alignment = if (maxHeight > maxWidth) preset.portraitFocus else Alignment.Center,
                modifier = Modifier.matchParentSize()
            )
        }
        return
    }
    if (file == null || !file.isFile) return
    key(type, file.absolutePath, file.lastModified(), file.length(), revision) {
        when (type) {
            HomeBackgroundType.IMAGE,
            HomeBackgroundType.GIF -> ImageBackground(file = file, modifier = modifier)
            HomeBackgroundType.VIDEO -> VideoBackground(file = file, modifier = modifier)
            HomeBackgroundType.NONE,
            HomeBackgroundType.BUILT_IN -> Unit
        }
    }
}

val HomeBackgroundPreset.drawableResource: Int
    @DrawableRes get() = when (this) {
        HomeBackgroundPreset.OLYMPUS -> R.drawable.home_background_olympus
        HomeBackgroundPreset.NEON_RACING -> R.drawable.home_background_neon_racing
        HomeBackgroundPreset.TROPICAL_RUINS -> R.drawable.home_background_tropical_ruins
        HomeBackgroundPreset.COLOSSUS_VALLEY -> R.drawable.home_background_colossus_valley
        HomeBackgroundPreset.STEALTH_JUNGLE -> R.drawable.home_background_stealth_jungle
        HomeBackgroundPreset.GOTHIC_CITY -> R.drawable.home_background_gothic_city
        HomeBackgroundPreset.WEST_COAST -> R.drawable.home_background_west_coast
        HomeBackgroundPreset.SAMURAI_NIGHT -> R.drawable.home_background_samurai_night
        HomeBackgroundPreset.CRYSTAL_PILGRIMAGE -> R.drawable.home_background_crystal_pilgrimage
    }

private val HomeBackgroundPreset.portraitFocus: Alignment
    get() = when (this) {
        HomeBackgroundPreset.OLYMPUS,
        HomeBackgroundPreset.GOTHIC_CITY,
        HomeBackgroundPreset.WEST_COAST,
        HomeBackgroundPreset.SAMURAI_NIGHT -> Alignment.CenterStart
        HomeBackgroundPreset.NEON_RACING,
        HomeBackgroundPreset.TROPICAL_RUINS,
        HomeBackgroundPreset.COLOSSUS_VALLEY,
        HomeBackgroundPreset.STEALTH_JUNGLE,
        HomeBackgroundPreset.CRYSTAL_PILGRIMAGE -> Alignment.CenterEnd
    }

@Composable
private fun ImageBackground(file: File, modifier: Modifier) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val context = androidx.compose.ui.platform.LocalContext.current
    val powerManager = remember(context) { context.getSystemService(PowerManager::class.java) }
    val allowAnimation = powerManager?.isPowerSaveMode != true
    val drawable = remember(file.absolutePath, file.lastModified()) {
        runCatching {
            ImageDecoder.decodeDrawable(ImageDecoder.createSource(file)) { decoder, info, _ ->
                var sampleSize = 1
                while (max(info.size.width, info.size.height) / sampleSize > 2048) {
                    sampleSize *= 2
                }
                decoder.setTargetSampleSize(sampleSize)
            }
        }.getOrNull()
    } ?: return
    var imageView by remember(file.absolutePath) { mutableStateOf<ImageView?>(null) }

    DisposableEffect(lifecycleOwner, drawable, allowAnimation) {
        val animatedDrawable = drawable as? AnimatedImageDrawable
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> if (allowAnimation) animatedDrawable?.start()
                Lifecycle.Event.ON_PAUSE,
                Lifecycle.Event.ON_STOP,
                Lifecycle.Event.ON_DESTROY -> animatedDrawable?.stop()
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            animatedDrawable?.stop()
            imageView?.setImageDrawable(null)
        }
    }

    AndroidView(
        modifier = modifier,
        factory = { context ->
            ImageView(context).apply {
                layoutParams = ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )
                scaleType = ImageView.ScaleType.CENTER_CROP
                setImageDrawable(drawable)
                if (allowAnimation) (drawable as? AnimatedImageDrawable)?.start()
                imageView = this
            }
        },
        update = { imageView ->
            if (imageView.drawable !== drawable) imageView.setImageDrawable(drawable)
            if (allowAnimation) (drawable as? AnimatedImageDrawable)?.start()
        },
        onRelease = {
            (drawable as? AnimatedImageDrawable)?.stop()
            it.setImageDrawable(null)
        }
    )
}

@Composable
private fun VideoBackground(file: File, modifier: Modifier) {
    val lifecycleOwner = LocalLifecycleOwner.current
    val context = androidx.compose.ui.platform.LocalContext.current
    val powerManager = remember(context) { context.getSystemService(PowerManager::class.java) }
    val allowPlayback = powerManager?.isPowerSaveMode != true
    var videoView by remember(file.absolutePath) { mutableStateOf<VideoView?>(null) }

    DisposableEffect(lifecycleOwner, videoView) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> if (allowPlayback) videoView?.start()
                Lifecycle.Event.ON_PAUSE -> videoView?.pause()
                Lifecycle.Event.ON_DESTROY -> videoView?.stopPlayback()
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            videoView?.pause()
        }
    }

    AndroidView(
        modifier = modifier,
        factory = { context ->
            VideoView(context).apply {
                layoutParams = ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT
                )
                setVideoPath(file.absolutePath)
                setOnPreparedListener { player ->
                    player.isLooping = true
                    player.setVolume(0f, 0f)
                    player.setVideoScalingMode(MediaPlayer.VIDEO_SCALING_MODE_SCALE_TO_FIT_WITH_CROPPING)
                    if (allowPlayback && lifecycleOwner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
                        start()
                    } else {
                        seekTo(1)
                    }
                }
                videoView = this
            }
        },
        update = { view ->
            if (videoView !== view) videoView = view
        },
        onRelease = { view ->
            view.stopPlayback()
            if (videoView === view) videoView = null
        }
    )
}
