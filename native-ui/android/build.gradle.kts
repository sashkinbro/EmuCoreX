plugins {
    alias(libs.plugins.android.library)
}

android {
    namespace = "com.sbro.emucorex.ps2ui"
    compileSdk {
        version = release(37) {
            minorApiLevel = 2
        }
    }
    ndkVersion = "29.0.14206865"

    defaultConfig {
        minSdk = 29
        consumerProguardFiles("consumer-rules.pro")
        ndk {
            abiFilters += "arm64-v8a"
        }
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += listOf("-DANDROID_STL=c++_static")
            }
        }
    }

    buildFeatures {
        buildConfig = true
    }

    buildTypes {
        debug {
            externalNativeBuild {
                cmake {
                    cppFlags += "-O3"
                    arguments += listOf(
                        "-DPS2UI_ENABLE_LTO=ON",
                        "-DPS2UI_DISABLE_LOGS=OFF",
                    )
                }
            }
        }
        release {
            externalNativeBuild {
                cmake {
                    arguments += listOf(
                        "-DPS2UI_ENABLE_LTO=ON",
                        "-DPS2UI_DISABLE_LOGS=ON",
                        "-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O3 -DNDEBUG -DPS2UI_DISABLE_LOGS=1",
                        "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -DPS2UI_DISABLE_LOGS=1",
                    )
                }
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions {
        jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17)
    }
}
