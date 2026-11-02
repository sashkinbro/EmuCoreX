# Vendored DXVK DXBC->SPIR-V compiler.
#
# Upstream shipped this as a git submodule of lsfg-vk-android (PancakeTAS/dxbc), which was
# deleted from GitHub. The sources here are taken from DXVK v2.4.1 (src/dxbc, src/spirv and
# the util/log/com/rc pieces the compiler includes), with DXVK's device-wide shader class
# replaced by the small shim in dxvk/dxvk_shader.h. Only the compiler is built.
cmake_minimum_required(VERSION 3.20)

project(dxbc LANGUAGES CXX)

file(GLOB DXBC_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/dxbc/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/dxbc_android_shims.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/util/util_env.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/util/util_string.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/util/thread.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/util/log/log.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/util/log/log_debug.cpp"
)

add_library(dxbc STATIC ${DXBC_SOURCES})

set_target_properties(dxbc PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)

# Public include roots, because consumers spell the headers the way DXVK does:
# <dxbc_module.h> from dxbc/, <spirv_*.h> from spirv/ and <thirdparty/spirv.hpp> from the root.
target_include_directories(dxbc
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/dxbc"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/spirv"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/util"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

# The compiler needs the Vulkan headers for VkShaderStageFlagBits and friends.
if(TARGET Vulkan::Headers)
    target_link_libraries(dxbc PUBLIC Vulkan::Headers)
elseif(DXBC_VULKAN_INCLUDE_DIR)
    target_include_directories(dxbc SYSTEM PUBLIC "${DXBC_VULKAN_INCLUDE_DIR}")
endif()