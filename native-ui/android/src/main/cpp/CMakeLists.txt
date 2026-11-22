cmake_minimum_required(VERSION 3.22)
project(emucorex_ps2ui CXX)

# Google Play 16 KiB page alignment support.
add_link_options("-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")

option(PS2UI_ENABLE_LTO "Enable ThinLTO for the native PS2 UI renderer" ON)
option(PS2UI_DISABLE_LOGS "Disable all native Android logging in Release builds" OFF)

add_library(emucorex_ps2ui SHARED
    ps2_jni.cpp
    ps2_renderer.cpp
    ps2_bios_assets.cpp
)

target_compile_features(emucorex_ps2ui PRIVATE cxx_std_17)
target_compile_options(emucorex_ps2ui PRIVATE
    -Wall -Wextra -Wno-unused-parameter
    -O3
    -fno-exceptions -fno-rtti
    -ffunction-sections -fdata-sections
)

target_link_options(emucorex_ps2ui PRIVATE "-Wl,--gc-sections")

if(PS2UI_ENABLE_LTO AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(emucorex_ps2ui PRIVATE "-flto=thin")
    target_link_options(emucorex_ps2ui PRIVATE "-flto=thin")
endif()

set_target_properties(emucorex_ps2ui PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

find_library(android-lib android)
find_library(GLESv3-lib GLESv3)
if(PS2UI_DISABLE_LOGS)
    target_compile_definitions(emucorex_ps2ui PRIVATE PS2UI_DISABLE_LOGS=1)
    target_link_libraries(emucorex_ps2ui PRIVATE ${android-lib} ${GLESv3-lib})
else()
    find_library(log-lib log)
    target_link_libraries(emucorex_ps2ui PRIVATE ${android-lib} ${log-lib} ${GLESv3-lib})
endif()
