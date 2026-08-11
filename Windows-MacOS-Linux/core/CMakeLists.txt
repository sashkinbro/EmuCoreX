add_library(EmuCoreXCore SHARED
    src/CoreAdapter.cpp
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

add_custom_command(TARGET EmuCoreXCore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/bin/resources"
        "$<TARGET_FILE_DIR:EmuCoreXCore>/resources"
    COMMENT "Deploying the PCSX2 game database and core resources"
    VERBATIM
)
