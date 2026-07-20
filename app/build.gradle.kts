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
        versionCode = 125
        versionName = "0.2.8"

        buildConfigField("String", "FEEDBACK_ENDPOINT", buildConfigString(feedbackEndpoint))
        buildConfigField("String", "FEEDBACK_API_KEY", buildConfigString(feedbackApiKey))

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

        externalNativeBuild {
            cmake {
                arguments(
                    "-DANDROID=true",
                    "-DCMAKE_BUILD_TYPE=Release",
                    "-DANDROID_STL=c++_static",
                    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                    "-DEMUCOREX_ANDROID_HOST_PAGE_SIZE=0x1000",
                    "-DEMUCOREX_NATIVE_LIBRARY_NAME=emucore_4k"
                )
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
                    arguments("-DEMUCOREX_ENABLE_NATIVE_SELF_TESTS=ON")
                }
            }
        }
        release {
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
            // Populated only by tools/build_universal_page_release.ps1.
            jniLibs.srcDir(file("build/generated/page-size-jni-libs"))
        }
    }
    packaging {
        jniLibs {
            useLegacyPackaging = true
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

// PCSX2 bakes the host page size into its fastmem implementation, so Android
// needs two native cores. AGP builds the normal 4 KiB core above; these tasks
// build the 16 KiB core in an isolated CMake tree and expose it as generated
// jniLibs before every APK/AAB packaging task. This keeps Android Studio's
// standard Build/Generate Signed Bundle or APK flow fully universal.
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
val secondary16kBuildDirectory = layout.buildDirectory.dir("native-secondary/16k/arm64-v8a")
val secondary16kObjectDirectory = layout.buildDirectory.dir("native-secondary/16k/obj/arm64-v8a")
val secondary16kCore = secondary16kObjectDirectory.map { it.file("libemucore_16k.so") }
val generated16kJniDirectory = layout.buildDirectory.dir("generated/page-size-jni-libs/arm64-v8a")

val configureEmucore16k by tasks.registering(Exec::class) {
    group = "build"
    description = "Configures the secondary 16 KiB Android emulator core."
    inputs.files(fileTree("src/main/cpp") {
        include("**/CMakeLists.txt", "**/*.cmake")
    })
    inputs.property("cmakeVersion", emucorexCmakeVersion)
    inputs.property("ndkVersion", emucorexNdkVersion)
    inputs.property("androidSdkPath", androidSdkDirectory.absolutePath)
    outputs.file(secondary16kBuildDirectory.map { it.file("CMakeCache.txt") })

    doFirst {
        check(cmakeExecutable.isFile) { "CMake was not found: $cmakeExecutable" }
        check(ninjaExecutable.isFile) { "Ninja was not found: $ninjaExecutable" }
        check(androidNdkDirectory.isDirectory) { "Android NDK was not found: $androidNdkDirectory" }
    }

    commandLine(
        cmakeExecutable.absolutePath,
        "-S", file("src/main/cpp").absolutePath,
        "-B", secondary16kBuildDirectory.get().asFile.absolutePath,
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
        "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${secondary16kObjectDirectory.get().asFile.absolutePath}",
        "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${secondary16kObjectDirectory.get().asFile.absolutePath}",
        "-DANDROID=true",
        "-DCMAKE_BUILD_TYPE=Release",
        "-DANDROID_STL=c++_static",
        "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
        "-DEMUCOREX_ANDROID_HOST_PAGE_SIZE=0x4000",
        "-DEMUCOREX_NATIVE_LIBRARY_NAME=emucore_16k"
    )
}

val buildEmucore16k by tasks.registering(Exec::class) {
    group = "build"
    description = "Builds the secondary 16 KiB Android emulator core."
    dependsOn(configureEmucore16k)
    outputs.file(secondary16kCore)
    outputs.upToDateWhen { false }
    commandLine(
        cmakeExecutable.absolutePath,
        "--build", secondary16kBuildDirectory.get().asFile.absolutePath,
        "--target", "emucore"
    )
    doLast {
        check(secondary16kCore.get().asFile.isFile) {
            "The 16 KiB emulator core was not produced: ${secondary16kCore.get().asFile}"
        }
    }
}

val stageEmucore16k by tasks.registering(Copy::class) {
    group = "build"
    description = "Stages the 16 KiB core for standard APK and AAB packaging."
    dependsOn(buildEmucore16k)
    from(secondary16kCore)
    into(generated16kJniDirectory)
}

tasks.matching { it.name == "mergeReleaseJniLibFolders" }.configureEach {
    dependsOn(stageEmucore16k)
}

dependencies {
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
    implementation(libs.android.youtube.player.core)
    implementation(platform(libs.firebase.bom))
    implementation(libs.firebase.analytics)
    implementation(libs.firebase.firestore)
    implementation(libs.firebase.auth)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
    androidTestImplementation(platform(libs.androidx.compose.bom))
    androidTestImplementation(libs.androidx.compose.ui.test.junit4)
    debugImplementation(libs.androidx.compose.ui.tooling)
    debugImplementation(libs.androidx.compose.ui.test.manifest)
}
