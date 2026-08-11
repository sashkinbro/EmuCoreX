if(NOT CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    return()
endif()

if(NOT DEFINED EMUCOREX_DESKTOP_ROOT)
    message(FATAL_ERROR "EMUCOREX_DESKTOP_ROOT must point to Windows-MacOS-Linux")
endif()

# Bridge PCSX2's legacy find modules to the multi-configuration targets
# exported by vcpkg without changing the pinned upstream checkout.
if(MSVC)
    set(CMAKE_FIND_PACKAGE_PREFER_CONFIG OFF)
    find_package(lz4 CONFIG REQUIRED)
    if(NOT TARGET LZ4::LZ4)
        add_library(LZ4::LZ4 ALIAS lz4::lz4)
    endif()

    find_package(unofficial-shaderc CONFIG REQUIRED)
    get_target_property(_emucorex_shaderc_location unofficial::shaderc::shaderc IMPORTED_LOCATION_RELEASE)
    get_target_property(_emucorex_shaderc_includes unofficial::shaderc::shaderc INTERFACE_INCLUDE_DIRECTORIES)
    set(SHADERC_LIBRARY "${_emucorex_shaderc_location}" CACHE FILEPATH "" FORCE)
    set(SHADERC_INCLUDE_DIR "${_emucorex_shaderc_includes}" CACHE PATH "" FORCE)
endif()

# PCSX2 remains the top-level project. CMake resolves target names during the
# generate phase, so the adapter can be declared before the PCSX2 target itself.
add_subdirectory(
    "${EMUCOREX_DESKTOP_ROOT}/core"
    "${CMAKE_BINARY_DIR}/emucorex-core"
)

function(emucorex_patch_msvc_shaderc_target)
    if(MSVC AND TARGET Shaderc::shaderc_shared AND TARGET unofficial::shaderc::shaderc)
        set_property(TARGET Shaderc::shaderc_shared PROPERTY INTERFACE_COMPILE_DEFINITIONS "")
        set_property(TARGET Shaderc::shaderc_shared PROPERTY
            INTERFACE_LINK_LIBRARIES unofficial::shaderc::shaderc)
    endif()
endfunction()

cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL emucorex_patch_msvc_shaderc_target)
