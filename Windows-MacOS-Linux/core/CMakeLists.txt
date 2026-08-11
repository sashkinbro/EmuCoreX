add_library(EmuCoreXCore SHARED
    src/CoreAdapter.cpp
    src/DesktopCoreSession.cpp
    src/DesktopCoreSession.h
    src/DesktopHost.cpp
    include/EmuCoreXCoreApi.h
)

target_compile_features(EmuCoreXCore PRIVATE cxx_std_20)
target_compile_definitions(EmuCoreXCore PRIVATE EMUCOREX_CORE_BUILD=1)
target_include_directories(EmuCoreXCore
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/pcsx2"
        "${CMAKE_SOURCE_DIR}/common/include"
)
target_link_libraries(EmuCoreXCore PRIVATE PCSX2 PCSX2_FLAGS)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64" AND NOT MSVC)
    target_compile_options(EmuCoreXCore PRIVATE -msse4.1)
endif()

set_target_properties(EmuCoreXCore PROPERTIES
    OUTPUT_NAME "EmuCoreXCore"
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

set(EMUCOREX_PATCHES_URL
    "https://github.com/PCSX2/pcsx2_patches/releases/download/latest/patches.zip")
set(EMUCOREX_PATCHES_SHA256
    "7628856F0BC4E615F2372FA545896511F9D2539E5AD5073DC2FB186810D8C253")
set(EMUCOREX_PATCHES_ARCHIVE "${CMAKE_BINARY_DIR}/runtime-resources/patches.zip")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/runtime-resources")
file(DOWNLOAD "${EMUCOREX_PATCHES_URL}" "${EMUCOREX_PATCHES_ARCHIVE}"
    EXPECTED_HASH "SHA256=${EMUCOREX_PATCHES_SHA256}"
    TLS_VERIFY ON
    SHOW_PROGRESS
    STATUS EMUCOREX_PATCHES_DOWNLOAD_STATUS)
list(GET EMUCOREX_PATCHES_DOWNLOAD_STATUS 0 EMUCOREX_PATCHES_DOWNLOAD_CODE)
if(NOT EMUCOREX_PATCHES_DOWNLOAD_CODE EQUAL 0)
    list(GET EMUCOREX_PATCHES_DOWNLOAD_STATUS 1 EMUCOREX_PATCHES_DOWNLOAD_MESSAGE)
    message(FATAL_ERROR "Unable to download the pinned PCSX2 patches archive: ${EMUCOREX_PATCHES_DOWNLOAD_MESSAGE}")
endif()

add_custom_command(TARGET EmuCoreXCore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/bin/resources"
        "$<TARGET_FILE_DIR:EmuCoreXCore>/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${EMUCOREX_PATCHES_ARCHIVE}"
        "$<TARGET_FILE_DIR:EmuCoreXCore>/resources/patches.zip"
    COMMENT "Deploying the PCSX2 game database and core resources"
    VERBATIM
)
