package com.sbro.emucorex.ui.onboarding

import android.annotation.SuppressLint
import android.net.Uri
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
import androidx.compose.foundation.layout.navigationBarsPadding
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
import androidx.compose.material.icons.rounded.FolderOpen
import androidx.compose.material.icons.rounded.Gamepad
import androidx.compose.material.icons.rounded.Memory
import androidx.compose.material.icons.rounded.SmartDisplay
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import com.sbro.emucorex.R
import com.sbro.emucorex.core.DocumentPathResolver
import com.sbro.emucorex.ui.common.rememberDebouncedClick
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

@OptIn(ExperimentalFoundationApi::class, ExperimentalLayoutApi::class)
@SuppressLint("ConfigurationScreenWidthHeight")
@Composable
fun OnboardingScreen(
    onComplete: () -> Unit,
    viewModel: OnboardingViewModel = viewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val lifecycleOwner = LocalLifecycleOwner.current
    val configuration = LocalConfiguration.current

    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp && configuration.screenWidthDp >= 600
    var isCompleting by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    
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
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        uri?.let(viewModel::setBiosPath)
    }

    val gamePicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocumentTree()
    ) { uri: Uri? ->
        uri?.let(viewModel::setGamePath)
    }
    val launchBiosPicker = rememberDebouncedClick(onClick = { biosPicker.launch(null) })
    val launchGamePicker = rememberDebouncedClick(onClick = { gamePicker.launch(null) })

    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_RESUME) Unit
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
        }
    }
    
    val continueClick = rememberDebouncedClick(
        onClick = {
            if (isCompleting || !uiState.canContinue) return@rememberDebouncedClick
            isCompleting = true
            scope.launch {
                delay(280)
                viewModel.completeOnboarding(onComplete)
            }
        }
    )
    
    val nextClick = rememberDebouncedClick {
        viewModel.goToNextPage()
    }
    
    val previousClick = rememberDebouncedClick {
        viewModel.goToPreviousPage()
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
            Row(
                modifier = Modifier
                    .fillMaxSize()
                    .graphicsLayer {
                        alpha = contentAlpha
                        translationY = contentOffset
                    },
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
                    if (uiState.currentPage < 3) {
                        OnboardingHero(
                            page = uiState.currentPage, 
                            showSubtitle = false,
                            modifier = Modifier.padding(bottom = 8.dp)
                        )
                    } else {
                        OnboardingHeroSetup(
                            showSubtitle = false,
                            modifier = Modifier.padding(bottom = 8.dp)
                        )
                    }
                }

                Column(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxSize()
                ) {
                    Box(
                        modifier = Modifier
                            .weight(1f)
                            .fillMaxWidth(),
                        contentAlignment = Alignment.Center
                    ) {
                        if (uiState.currentPage < 3) {
                            val subtitleRes = when (uiState.currentPage) {
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
                        } else {
                            Column(
                                modifier = Modifier
                                    .fillMaxSize()
                                    .verticalScroll(rememberScrollState()),
                                horizontalAlignment = Alignment.CenterHorizontally,
                                verticalArrangement = Arrangement.Top
                            ) {
                                Spacer(modifier = Modifier.statusBarsPadding().height(24.dp))
                                
                                OnboardingSetupContent(
                                    biosPath = uiState.biosPath,
                                    gamePath = uiState.gamePath,
                                    biosValid = uiState.biosValid,
                                    gamePathValid = uiState.gamePathValid,
                                    context = LocalContext.current,
                                    launchBiosPicker = launchBiosPicker,
                                    launchGamePicker = launchGamePicker,
                                    endInset = 0.dp,
                                    bottomInset = 0.dp,
                                    modifier = Modifier.padding(horizontal = 32.dp)
                                )
                                
                                Spacer(modifier = Modifier.height(24.dp))
                            }
                        }
                    }

                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        modifier = Modifier
                            .fillMaxWidth()
                            .navigationBarsPadding()
                            .displayCutoutPadding()
                            .padding(bottom = 16.dp, top = 8.dp)
                    ) {
                        OnboardingPageIndicator(
                            currentPage = uiState.currentPage,
                            totalPages = uiState.totalPages,
                            modifier = Modifier.padding(bottom = 20.dp)
                        )
                        OnboardingNavigation(
                            currentPage = uiState.currentPage,
                            totalPages = uiState.totalPages,
                            canContinue = uiState.canContinue,
                            onNext = nextClick,
                            onPrevious = previousClick,
                            onContinue = continueClick,
                            modifier = Modifier.widthIn(max = 420.dp)
                        )
                    }
                }
            }
        } else {
            Box(
                modifier = Modifier
                    .fillMaxSize()
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
                            .navigationBarsPadding()
                            .padding(
                                start = 24.dp,
                                end = 24.dp,
                                top = 48.dp,
                                bottom = 160.dp
                            ),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        when (page) {
                            0, 1, 2 -> OnboardingHero(page = page)
                            3 -> {
                                OnboardingHeroSetup()
                                Spacer(modifier = Modifier.height(32.dp))
                                OnboardingSetupContent(
                                    biosPath = uiState.biosPath,
                                    gamePath = uiState.gamePath,
                                    biosValid = uiState.biosValid,
                                    gamePathValid = uiState.gamePathValid,
                                    context = LocalContext.current,
                                    launchBiosPicker = launchBiosPicker,
                                    launchGamePicker = launchGamePicker,
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
                        .navigationBarsPadding()
                        .padding(bottom = 24.dp)
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
                    modifier = Modifier.height(44.dp),
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
                    modifier = Modifier.height(44.dp),
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
    biosValid: Boolean,
    gamePathValid: Boolean,
    context: android.content.Context,
    launchBiosPicker: () -> Unit,
    launchGamePicker: () -> Unit,
    endInset: androidx.compose.ui.unit.Dp,
    modifier: Modifier = Modifier,
    bottomInset: androidx.compose.ui.unit.Dp = 0.dp
) {
    val completionProgress = listOf(biosValid, gamePathValid).count { it }
    Column(
        modifier = modifier.padding(end = endInset),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        SetupCard(
            icon = Icons.Rounded.Memory,
            title = stringResource(R.string.onboarding_bios_title),
            description = if (biosPath == null) {
                stringResource(R.string.onboarding_bios_desc)
            } else {
                DocumentPathResolver.getDisplayName(context, biosPath)
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
            description = gamePath?.let { DocumentPathResolver.getDisplayName(context, it) }
                ?: stringResource(R.string.onboarding_games_desc),
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
