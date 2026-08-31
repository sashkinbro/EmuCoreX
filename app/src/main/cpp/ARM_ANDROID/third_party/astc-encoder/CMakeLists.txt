cmake_minimum_required(VERSION 3.22.1)
# Pinned Arm encoder sources; CLI and diagnostic tools are not shipped.
file(GLOB ASTC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/astcenc_*.cpp")
add_library(emucore_astc STATIC ${ASTC_SOURCES})
target_include_directories(emucore_astc PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(emucore_astc PUBLIC cxx_std_17)
target_compile_options(emucore_astc PRIVATE -O3 -ffp-contract=off -fno-math-errno)
target_compile_definitions(emucore_astc PRIVATE ASTCENC_NEON=1 ASTCENC_SVE=0 ASTCENC_SSE=0 ASTCENC_AVX=0 ASTCENC_POPCNT=0 ASTCENC_F16C=0)
set_target_properties(emucore_astc PROPERTIES POSITION_INDEPENDENT_CODE ON)
