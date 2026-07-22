package com.sbro.emucorex.ui.onboarding

import android.annotation.SuppressLint
import android.app.Activity
import android.net.Uri
import android.os.Build
import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.RepeatMode
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.scaleIn
import androidx.compose.animation.scaleOut
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.displayCutoutPadding
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.ArrowForward
import androidx.compose.material.icons.automirrored.rounded.LibraryBooks
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.DeleteOutline
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Info
import androidx.compose.material.icons.rounded.KeyboardArrowDown
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.RadioButtonChecked
import androidx.compose.material.icons.rounded.RadioButtonUnchecked
import androidx.compose.material.icons.rounded.SmartDisplay
import androidx.compose.material.icons.rounded.Star
import com.sbro.emucorex.ui.common.AppAlertDialog as AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.core.EmulatorStorage
import com.sbro.emucorex.core.LocalTvUiEnvironment
import com.sbro.emucorex.core.TvUiMetrics

import com.sbro.emucorex.core.PerformanceProfiles
import com.sbro.emucorex.ui.common.EmulatorDataLocationDialog
import com.sbro.emucorex.ui.common.TvStoragePickerHost
import com.sbro.emucorex.ui.common.TvStorageRequest
import com.sbro.emucorex.ui.common.navigationBarsHorizontalPaddingValues
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.util.Locale
import kotlin.time.Duration.Companion.milliseconds

private enum class DeviceChipsetFamily {
    Snapdragon, MediaTek, Exynos, Tensor, Unknown
}

private data class DeviceChipsetInfo(
    val family: DeviceChipsetFamily,
    val manufacturer: String,
    val socModel: String,
    val hardware: String,
    val deviceModel: String
)

@Suppress("unused")
private fun detectDeviceChipsetInfo(): DeviceChipsetInfo {
    val socManufacturer = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        Build.SOC_MANUFACTURER
    } else {
        ""
    }
    val socModel = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
        Build.SOC_MODEL
    } else {
        ""
    }
    val hardware = listOf(Build.HARDWARE, Build.BOARD, Build.DEVICE)
        .filterNot { it.isNullOrBlank() }
        .joinToString(" / ")
    val deviceModel = listOf(Build.MANUFACTURER, Build.MODEL)
        .filterNot { it.isNullOrBlank() }
        .joinToString(" ")
    val hints = listOf(socManufacturer, socModel, hardware, deviceModel)
        .joinToString(" ")
        .lowercase(Locale.US)
    val family = when {
        listOf("qualcomm", "qcom", "snapdragon", "adreno").any(hints::contains) -> DeviceChipsetFamily.Snapdragon
        listOf("mediatek", "mtk", "mt").any(hints::contains) -> DeviceChipsetFamily.MediaTek
        listOf("exynos", "samsung").any(hints::contains) -> DeviceChipsetFamily.Exynos
        listOf("tensor", "gs101", "gs201", "gs301").any(hints::contains) -> DeviceChipsetFamily.Tensor
        else -> DeviceChipsetFamily.Unknown
    }
    return DeviceChipsetInfo(
        family = family,
        manufacturer = socManufacturer.ifBlank { Build.MANUFACTURER.orEmpty() },
        socModel = socModel.ifBlank { Build.BOARD.orEmpty() },
        hardware = hardware.ifBlank { Build.HARDWARE.orEmpty() },
        deviceModel = deviceModel
    )
}

@OptIn(ExperimentalFoundationApi::class, ExperimentalLayoutApi::class)
@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun OnboardingScreen(
    onComplete: () -> Unit,
    viewModel: OnboardingViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val tvUiEnabled = LocalTvUiEnvironment.current.enabled

    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp && configuration.screenWidthDp >= 600
    var isCompleting by remember { mutableStateOf(false) }
    var showEmulatorDataLocationDialog by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val bottomInset = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()
    val horizontalSystemBarPadding = navigationBarsHorizontalPaddingValues()
    val tvSafeHorizontal = if (tvUiEnabled) {
        TvUiMetrics.safeHorizontalDp(configuration.screenWidthDp).dp
    } else {
        0.dp
    }
    val tvSafeVertical = if (tvUiEnabled) {
        TvUiMetrics.safeVerticalDp(configuration.screenHeightDp).dp
    } else {
        0.dp
    }
    val landscapeSetupScrollState = rememberScrollState()
    val landscapeSetupScrollProgress by remember(landscapeSetupScrollState) {
        derivedStateOf {
            if (landscapeSetupScrollState.maxValue > 0) {
                landscapeSetupScrollState.value.toFloat() / landscapeSetupScrollState.maxValue.toFloat()
            } else {
                0f
            }
        }
    }
    
    val pagerState = rememberPagerState(pageCount = { uiState.totalPages })
    
    LaunchedEffect(pagerState.currentPage) {
        if (pagerState.currentPage != uiState.currentPage) {
            viewModel.setCurrentPage(pagerState.currentPage)
        }
    }
    
    LaunchedEffect(uiState.currentPage) {
        if (pagerState.currentPage != uiState.currentPage) {
            pagerState.animateScrollToPage(uiState.currentPage)
        }
    }

    val biosPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri: Uri? ->
        uri?.let(viewModel::setBiosPath)
    }

    val gamePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        uri?.let(viewModel::setGamePath)
    }
    var tvStorageRequest by remember { mutableStateOf<TvStorageRequest?>(null) }
    TvStoragePickerHost(
        request = tvStorageRequest,
        onDismiss = { tvStorageRequest = null },
        onBiosSelected = viewModel::setBiosPath,
        onGameFolderSelected = viewModel::setGamePath
    )
    val launchBiosPicker = rememberDebouncedClick(
        onClick = {
            if (tvUiEnabled) tvStorageRequest = TvStorageRequest.BIOS_FILE
            else biosPicker.launch(arrayOf("*/*"))
        }
    )
    val launchGamePicker = rememberDebouncedClick(
        onClick = {
            if (tvUiEnabled) tvStorageRequest = TvStorageRequest.GAME_FOLDER
            else gamePicker.launch(null)
        }
    )
    val openEmulatorDataLocationDialog = rememberDebouncedClick(
        onClick = {
            viewModel.refreshEmulatorDataLocations()
            showEmulatorDataLocationDialog = true
        }
    )

    if (showEmulatorDataLocationDialog) {
        EmulatorDataLocationDialog(
            selectedLocation = EmulatorStorage.selectedStandardLocation(
                uiState.emulatorDataPath,
                uiState.sdCardDataPath
            ),
            sdCardAvailable = uiState.sdCardDataPath != null,
            onSelect = { location ->
                showEmulatorDataLocationDialog = false
                viewModel.setEmulatorDataLocation(location)
            },
            onDismiss = { showEmulatorDataLocationDialog = false }
        )
    }

    val proPurchaseMessage = uiState.proPurchaseMessageResId?.let { stringResource(it) }
    LaunchedEffect(proPurchaseMessage) {
        val message = proPurchaseMessage ?: return@LaunchedEffect
        Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        viewModel.clearProPurchaseMessage()
    }

    
    val continueClick = rememberDebouncedClick(
        onClick = {
            if (isCompleting || !uiState.canContinue) return@rememberDebouncedClick
            isCompleting = true
            scope.launch {
                delay(280.milliseconds)
                viewModel.completeOnboarding(onComplete)
            }
        }
    )
    
    val goToPage: (Int) -> Unit = { page ->
        val targetPage = page.coerceIn(0, uiState.totalPages - 1)
        scope.launch {
            pagerState.animateScrollToPage(targetPage)
            viewModel.setCurrentPage(targetPage)
        }
    }

    val nextClick = {
        goToPage(pagerState.currentPage + 1)
    }
    
    val previousClick = {
        goToPage(pagerState.currentPage - 1)
    }

    val contentAlpha by animateFloatAsState(
        targetValue = if (isCompleting) 0.34f else 1f,
        animationSpec = tween(durationMillis = 280),
        label = "onboarding-content-alpha"
    )
    val contentOffset by animateFloatAsState(
        targetValue = if (isCompleting) -32f else 0f,
        animationSpec = tween(durationMillis = 320),
        label = "onboarding-content-offset"
    )
    val backgroundMotion = rememberInfiniteTransition(label = "onboarding-background-motion")
    val orbOneOffsetX by backgroundMotion.animateFloat(
        initialValue = -18f,
        targetValue = 42f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 5200),
            repeatMode = RepeatMode.Reverse
        ),
        label = "orb-one-offset-x"
    )
    val orbOneOffsetY by backgroundMotion.animateFloat(
        initialValue = -12f,
        targetValue = 34f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 6100),
            repeatMode = RepeatMode.Reverse
        ),
        label = "orb-one-offset-y"
    )
    val orbTwoOffsetX by backgroundMotion.animateFloat(
        initialValue = 20f,
        targetValue = -56f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 6800),
            repeatMode = RepeatMode.Reverse
        ),
        label = "orb-two-offset-x"
    )
    val orbTwoOffsetY by backgroundMotion.animateFloat(
        initialValue = 0f,
        targetValue = 58f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 5600),
            repeatMode = RepeatMode.Reverse
        ),
        label = "orb-two-offset-y"
    )

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(
                Brush.verticalGradient(
                    colors = listOf(
                        MaterialTheme.colorScheme.primary.copy(alpha = 0.18f),
                        MaterialTheme.colorScheme.background,
                        MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.45f)
                    )
                )
            )
    ) {
        Box(
            modifier = Modifier
                .padding(start = 28.dp)
                .size(180.dp)
                .graphicsLayer {
                    translationX = orbOneOffsetX
                    translationY = orbOneOffsetY
                }
                .clip(CircleShape)
                .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.12f))
        )
        Box(
            modifier = Modifier
                .align(Alignment.TopEnd)
                .padding(top = 96.dp, end = 20.dp)
                .size(140.dp)
                .graphicsLayer {
                    translationX = orbTwoOffsetX
                    translationY = orbTwoOffsetY
                }
                .clip(CircleShape)
                .background(MaterialTheme.colorScheme.secondary.copy(alpha = 0.1f))
        )

        if (isLandscape) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontalSystemBarPadding)
                    .padding(horizontal = tvSafeHorizontal, vertical = tvSafeVertical)
                    .graphicsLayer {
                        alpha = contentAlpha
                        translationY = contentOffset
                    }
            ) {
                HorizontalPager(
                    state = pagerState,
                    modifier = Modifier.fillMaxSize()
                ) { page ->
                    Row(
                        modifier = Modifier.fillMaxSize(),
                        horizontalArrangement = Arrangement.spacedBy(32.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxSize()
                                .statusBarsPadding()
                                .displayCutoutPadding()
                                .padding(start = 24.dp, bottom = 12.dp),
                            contentAlignment = Alignment.Center
                        ) {
                            when {
                                page < 3 -> OnboardingHero(
                                    page = page,
                                    showSubtitle = false,
                                    modifier = Modifier.padding(bottom = 8.dp)
                                )
                                page == 3 -> OnboardingHeroProfile(
                                    showSubtitle = false,
                                    modifier = Modifier.padding(bottom = 8.dp)
                                )
                                page == 4 -> OnboardingHeroPro(
                                    showSubtitle = false,
                                    modifier = Modifier.padding(bottom = 8.dp)
                                )
                                else -> OnboardingHeroSetup(
                                    showSubtitle = false,
                                    modifier = Modifier.padding(bottom = 8.dp)
                                )
                            }
                        }

                        Box(
                            modifier = Modifier
                                .weight(1f)
                                .fillMaxSize()
                                .padding(bottom = 106.dp + bottomInset),
                            contentAlignment = Alignment.Center
                        ) {
                            if (page < 3) {
                                val subtitleRes = when (page) {
                                    0 -> R.string.onboarding_page_1_subtitle
                                    1 -> R.string.onboarding_page_2_subtitle
                                    2 -> R.string.onboarding_page_3_subtitle
                                    else -> R.string.onboarding_page_4_subtitle
                                }
                                Text(
                                    text = stringResource(subtitleRes),
                                    style = MaterialTheme.typography.bodyLarge.copy(
                                        lineHeight = 28.sp,
                                        fontWeight = FontWeight.Normal,
                                        letterSpacing = 0.sp
                                    ),
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                    textAlign = TextAlign.Center,
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .padding(horizontal = 40.dp)
                                        .statusBarsPadding()
                                        .padding(top = 32.dp)
                                )
                            } else if (page == 3) {
                                Column(
                                    modifier = Modifier
                                        .fillMaxSize()
                                        .verticalScroll(rememberScrollState()),
                                    horizontalAlignment = Alignment.CenterHorizontally,
                                    verticalArrangement = Arrangement.Center
                                ) {
                                    OnboardingPerformanceProfileContent(
                                        selectedProfile = uiState.performanceProfile,
                                        onSelectProfile = viewModel::setPerformanceProfile,
                                        modifier = Modifier.padding(horizontal = 32.dp)
                                    )
                                }
                            } else if (page == 4) {
                                OnboardingProContent(
                                    isProUnlocked = uiState.isProUnlocked,
                                    proPrice = uiState.proPrice,
                                    isProductLoading = uiState.isProProductLoading,
                                    isPurchaseInProgress = uiState.isProPurchaseInProgress,
                                    onPurchase = { (context as? Activity)?.let(viewModel::purchasePro) },
                                    modifier = Modifier.padding(horizontal = 32.dp)
                                )
                            } else {
                                Box(modifier = Modifier.fillMaxSize()) {
                                    Column(
                                        modifier = Modifier
                                            .fillMaxSize()
                                            .verticalScroll(landscapeSetupScrollState),
                                        horizontalAlignment = Alignment.CenterHorizontally,
                                        verticalArrangement = Arrangement.Top
                                    ) {
                                        Spacer(modifier = Modifier.statusBarsPadding().height(24.dp))
                                        
                                        OnboardingSetupContent(
                                            biosPath = uiState.biosPath,
                                            gamePath = uiState.gamePath,
                                            gamePaths = uiState.gamePaths,
                                            emulatorDataPath = uiState.emulatorDataPath,
                                            sdCardDataPath = uiState.sdCardDataPath,
                                            biosValid = uiState.biosValid,
                                            gamePathValid = uiState.gamePathValid,
                                            launchBiosPicker = launchBiosPicker,
                                            launchGamePicker = launchGamePicker,
                                            onRemoveGamePath = viewModel::removeGamePath,
                                            openEmulatorDataLocationDialog = openEmulatorDataLocationDialog,
                                            endInset = 0.dp,
                                            bottomInset = 0.dp,
                                            reserveScrollHintSpace = true,
                                            modifier = Modifier.padding(horizontal = 32.dp)
                                        )
                                        
                                        Spacer(modifier = Modifier.height(24.dp))
                                    }

                                    OnboardingSetupScrollHint(
                                        visible = landscapeSetupScrollState.canScrollForward,
                                        scrollProgress = landscapeSetupScrollProgress,
                                        modifier = Modifier
                                            .align(Alignment.TopStart)
                                            .statusBarsPadding()
                                            .padding(start = 32.dp, top = 80.dp)
                                            .width(36.dp)
                                    )
                                }
                            }
                        }
                    }
                }

                Row(
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                        .fillMaxWidth()
                        .displayCutoutPadding()
                        .padding(bottom = 16.dp + bottomInset),
                    horizontalArrangement = Arrangement.spacedBy(32.dp)
                ) {
                    Spacer(modifier = Modifier.weight(1f))
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxWidth()
                    ) {
                        OnboardingPageIndicator(
                            currentPage = pagerState.currentPage,
                            totalPages = uiState.totalPages,
                            modifier = Modifier.padding(bottom = 20.dp)
                        )
                        OnboardingNavigation(
                            currentPage = pagerState.currentPage,
                            totalPages = uiState.totalPages,
                            canContinue = uiState.canContinue,
                            onNext = nextClick,
                            onPrevious = previousClick,
                            onContinue = continueClick,
                            modifier = Modifier
                                .widthIn(max = 420.dp)
                                .padding(horizontal = 32.dp)
                        )
                    }
                }
            }
        } else {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .padding(horizontalSystemBarPadding)
                    .padding(horizontal = tvSafeHorizontal, vertical = tvSafeVertical)
                    .imePadding()
                    .graphicsLayer {
                        alpha = contentAlpha
                        translationY = contentOffset
                    }
            ) {
                HorizontalPager(
                    state = pagerState,
                    modifier = Modifier.fillMaxSize()
                ) { page ->
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .verticalScroll(rememberScrollState())
                            .statusBarsPadding()
                            .padding(
                                start = 24.dp,
                                end = 24.dp,
                                top = 48.dp,
                                bottom = 160.dp + bottomInset
                            ),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        when (page) {
                            0, 1, 2 -> OnboardingHero(page = page)
                            3 -> {
                                OnboardingHeroProfile()
                                Spacer(modifier = Modifier.height(32.dp))
                                OnboardingPerformanceProfileContent(
                                    selectedProfile = uiState.performanceProfile,
                                    onSelectProfile = viewModel::setPerformanceProfile
                                )
                            }
                            4 -> {
                                OnboardingHeroPro()
                                Spacer(modifier = Modifier.height(32.dp))
                                OnboardingProContent(
                                    isProUnlocked = uiState.isProUnlocked,
                                    proPrice = uiState.proPrice,
                                    isProductLoading = uiState.isProProductLoading,
                                    isPurchaseInProgress = uiState.isProPurchaseInProgress,
                                    onPurchase = { (context as? Activity)?.let(viewModel::purchasePro) }
                                )
                            }
                            5 -> {
                                OnboardingHeroSetup()
                                Spacer(modifier = Modifier.height(32.dp))
                                OnboardingSetupContent(
                                    biosPath = uiState.biosPath,
                                    gamePath = uiState.gamePath,
                                    gamePaths = uiState.gamePaths,
                                    emulatorDataPath = uiState.emulatorDataPath,
                                    sdCardDataPath = uiState.sdCardDataPath,
                                    biosValid = uiState.biosValid,
                                    gamePathValid = uiState.gamePathValid,
                                    launchBiosPicker = launchBiosPicker,
                                    launchGamePicker = launchGamePicker,
                                    onRemoveGamePath = viewModel::removeGamePath,
                                    openEmulatorDataLocationDialog = openEmulatorDataLocationDialog,
                                    endInset = 0.dp,
                                    bottomInset = 0.dp
                                )
                            }
                        }
                    }
                }

                Column(
                    modifier = Modifier
                        .align(Alignment.BottomCenter)
                        .fillMaxWidth()
                        .padding(bottom = 24.dp + bottomInset)
                        .padding(horizontal = 24.dp),
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    OnboardingPageIndicator(
                        currentPage = pagerState.currentPage,
                        totalPages = uiState.totalPages,
                        modifier = Modifier.padding(bottom = 24.dp)
                    )
                    OnboardingNavigation(
                        currentPage = pagerState.currentPage,
                        totalPages = uiState.totalPages,
                        canContinue = uiState.canContinue,
                        onNext = nextClick,
                        onPrevious = previousClick,
                        onContinue = continueClick
                    )
                }
            }
        }

        AnimatedVisibility(
            visible = isCompleting,
            modifier = Modifier.align(Alignment.Center),
            enter = fadeIn(tween(180)) + scaleIn(initialScale = 0.96f, animationSpec = tween(220)),
            exit = fadeOut(tween(120)) + scaleOut(targetScale = 1.02f, animationSpec = tween(120))
        ) {
            Surface(
                shape = RoundedCornerShape(28.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.96f),
                tonalElevation = 12.dp
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 22.dp, vertical = 18.dp),
                    horizontalArrangement = Arrangement.spacedBy(14.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    CircularProgressIndicator(
                        modifier = Modifier.size(22.dp),
                        strokeWidth = 2.5.dp
                    )
                    Text(
                        text = stringResource(R.string.onboarding_finishing),
                        style = MaterialTheme.typography.titleMedium,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                }
            }
        }
    }
}

@Composable
private fun OnboardingHero(
    page: Int,
    modifier: Modifier = Modifier,
    showSubtitle: Boolean = true
) {
    val (titleRes, subtitleRes, icon) = when (page) {
        0 -> Triple(
            R.string.onboarding_page_1_title,
            R.string.onboarding_page_1_subtitle,
            Icons.Rounded.Gamepad
        )
        1 -> Triple(
            R.string.onboarding_page_2_title,
            R.string.onboarding_page_2_subtitle,
            Icons.Rounded.SmartDisplay
        )
        2 -> Triple(
            R.string.onboarding_page_3_title,
            R.string.onboarding_page_3_subtitle,
            Icons.AutoMirrored.Rounded.LibraryBooks
        )
        else -> Triple(
            R.string.onboarding_page_4_title,
            R.string.onboarding_page_4_subtitle,
            Icons.Rounded.CheckCircle
        )
    }
    
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = modifier.widthIn(max = 480.dp)
    ) {
        Box(
            modifier = Modifier
                .size(112.dp)
                .clip(RoundedCornerShape(32.dp))
                .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = icon,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(56.dp)
            )
        }
        Spacer(modifier = Modifier.height(36.dp))
        Text(
            text = stringResource(titleRes),
            style = MaterialTheme.typography.displaySmall.copy(
                fontWeight = FontWeight.Bold,
                letterSpacing = (-0.5).sp
            ),
            color = MaterialTheme.colorScheme.onBackground,
            textAlign = TextAlign.Center
        )
        if (showSubtitle) {
            Spacer(modifier = Modifier.height(16.dp))
            Text(
                text = stringResource(subtitleRes),
                style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 24.sp),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = 8.dp)
            )
        }
    }
}

@Composable
private fun OnboardingHeroSetup(
    modifier: Modifier = Modifier,
    showSubtitle: Boolean = true
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = modifier.widthIn(max = 480.dp)
    ) {
        Text(
            text = stringResource(R.string.onboarding_title),
            style = MaterialTheme.typography.displaySmall.copy(
                fontWeight = FontWeight.Bold,
                letterSpacing = (-0.5).sp
            ),
            color = MaterialTheme.colorScheme.onBackground,
            textAlign = TextAlign.Center
        )
        if (showSubtitle) {
            Spacer(modifier = Modifier.height(12.dp))
            Text(
                text = stringResource(R.string.onboarding_subtitle),
                style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 24.sp),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = 8.dp)
            )
        }
    }
}

@Composable
private fun OnboardingHeroProfile(
    modifier: Modifier = Modifier,
    showSubtitle: Boolean = true
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = modifier.widthIn(max = 480.dp)
    ) {
        Text(
            text = stringResource(R.string.onboarding_profile_title),
            style = MaterialTheme.typography.displaySmall.copy(
                fontWeight = FontWeight.Bold,
                letterSpacing = (-0.5).sp
            ),
            color = MaterialTheme.colorScheme.onBackground,
            textAlign = TextAlign.Center
        )
        if (showSubtitle) {
            Spacer(modifier = Modifier.height(12.dp))
            Text(
                text = stringResource(R.string.onboarding_profile_subtitle),
                style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 24.sp),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = 8.dp)
            )
        }
    }
}

@Composable
private fun OnboardingHeroPro(
    modifier: Modifier = Modifier,
    showSubtitle: Boolean = true
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = modifier.widthIn(max = 480.dp)
    ) {
        Box(
            modifier = Modifier
                .size(112.dp)
                .clip(RoundedCornerShape(32.dp))
                .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.14f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Rounded.Star,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary,
                modifier = Modifier.size(56.dp)
            )
        }
        Spacer(modifier = Modifier.height(28.dp))
        Text(
            text = stringResource(R.string.onboarding_pro_title),
            style = MaterialTheme.typography.displaySmall.copy(fontWeight = FontWeight.Bold, letterSpacing = 0.sp),
            color = MaterialTheme.colorScheme.onBackground,
            textAlign = TextAlign.Center
        )
        if (showSubtitle) {
            Spacer(modifier = Modifier.height(12.dp))
            Text(
                text = stringResource(R.string.onboarding_pro_subtitle),
                style = MaterialTheme.typography.bodyLarge.copy(lineHeight = 24.sp),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
                modifier = Modifier.padding(horizontal = 8.dp)
            )
        }
    }
}

@Composable
private fun OnboardingProContent(
    isProUnlocked: Boolean,
    proPrice: String?,
    isProductLoading: Boolean,
    isPurchaseInProgress: Boolean,
    onPurchase: () -> Unit,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .widthIn(max = 520.dp),
        shape = RoundedCornerShape(24.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.94f),
        tonalElevation = 4.dp
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Text(
                text = stringResource(R.string.pro_theme_name),
                style = MaterialTheme.typography.titleLarge.copy(fontWeight = FontWeight.Bold),
                color = MaterialTheme.colorScheme.onSurface
            )
            Text(
                text = stringResource(R.string.pro_feature_theme),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = stringResource(R.string.pro_feature_support),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            Text(
                text = when {
                    isProUnlocked -> stringResource(R.string.pro_status_active)
                    proPrice != null -> proPrice
                    isProductLoading -> stringResource(R.string.pro_price_loading)
                    else -> stringResource(R.string.pro_price_unavailable)
                },
                style = MaterialTheme.typography.titleMedium,
                color = MaterialTheme.colorScheme.primary
            )
            if (!isProUnlocked) {
                Button(
                    onClick = onPurchase,
                    enabled = !isPurchaseInProgress && !isProductLoading,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        text = if (isPurchaseInProgress) {
                            stringResource(R.string.pro_purchase_busy)
                        } else {
                            stringResource(R.string.settings_pro_buy)
                        }
                    )
                }
            }
        }
    }
}
@Composable
private fun OnboardingPageIndicator(
    currentPage: Int,
    totalPages: Int,
    modifier: Modifier = Modifier
) {
    Row(
        modifier = modifier,
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically
    ) {
        for (page in 0 until totalPages) {
            val isSelected = page == currentPage
            val width by animateFloatAsState(
                targetValue = if (isSelected) 22f else 8f,
                animationSpec = tween(300),
                label = "indicator-width"
            )
            
            Box(
                modifier = Modifier
                    .padding(horizontal = 4.dp)
                    .height(8.dp)
                    .width(width.dp)
                    .clip(CircleShape)
                    .background(
                        if (isSelected) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.25f)
                        }
                    )
            )
        }
    }
}

@Composable
private fun OnboardingNavigation(
    currentPage: Int,
    totalPages: Int,
    canContinue: Boolean,
    onNext: () -> Unit,
    onPrevious: () -> Unit,
    onContinue: () -> Unit,
    modifier: Modifier = Modifier
) {
    val tvUiEnabled = LocalTvUiEnvironment.current.enabled
    val primaryActionFocusRequester = remember { FocusRequester() }
    LaunchedEffect(tvUiEnabled, currentPage, totalPages, canContinue) {
        val primaryActionEnabled = currentPage < totalPages - 1 || canContinue
        if (tvUiEnabled && primaryActionEnabled) {
            delay(80.milliseconds)
            runCatching { primaryActionFocusRequester.requestFocus() }
        }
    }
    Row(
        modifier = modifier
            .fillMaxWidth()
            .padding(horizontal = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            if (currentPage > 0) {
                OutlinedButton(
                    onClick = onPrevious,
                    modifier = Modifier.height(44.dp),
                    shape = RoundedCornerShape(12.dp),
                    border = androidx.compose.foundation.BorderStroke(
                        1.dp, 
                        MaterialTheme.colorScheme.outline.copy(alpha = 0.2f)
                    ),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 16.dp)
                ) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Rounded.ArrowBack,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Text(
                        text = stringResource(R.string.onboarding_back),
                        style = MaterialTheme.typography.labelLarge.copy(
                            fontWeight = FontWeight.Medium,
                            letterSpacing = 0.sp
                        )
                    )
                }
            }
        }
        
        Spacer(modifier = Modifier.width(12.dp))

        Box(modifier = Modifier.weight(1f), contentAlignment = Alignment.CenterEnd) {
            if (currentPage < totalPages - 1) {
                Button(
                    onClick = onNext,
                    modifier = Modifier
                        .height(52.dp)
                        .focusRequester(primaryActionFocusRequester),
                    shape = RoundedCornerShape(12.dp),
                    elevation = androidx.compose.material3.ButtonDefaults.buttonElevation(
                        defaultElevation = 0.dp
                    ),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 16.dp)
                ) {
                    Text(
                        text = stringResource(R.string.onboarding_next),
                        style = MaterialTheme.typography.labelLarge.copy(
                            fontWeight = FontWeight.Medium,
                            letterSpacing = 0.sp
                        )
                    )
                    Spacer(modifier = Modifier.width(6.dp))
                    Icon(
                        imageVector = Icons.AutoMirrored.Rounded.ArrowForward,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp)
                    )
                }
            } else {
                Button(
                    onClick = onContinue,
                    enabled = canContinue,
                    modifier = Modifier
                        .height(52.dp)
                        .focusRequester(primaryActionFocusRequester),
                    shape = RoundedCornerShape(12.dp),
                    colors = androidx.compose.material3.ButtonDefaults.buttonColors(
                        containerColor = MaterialTheme.colorScheme.primary,
                        contentColor = MaterialTheme.colorScheme.onPrimary
                    ),
                    contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 20.dp)
                ) {
                    Text(
                        text = stringResource(R.string.onboarding_get_started),
                        style = MaterialTheme.typography.labelLarge.copy(
                            fontWeight = FontWeight.Bold,
                            letterSpacing = 0.sp
                        )
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Icon(
                        imageVector = Icons.AutoMirrored.Rounded.ArrowForward,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp)
                    )
                }
            }
        }
    }
}

@Composable
private fun OnboardingSetupContent(
    biosPath: String?,
    gamePath: String?,
    gamePaths: List<String>,
    emulatorDataPath: String?,
    sdCardDataPath: String?,
    biosValid: Boolean,
    gamePathValid: Boolean,
    launchBiosPicker: () -> Unit,
    launchGamePicker: () -> Unit,
    onRemoveGamePath: (String) -> Unit,
    openEmulatorDataLocationDialog: () -> Unit,
    endInset: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier,
    bottomInset: androidx.compose.ui.unit.Dp = 0.dp,
    reserveScrollHintSpace: Boolean = false,
) {
    val completionProgress = listOf(biosValid, gamePathValid).count { it }
    Row(
        modifier = modifier.padding(end = endInset),
        verticalAlignment = Alignment.Top
    ) {
        if (reserveScrollHintSpace) {
            Spacer(modifier = Modifier.width(36.dp))
        }

        Column(
            modifier = if (reserveScrollHintSpace) Modifier.weight(1f) else Modifier.fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            SetupCard(
                icon = Icons.Rounded.Memory,
                title = stringResource(R.string.onboarding_bios_title),
                description = if (biosPath == null) {
                    stringResource(R.string.onboarding_bios_desc)
                } else {
                    DocumentPathResolver.getFallbackDisplayName(biosPath)
                },
                status = when {
                    biosPath == null -> stringResource(R.string.onboarding_status_required)
                    biosValid -> stringResource(R.string.onboarding_status_ready)
                    else -> stringResource(R.string.onboarding_status_invalid_bios)
                },
                statusColor = when {
                    biosPath == null -> MaterialTheme.colorScheme.tertiary
                    biosValid -> Color(0xFF1B8A5A)
                    else -> MaterialTheme.colorScheme.error
                },
                onClick = launchBiosPicker
            )

            Spacer(modifier = Modifier.height(8.dp))

            SetupCard(
                icon = Icons.Rounded.FolderOpen,
                title = stringResource(R.string.onboarding_games_title),
                description = if (gamePaths.isEmpty()) {
                    stringResource(R.string.onboarding_games_desc)
                } else {
                    stringResource(R.string.onboarding_games_selected_count, gamePaths.size)
                },
                status = when {
                    gamePath == null -> stringResource(R.string.onboarding_status_required)
                    gamePathValid -> stringResource(R.string.onboarding_status_ready)
                    else -> stringResource(R.string.onboarding_status_invalid_folder)
                },
                statusColor = when {
                    gamePath == null -> MaterialTheme.colorScheme.tertiary
                    gamePathValid -> Color(0xFF1B8A5A)
                    else -> MaterialTheme.colorScheme.error
                },
                onClick = launchGamePicker
            )

            gamePaths.forEach { path ->
                Surface(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 3.dp),
                    shape = RoundedCornerShape(18.dp),
                    color = MaterialTheme.colorScheme.surfaceVariant.copy(alpha = 0.72f)
                ) {
                    Row(
                        modifier = Modifier.padding(start = 14.dp, end = 6.dp, top = 6.dp, bottom = 6.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = DocumentPathResolver.getFallbackDisplayName(path),
                            modifier = Modifier.weight(1f),
                            style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                        IconButton(onClick = { onRemoveGamePath(path) }) {
                            Icon(
                                imageVector = Icons.Rounded.DeleteOutline,
                                contentDescription = stringResource(R.string.game_folders_remove)
                            )
                        }
                    }
                }
            }

            Spacer(modifier = Modifier.height(8.dp))

            SetupCard(
                icon = Icons.Rounded.FolderOpen,
                title = stringResource(R.string.emulator_data_location_title),
                description = when {
                    emulatorDataPath.isNullOrBlank() -> stringResource(
                        R.string.emulator_data_location_internal_description
                    )
                    sdCardDataPath != null && emulatorDataPath == sdCardDataPath -> stringResource(
                        R.string.emulator_data_location_sd_card_description
                    )
                    else -> DocumentPathResolver.getFallbackDisplayName(emulatorDataPath)
                },
                status = when {
                    emulatorDataPath.isNullOrBlank() -> stringResource(R.string.emulator_data_location_internal)
                    sdCardDataPath != null && emulatorDataPath == sdCardDataPath -> stringResource(
                        R.string.emulator_data_location_sd_card
                    )
                    else -> stringResource(R.string.onboarding_status_custom_folder)
                },
                statusColor = Color(0xFF1B8A5A),
                onClick = openEmulatorDataLocationDialog
            )

            Spacer(modifier = Modifier.height(10.dp))

            Surface(
                shape = RoundedCornerShape(24.dp),
                color = MaterialTheme.colorScheme.surface.copy(alpha = 0.92f),
                tonalElevation = 4.dp
            ) {
                Column(modifier = Modifier.padding(20.dp)) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(
                            imageVector = Icons.Rounded.CheckCircle,
                            contentDescription = null,
                            tint = MaterialTheme.colorScheme.primary
                        )
                        Spacer(modifier = Modifier.width(10.dp))
                        Text(
                            text = stringResource(R.string.onboarding_hint_title),
                            style = MaterialTheme.typography.titleMedium,
                            color = MaterialTheme.colorScheme.onSurface
                        )
                    }
                    Spacer(modifier = Modifier.height(10.dp))
                    Text(
                        text = stringResource(R.string.onboarding_hint_body, completionProgress, 2),
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Spacer(modifier = Modifier.height(10.dp))
            Spacer(modifier = Modifier.height(bottomInset))
        }
    }
}

@Composable
private fun OnboardingSetupScrollHint(
    visible: Boolean,
    scrollProgress: Float,
    modifier: Modifier = Modifier
) {
    val density = LocalDensity.current
    val scrollTravelPx = with(density) { 112.dp.toPx() }
    val clampedScrollProgress = scrollProgress.coerceIn(0f, 1f)
    val bottomFade = (1f - ((clampedScrollProgress - 0.7f) / 0.3f).coerceIn(0f, 1f))
    val hintMotion = rememberInfiniteTransition(label = "onboarding-setup-scroll-hint")
    val arrowOffsetY by hintMotion.animateFloat(
        initialValue = 0f,
        targetValue = 10f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 680),
            repeatMode = RepeatMode.Reverse
        ),
        label = "onboarding-setup-scroll-hint-offset"
    )
    val arrowAlpha by hintMotion.animateFloat(
        initialValue = 0.54f,
        targetValue = 0.92f,
        animationSpec = infiniteRepeatable(
            animation = tween(durationMillis = 680),
            repeatMode = RepeatMode.Reverse
        ),
        label = "onboarding-setup-scroll-hint-alpha"
    )

    Box(
        modifier = modifier,
        contentAlignment = Alignment.TopCenter
    ) {
        AnimatedVisibility(
            visible = visible,
            enter = fadeIn(tween(180)) + scaleIn(initialScale = 0.9f, animationSpec = tween(180)),
            exit = fadeOut(tween(120)) + scaleOut(targetScale = 0.9f, animationSpec = tween(120))
        ) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                modifier = Modifier.graphicsLayer {
                    translationY = clampedScrollProgress * scrollTravelPx + arrowOffsetY
                    alpha = arrowAlpha * bottomFade
                }
            ) {
                Box(
                    modifier = Modifier
                        .width(3.dp)
                        .height(58.dp)
                        .clip(RoundedCornerShape(2.dp))
                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.5f))
                )
                Icon(
                    imageVector = Icons.Rounded.KeyboardArrowDown,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(30.dp)
                )
            }
        }
    }
}

@Composable
private fun OnboardingPerformanceProfileContent(
    selectedProfile: Int,
    onSelectProfile: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        ProfileCard(
            title = stringResource(R.string.onboarding_profile_safe_title),
            description = stringResource(R.string.onboarding_profile_safe_desc),
            selected = selectedProfile == PerformanceProfiles.SAFE,
            onClick = { onSelectProfile(PerformanceProfiles.SAFE) }
        )
        Spacer(modifier = Modifier.height(12.dp))
        ProfileCard(
            title = stringResource(R.string.onboarding_profile_fast_title),
            description = stringResource(R.string.onboarding_profile_fast_desc),
            selected = selectedProfile == PerformanceProfiles.FAST,
            onClick = { onSelectProfile(PerformanceProfiles.FAST) }
        )
    }
}

@Composable
@Suppress("unused")
private fun ChipsetInfoDialog(
    chipsetInfo: DeviceChipsetInfo,
    onDismiss: () -> Unit
) {
    val familyLabel = when (chipsetInfo.family) {
        DeviceChipsetFamily.Snapdragon -> stringResource(R.string.gpu_chipset_snapdragon_title)
        DeviceChipsetFamily.MediaTek -> stringResource(R.string.gpu_chipset_mediatek_title)
        DeviceChipsetFamily.Exynos -> stringResource(R.string.settings_device_profile_family_exynos)
        DeviceChipsetFamily.Tensor -> stringResource(R.string.settings_device_profile_family_tensor)
        DeviceChipsetFamily.Unknown -> stringResource(R.string.settings_device_profile_family_generic)
    }
    val recommendedProfile = when (chipsetInfo.family) {
        DeviceChipsetFamily.Snapdragon -> "${stringResource(R.string.gpu_chipset_snapdragon_title)} / ${stringResource(R.string.gpu_adreno_title)}"
        DeviceChipsetFamily.MediaTek -> stringResource(
            R.string.onboarding_chipset_dialog_recommend_mediatek,
            stringResource(R.string.gpu_chipset_mediatek_title),
            stringResource(R.string.gpu_mali_title),
            stringResource(R.string.gpu_powervr_title)
        )
        else -> stringResource(R.string.onboarding_chipset_dialog_recommend_manual)
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        icon = {
            Icon(
                imageVector = Icons.Rounded.Info,
                contentDescription = null,
                tint = MaterialTheme.colorScheme.primary
            )
        },
        title = {
            Text(text = stringResource(R.string.onboarding_chipset_dialog_title))
        },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
                ChipsetInfoRow(
                    label = stringResource(R.string.onboarding_chipset_dialog_family),
                    value = familyLabel
                )
                ChipsetInfoRow(
                    label = stringResource(R.string.onboarding_chipset_dialog_recommended),
                    value = recommendedProfile
                )
                ChipsetInfoRow(
                    label = stringResource(R.string.onboarding_chipset_dialog_soc),
                    value = chipsetInfo.socModel.ifBlank { stringResource(R.string.settings_not_set) }
                )
                ChipsetInfoRow(
                    label = stringResource(R.string.onboarding_chipset_dialog_hardware),
                    value = chipsetInfo.hardware.ifBlank { stringResource(R.string.settings_not_set) }
                )
                ChipsetInfoRow(
                    label = stringResource(R.string.onboarding_chipset_dialog_device),
                    value = chipsetInfo.deviceModel.ifBlank { stringResource(R.string.settings_not_set) }
                )
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text(text = stringResource(R.string.onboarding_chipset_dialog_ok))
            }
        },
        shape = RoundedCornerShape(24.dp)
    )
}

@Composable
private fun ChipsetInfoRow(
    label: String,
    value: String
) {
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Text(
            text = label,
            style = MaterialTheme.typography.labelMedium.copy(fontWeight = FontWeight.SemiBold),
            color = MaterialTheme.colorScheme.primary
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurface
        )
    }
}

@Composable
@Suppress("unused")
private fun CompactProfileCard(
    title: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(20.dp),
        color = if (selected) {
            MaterialTheme.colorScheme.primary.copy(alpha = 0.11f)
        } else {
            MaterialTheme.colorScheme.surface.copy(alpha = 0.9f)
        },
        tonalElevation = if (selected) 5.dp else 1.dp,
        border = androidx.compose.foundation.BorderStroke(
            if (selected) 2.dp else 1.dp,
            if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.72f)
            else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.42f)
        ),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 14.dp, vertical = 14.dp),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = if (selected) Icons.Rounded.CheckCircle else Icons.Rounded.RadioButtonUnchecked,
                contentDescription = null,
                tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.size(22.dp)
            )
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleSmall.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
            }
        }
    }
}

@Composable
private fun ProfileCard(
    title: String,
    description: String,
    selected: Boolean,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(32.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.9f),
        tonalElevation = if (selected) 6.dp else 2.dp,
        border = androidx.compose.foundation.BorderStroke(
            1.dp,
            if (selected) MaterialTheme.colorScheme.primary.copy(alpha = 0.6f)
            else MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)
        ),
        onClick = onClick
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 20.dp, vertical = 20.dp),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                imageVector = if (selected) Icons.Rounded.RadioButtonChecked else Icons.Rounded.RadioButtonUnchecked,
                contentDescription = null,
                tint = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant
            )
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = title,
                    style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onSurface
                )
                Spacer(modifier = Modifier.height(4.dp))
                Text(
                    text = description,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
        }
    }
}


@Composable
private fun SetupCard(
    icon: androidx.compose.ui.graphics.vector.ImageVector,
    title: String,
    description: String,
    status: String,
    statusColor: Color,
    onClick: () -> Unit
) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        shape = RoundedCornerShape(32.dp),
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.88f),
        tonalElevation = 2.dp,
        onClick = onClick,
        border = androidx.compose.foundation.BorderStroke(
            1.dp, 
            MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.4f)
        )
    ) {
        Column(modifier = Modifier.padding(horizontal = 20.dp, vertical = 20.dp)) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(16.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(52.dp)
                        .clip(RoundedCornerShape(18.dp))
                        .background(MaterialTheme.colorScheme.primary.copy(alpha = 0.08f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = icon,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                        modifier = Modifier.size(26.dp)
                    )
                }
                
                Column(modifier = Modifier.weight(1f)) {
                    Text(
                        text = title,
                        style = MaterialTheme.typography.titleMedium.copy(fontWeight = FontWeight.Bold),
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Spacer(modifier = Modifier.height(2.dp))
                    Text(
                        text = description,
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }

            Spacer(modifier = Modifier.height(16.dp))

            Surface(
                shape = CircleShape,
                color = statusColor.copy(alpha = 0.12f)
            ) {
                Row(
                    modifier = Modifier.padding(horizontal = 14.dp, vertical = 6.dp),
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Box(
                        modifier = Modifier
                            .size(6.dp)
                            .clip(CircleShape)
                            .background(statusColor)
                    )
                    Text(
                        text = status,
                        style = MaterialTheme.typography.labelLarge.copy(fontWeight = FontWeight.SemiBold),
                        color = statusColor
                    )
                }
            }
        }
    }
}
