@file:Suppress("UnstableApiUsage", "DEPRECATION")

import java.util.Properties
import java.security.MessageDigest
import java.util.zip.ZipFile
import org.gradle.api.tasks.Copy
import org.gradle.api.tasks.Exec

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
    alias(libs.plugins.google.services)
    alias(libs.plugins.ksp)
}

val localProperties = Properties().apply {
    val propertiesFile = rootProject.file("local.properties")
    if (propertiesFile.isFile) {
        propertiesFile.inputStream().use(::load)
    }
}

fun localProperty(name: String): String? = localProperties.getProperty(name)?.takeIf { it.isNotBlank() }

fun buildConfigString(value: String): String = "\"" + value
    .replace("\\", "\\\\")
    .replace("\"", "\\\"") + "\""

val feedbackEndpoint = localProperty("emucorex.feedback.endpoint").orEmpty()
val feedbackApiKey = localProperty("emucorex.feedback.apiKey").orEmpty()
val multiplayerSignalingUrl = "https://emucorex-multiplayer.kyivstar19971502.workers.dev"
val multiplayerClientCode = "ecx-mp-v1-7H4K9M2Q"
val discordApplicationId = "1536775623287115786"
val discordSdkDirectory = (
    providers.gradleProperty("emucorex.discord.sdkDir").orNull
        ?: localProperty("emucorex.discord.sdkDir")
        ?: providers.environmentVariable("DISCORD_SDK_DIR").orNull
    )
    ?.let(::file)
    ?.takeIf { sdkDir ->
        sdkDir.resolve("include/discordpp.h").isFile &&
            sdkDir.resolve("arm64-v8a/libdiscord_partner_sdk.so").isFile &&
            sdkDir.resolve("discord_partner_sdk.aar").isFile
    }
val emucorexCmakeVersion = "3.22.1"
val emucorexNdkVersion = "29.0.14206865"

val releaseStoreFilePath = localProperty("emucorex.release.storeFile")
val releaseStorePassword = localProperty("emucorex.release.storePassword")
val releaseKeyAlias = localProperty("emucorex.release.keyAlias")
val releaseKeyPassword = localProperty("emucorex.release.keyPassword")
val releaseSigningConfigured = listOf(
    releaseStoreFilePath,
    releaseStorePassword,
    releaseKeyAlias,
    releaseKeyPassword
).all { it != null }

android {
    namespace = "com.sbro.emucorex"
    compileSdk = 37
    ndkVersion = emucorexNdkVersion

    defaultConfig {
        applicationId = "com.sbro.emucorex"
        minSdk = 29
        targetSdk = 37
        versionCode = 211
        versionName = "0.5.3"

        buildConfigField("String", "FEEDBACK_ENDPOINT", buildConfigString(feedbackEndpoint))
        buildConfigField("String", "FEEDBACK_API_KEY", buildConfigString(feedbackApiKey))
        buildConfigField("String", "MULTIPLAYER_SIGNALING_URL", buildConfigString(multiplayerSignalingUrl))
        buildConfigField("String", "MULTIPLAYER_CLIENT_CODE", buildConfigString(multiplayerClientCode))
        buildConfigField("long", "DISCORD_APPLICATION_ID", "${discordApplicationId}L")
        buildConfigField("boolean", "DISCORD_SDK_AVAILABLE", (discordSdkDirectory != null).toString())
        manifestPlaceholders["discordSdkAvailable"] = (discordSdkDirectory != null).toString()

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                arguments(
                    "-DANDROID=true",
                    "-DANDROID_STL=c++_static",
                    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                    "-DEMUCOREX_ANDROID_HOST_PAGE_SIZE=0x1000",
                    "-DEMUCOREX_GS_FAMILY=MALI",
                    "-DEMUCOREX_NATIVE_LIBRARY_NAME=emucore_mali_4k"
                )
                arguments("-DDISCORD_SDK_DIR=${discordSdkDirectory?.invariantSeparatorsPath.orEmpty()}")
            }
        }
        ndk {
            //noinspection ChromeOsAbiSupport
            abiFilters += "arm64-v8a"
        }
    }

    signingConfigs {
        if (releaseSigningConfigured) {
            create("release") {
                storeFile = rootProject.file(releaseStoreFilePath!!)
                storePassword = releaseStorePassword!!
                keyAlias = releaseKeyAlias!!
                keyPassword = releaseKeyPassword!!
            }
        }
    }

    buildTypes {
        debug {
            externalNativeBuild {
                cmake {
                    // Keep gameplay profiling useful while retaining Debug-only diagnostics.
                    arguments(
                        "-DCMAKE_BUILD_TYPE=Debug",
                        "-DCMAKE_C_FLAGS_DEBUG=-O2 -g1",
                        "-DCMAKE_CXX_FLAGS_DEBUG=-O2 -g1",
                        "-DEMUCOREX_ENABLE_NATIVE_SELF_TESTS=ON",
                        "-DEMUCOREX_ENABLE_EXPERIMENTAL_IR=OFF",
                        "-DEMUCOREX_ENABLE_GS_ORACLE=ON"
                    )
                }
            }
        }
        release {
            externalNativeBuild {
                cmake {
                    arguments("-DCMAKE_BUILD_TYPE=Release", "-DEMUCOREX_ENABLE_NATIVE_SELF_TESTS=OFF",
                        "-DEMUCOREX_ENABLE_GS_ORACLE=OFF", "-DEMUCOREX_ENABLE_EXPERIMENTAL_IR=OFF")
                }
            }
            if (releaseSigningConfigured) {
                signingConfig = signingConfigs.getByName("release")
            }
            ndk {
                // Keep full native symbols in release bundles so Google Play can
                // report source files and line numbers for production NDK crashes.
                debugSymbolLevel = "FULL"
            }
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
                "src/main/cpp/PCSX2/3rdparty/SDL3/android-project/app/proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    externalNativeBuild {
        cmake {
            version = emucorexCmakeVersion
            path = file("src/main/cpp/CMakeLists.txt")
        }
    }
    buildFeatures {
        compose = true
        buildConfig = true
    }
    sourceSets {
        getByName("main") {
            assets.srcDir("src/main/cpp/PCSX2/bin")
            java.srcDir("src/main/cpp/PCSX2/3rdparty/SDL3/android-project/app/src/main/java")
            discordSdkDirectory?.let(jniLibs::srcDir)
        }
        getByName("debug") {
            listOf("emucore_adreno_4k", "emucore_mali_16k", "emucore_adreno_16k").forEach { core ->
                jniLibs.srcDir(layout.buildDirectory.dir("generated/native-variants/debug/$core/jniLibs").get().asFile)
            }
        }
        getByName("release") {
            listOf("emucore_adreno_4k", "emucore_mali_16k", "emucore_adreno_16k").forEach { core ->
                jniLibs.srcDir(layout.buildDirectory.dir("generated/native-variants/release/$core/jniLibs").get().asFile)
            }
        }
    }
    packaging {
        jniLibs {
            useLegacyPackaging = true
            pickFirsts += "**/libdiscord_partner_sdk.so"
        }
    }
    bundle {
        language {
            enableSplit = false
        }
    }
    lint {
        lintConfig = file("lint.xml")
    }
}

val bundledPatchesArchive = layout.projectDirectory.file("src/main/cpp/PCSX2/bin/resources/patches.zip")
val bundledPatchesSha256 = "aa14a46908c1114e0c0f1c06accbfa7efc83c1768fb501699e043cbdb1af797f"

val verifyBundledPatches by tasks.registering {
    group = "verification"
    description = "Verifies the official PCSX2 widescreen/no-interlacing patch archive bundled in the APK."
    inputs.file(bundledPatchesArchive)
    doLast {
        val archive = bundledPatchesArchive.asFile
        check(archive.isFile) {
            "Missing ${archive.path}. Download patches.zip from the official PCSX2/pcsx2_patches latest release."
        }
        val actualSha256 = MessageDigest.getInstance("SHA-256")
            .digest(archive.readBytes())
            .joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }
        check(actualSha256 == bundledPatchesSha256) {
            "Unexpected patches.zip digest: $actualSha256. Verify the official release and update the pinned digest."
        }
        ZipFile(archive).use { zip ->
            val patchEntries = zip.entries().asSequence()
                .count { entry -> !entry.isDirectory && entry.name.endsWith(".pnach", ignoreCase = true) }
            check(patchEntries >= 1_000) {
                "patches.zip is valid ZIP data but contains only $patchEntries PNACH files."
            }
        }
    }
}

tasks.matching { it.name == "preBuild" }.configureEach {
    dependsOn(verifyBundledPatches)
}

// PCSX2 bakes page size and renderer-family policy into the native core. AGP
// builds the Debug/Release Mali 4 KiB core; isolated variant-specific CMake
// trees build the other renderer/page-size combinations for APK/AAB packaging.
val androidSdkPath = localProperty("sdk.dir")
    ?: providers.environmentVariable("ANDROID_SDK_ROOT").orNull
    ?: providers.environmentVariable("ANDROID_HOME").orNull
    ?: error("Android SDK path is missing. Set sdk.dir in local.properties.")
val androidSdkDirectory = file(androidSdkPath)
val androidNdkDirectory = androidSdkDirectory.resolve("ndk/$emucorexNdkVersion")
val hostExecutableSuffix = if (
    System.getProperty("os.name").startsWith("Windows", ignoreCase = true)
) ".exe" else ""
val cmakeExecutable = androidSdkDirectory.resolve(
    "cmake/$emucorexCmakeVersion/bin/cmake$hostExecutableSuffix"
)
val ninjaExecutable = androidSdkDirectory.resolve(
    "cmake/$emucorexCmakeVersion/bin/ninja$hostExecutableSuffix"
)
data class NativeCoreVariantTasks(
    val build: org.gradle.api.tasks.TaskProvider<Exec>,
    val stage: org.gradle.api.tasks.TaskProvider<Copy>
)

fun registerNativeCoreVariant(
    family: String,
    pageSize: String,
    buildType: String,
    selfTests: Boolean
): NativeCoreVariantTasks {
    val familyKey = family.lowercase()
    val pageKey = if (pageSize == "0x1000") "4k" else "16k"
    val libraryName = "emucore_${familyKey}_$pageKey"
    val suffix = "${family.lowercase().replaceFirstChar { it.titlecase() }}${pageKey.replaceFirstChar { it.titlecase() }}${buildType.replaceFirstChar { it.titlecase() }}"
    val variantBuildDirectory = layout.buildDirectory.dir("native-variants/$buildType/$libraryName/arm64-v8a")
    val variantObjectDirectory = layout.buildDirectory.dir("native-variants/$buildType/$libraryName/obj/arm64-v8a")
    val variantCore = variantObjectDirectory.map { it.file("lib$libraryName.so") }
    val stagedJniDirectory = layout.buildDirectory.dir("generated/native-variants/$buildType/$libraryName/jniLibs/arm64-v8a")

    val configure = tasks.register("configureEmucore$suffix", Exec::class) {
        group = "build"
        description = "Configures $libraryName for $buildType."
        inputs.files(fileTree("src/main/cpp") {
            include("**/CMakeLists.txt", "**/*.cmake", "**/*.cpp", "**/*.cc", "**/*.cxx", "**/*.h", "**/*.hpp",
                "**/*.inl", "**/*.inc", "**/*.c", "**/*.S", "**/*.s")
        })
        inputs.property("cmakeVersion", emucorexCmakeVersion)
        inputs.property("ndkVersion", emucorexNdkVersion)
        inputs.property("androidSdkPath", androidSdkDirectory.absolutePath)
        inputs.property("family", family)
        inputs.property("pageSize", pageSize)
        inputs.property("buildType", buildType)
        inputs.property("selfTests", selfTests)
        outputs.file(variantBuildDirectory.map { it.file("CMakeCache.txt") })

        doFirst {
            check(cmakeExecutable.isFile) { "CMake was not found: $cmakeExecutable" }
            check(ninjaExecutable.isFile) { "Ninja was not found: $ninjaExecutable" }
            check(androidNdkDirectory.isDirectory) { "Android NDK was not found: $androidNdkDirectory" }
        }

        commandLine(
            cmakeExecutable.absolutePath,
            "-S", file("src/main/cpp").absolutePath,
            "-B", variantBuildDirectory.get().asFile.absolutePath,
            "-G", "Ninja",
            "-DCMAKE_SYSTEM_NAME=Android",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            "-DCMAKE_SYSTEM_VERSION=29",
            "-DANDROID_PLATFORM=android-29",
            "-DANDROID_ABI=arm64-v8a",
            "-DCMAKE_ANDROID_ARCH_ABI=arm64-v8a",
            "-DANDROID_NDK=${androidNdkDirectory.absolutePath}",
            "-DCMAKE_ANDROID_NDK=${androidNdkDirectory.absolutePath}",
            "-DCMAKE_TOOLCHAIN_FILE=${androidNdkDirectory.resolve("build/cmake/android.toolchain.cmake").absolutePath}",
            "-DCMAKE_MAKE_PROGRAM=${ninjaExecutable.absolutePath}",
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${variantObjectDirectory.get().asFile.absolutePath}",
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${variantObjectDirectory.get().asFile.absolutePath}",
            "-DANDROID=true",
            "-DCMAKE_BUILD_TYPE=$buildType",
            "-DCMAKE_C_FLAGS_DEBUG=-O2 -g1",
            "-DCMAKE_CXX_FLAGS_DEBUG=-O2 -g1",
            "-DANDROID_STL=c++_static",
            "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
            "-DEMUCOREX_ANDROID_HOST_PAGE_SIZE=$pageSize",
            "-DEMUCOREX_GS_FAMILY=$family",
            "-DEMUCOREX_NATIVE_LIBRARY_NAME=$libraryName",
            "-DEMUCOREX_ENABLE_NATIVE_SELF_TESTS=${if (selfTests) "ON" else "OFF"}",
            "-DEMUCOREX_ENABLE_GS_ORACLE=${if (selfTests) "ON" else "OFF"}",
            "-DEMUCOREX_ENABLE_EXPERIMENTAL_IR=OFF",
            "-DDISCORD_SDK_DIR=${discordSdkDirectory?.invariantSeparatorsPath.orEmpty()}"
        )
    }

    val build = tasks.register("buildEmucore$suffix", Exec::class) {
        group = "build"
        description = "Builds $libraryName for $buildType."
        dependsOn(configure)
        inputs.files(fileTree("src/main/cpp") {
            include("**/CMakeLists.txt", "**/*.cmake", "**/*.cpp", "**/*.cc", "**/*.cxx", "**/*.h", "**/*.hpp",
                "**/*.inl", "**/*.inc", "**/*.c", "**/*.S", "**/*.s")
        })
        inputs.property("family", family)
        inputs.property("pageSize", pageSize)
        inputs.property("buildType", buildType)
        inputs.property("selfTests", selfTests)
        outputs.file(variantCore)
        // Keep invoking Ninja so generated shader inputs outside the tracked C++ file set
        // (GLSL/HLSL and bake-tool resources) still participate in Ninja's own dependency checks.
        outputs.upToDateWhen { false }
        commandLine(cmakeExecutable.absolutePath, "--build", variantBuildDirectory.get().asFile.absolutePath,
            "--target", "emucore")
        doLast {
            check(variantCore.get().asFile.isFile) { "Native core was not produced: ${variantCore.get().asFile}" }
        }
    }

    val stage = tasks.register("stageEmucore$suffix", Copy::class) {
        group = "build"
        description = "Stages $libraryName into the $buildType APK/AAB JNI inputs."
        dependsOn(build)
        from(variantCore)
        into(stagedJniDirectory)
    }
    return NativeCoreVariantTasks(build, stage)
}

val debugAdreno4k = registerNativeCoreVariant("ADRENO", "0x1000", "Debug", selfTests = true)
val debugMali16k = registerNativeCoreVariant("MALI", "0x4000", "Debug", selfTests = true)
val debugAdreno16k = registerNativeCoreVariant("ADRENO", "0x4000", "Debug", selfTests = true)
val releaseAdreno4k = registerNativeCoreVariant("ADRENO", "0x1000", "Release", selfTests = false)
val releaseMali16k = registerNativeCoreVariant("MALI", "0x4000", "Release", selfTests = false)
val releaseAdreno16k = registerNativeCoreVariant("ADRENO", "0x4000", "Release", selfTests = false)

tasks.matching { it.name == "mergeDebugJniLibFolders" }.configureEach {
    dependsOn(debugAdreno4k.stage, debugMali16k.stage, debugAdreno16k.stage)
}
tasks.matching { it.name == "mergeReleaseJniLibFolders" }.configureEach {
    dependsOn(releaseAdreno4k.stage, releaseMali16k.stage, releaseAdreno16k.stage)
}

dependencies {
    implementation(libs.androidx.credentials)
    implementation(libs.androidx.credentials.play.services)
    implementation(libs.google.identity)
    implementation(libs.google.auth)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.core.splashscreen)
    implementation(libs.androidx.lifecycle.runtime.ktx)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(libs.androidx.activity.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.graphics)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.extended)
    implementation(libs.androidx.navigation.compose)
    implementation(libs.androidx.datastore.preferences)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.androidx.documentfile)
    implementation(libs.androidx.fragment)
    implementation(libs.google.play.billing)
    implementation(libs.google.play.review)
    implementation(libs.google.play.review.ktx)
    implementation(libs.androidx.work.runtime)
    implementation(libs.androidx.room.runtime)
    implementation(libs.androidx.room.ktx)
    implementation(libs.coil.compose)
    implementation(libs.coil.network.okhttp)
    implementation(libs.androidx.browser)
    implementation(libs.webrtc.android)
    discordSdkDirectory?.let { sdkDir ->
        implementation(files(sdkDir.resolve("discord_partner_sdk.aar")))
    }
    ksp(libs.androidx.room.compiler)
    implementation(libs.android.youtube.player.core)
    implementation(platform(libs.firebase.bom))
    implementation(libs.firebase.analytics)
    implementation(libs.firebase.firestore)
    implementation(libs.firebase.auth)
    testImplementation(libs.junit)
    testImplementation("org.robolectric:robolectric:4.17")
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    debugImplementation(libs.androidx.compose.ui.tooling)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
}

ksp {
    arg("room.schemaLocation", "$projectDir/schemas")
}
