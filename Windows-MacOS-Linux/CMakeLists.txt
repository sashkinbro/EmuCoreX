cmake_minimum_required(VERSION 3.24)

project(EmuCoreX VERSION 0.3.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(EMUCOREX_WITH_PCSX2 "Build the embedded PCSX2 core adapter" OFF)
option(EMUCOREX_CONSOLE "Keep a console attached for desktop diagnostics" OFF)
option(EMUCOREX_BUILD_TOOLS "Build desktop runtime diagnostic tools" ON)
set(EMUCOREX_PREBUILT_CORE_DIR "" CACHE PATH
    "Directory containing the separately built EmuCoreX core and its runtime dependencies")

find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Gui
    Network
    Qml
    Quick
    QuickControls2
    QuickDialogs2
    Sql
)

if(WIN32)
    get_target_property(EMUCOREX_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    execute_process(
        COMMAND "${EMUCOREX_QMAKE_EXECUTABLE}" -query QT_INSTALL_BINS
        OUTPUT_VARIABLE EMUCOREX_QT_BIN_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND "${EMUCOREX_QMAKE_EXECUTABLE}" -query QT_INSTALL_QML
        OUTPUT_VARIABLE EMUCOREX_QT_QML_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    execute_process(
        COMMAND "${EMUCOREX_QMAKE_EXECUTABLE}" -query QT_INSTALL_PLUGINS
        OUTPUT_VARIABLE EMUCOREX_QT_PLUGINS_DIR
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY
    )
    file(GLOB EMUCOREX_QT_RUNTIME_DLLS CONFIGURE_DEPENDS
        "${EMUCOREX_QT_BIN_DIR}/Qt6*.dll"
    )
endif()

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(EmuCoreXDesktop
    src/main.cpp
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/SingleInstanceGuard.cpp
    src/app/SingleInstanceGuard.h
    src/core/EmulatorController.cpp
    src/core/EmulatorController.h
    src/core/CoreRuntime.cpp
    src/core/CoreRuntime.h
    src/core/GameMetadataProvider.cpp
    src/core/GameMetadataProvider.h
    src/core/NativeRenderItem.cpp
    src/core/NativeRenderItem.h
    src/data/DesktopDataService.cpp
    src/data/DesktopDataService.h
    src/i18n/TranslationManager.cpp
    src/i18n/TranslationManager.h
    src/input/InputBindingService.cpp
    src/input/InputBindingService.h
    src/library/GameLibraryModel.cpp
    src/library/GameLibraryModel.h
    src/library/CoverArtService.cpp
    src/library/CoverArtService.h
    src/library/GameCatalogModel.cpp
    src/library/GameCatalogModel.h
    src/settings/SettingsStore.cpp
    src/settings/SettingsStore.h
    resources.qrc
)

set_target_properties(EmuCoreXDesktop PROPERTIES OUTPUT_NAME "EmuCoreX")

target_link_libraries(EmuCoreXDesktop PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
    Qt6::Sql
)

target_compile_definitions(EmuCoreXDesktop PRIVATE
    EMUCOREX_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${EMUCOREX_WITH_PCSX2}>:EMUCOREX_WITH_PCSX2=1>
)

if(EMUCOREX_BUILD_TOOLS)
    add_executable(EmuCoreXCoreProbe
        tools/CoreProbe.cpp
        src/core/CoreRuntime.cpp
        src/core/CoreRuntime.h
        src/core/GameMetadataProvider.cpp
        src/core/GameMetadataProvider.h
        src/library/GameCatalogModel.cpp
        src/library/GameCatalogModel.h
        resources.qrc
    )
    target_link_libraries(EmuCoreXCoreProbe PRIVATE Qt6::Core Qt6::Sql)
    target_compile_definitions(EmuCoreXCoreProbe PRIVATE
        EMUCOREX_VERSION="${PROJECT_VERSION}"
    )
endif()

if(EMUCOREX_WITH_PCSX2)
    target_include_directories(EmuCoreXDesktop PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/PCSX2"
        "${CMAKE_CURRENT_SOURCE_DIR}/../app/src/main/cpp/PCSX2/pcsx2"
    )
endif()

if(WIN32)
    if(NOT EMUCOREX_CONSOLE)
        set_target_properties(EmuCoreXDesktop PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()

    # Keep development builds directly runnable from Explorer. Qt looks for the
    # Windows platform integration plugin in a `platforms` directory next to
    # the executable; without it the application exits before QML is loaded.
    if(TARGET Qt6::QWindowsIntegrationPlugin)
        add_custom_command(TARGET EmuCoreXDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/platforms"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:Qt6::QWindowsIntegrationPlugin>"
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/platforms/"
            COMMENT "Deploying the Qt Windows platform plugin"
            VERBATIM
        )
    endif()

    add_custom_command(TARGET EmuCoreXDesktop POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/qt.conf"
            "$<TARGET_FILE_DIR:EmuCoreXDesktop>/qt.conf"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${EMUCOREX_QT_QML_DIR}"
            "$<TARGET_FILE_DIR:EmuCoreXDesktop>/qml"
        COMMENT "Deploying the Qt QML runtime"
        VERBATIM
    )

    if(EXISTS "${EMUCOREX_QT_PLUGINS_DIR}/sqldrivers/qsqlite.dll")
        add_custom_command(TARGET EmuCoreXDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/sqldrivers"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EMUCOREX_QT_PLUGINS_DIR}/sqldrivers/qsqlite.dll"
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/sqldrivers/"
            COMMENT "Deploying the Qt SQLite driver"
            VERBATIM
        )
    endif()

    if(EXISTS "${EMUCOREX_QT_BIN_DIR}/libsqlite3-0.dll")
        add_custom_command(TARGET EmuCoreXDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EMUCOREX_QT_BIN_DIR}/libsqlite3-0.dll"
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/"
            COMMENT "Deploying the SQLite runtime"
            VERBATIM
        )
    endif()

    if(EXISTS "${EMUCOREX_QT_PLUGINS_DIR}/tls/qschannelbackend.dll")
        add_custom_command(TARGET EmuCoreXDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/tls"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EMUCOREX_QT_PLUGINS_DIR}/tls/qschannelbackend.dll"
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/tls/"
            COMMENT "Deploying the Qt Schannel TLS backend"
            VERBATIM
        )
    endif()

    if(EXISTS "${EMUCOREX_QT_PLUGINS_DIR}/imageformats/qjpeg.dll")
        add_custom_command(TARGET EmuCoreXDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/imageformats"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EMUCOREX_QT_PLUGINS_DIR}/imageformats/qjpeg.dll"
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/imageformats/"
            COMMENT "Deploying the Qt JPEG image plugin"
            VERBATIM
        )
    endif()

    if(EXISTS "${EMUCOREX_QT_BIN_DIR}/libjpeg-8.dll")
        add_custom_command(TARGET EmuCoreXDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EMUCOREX_QT_BIN_DIR}/libjpeg-8.dll"
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/"
            COMMENT "Deploying the JPEG runtime"
            VERBATIM
        )
    endif()

    foreach(EMUCOREX_QT_RUNTIME_DLL IN LISTS EMUCOREX_QT_RUNTIME_DLLS)
        add_custom_command(TARGET EmuCoreXDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${EMUCOREX_QT_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/"
            VERBATIM
        )
    endforeach()

    # The Windows UI uses the Qt toolchain while the upstream PCSX2 x64 core is
    # built with MSVC. Keep that compiler boundary explicit through the stable
    # C ABI and deploy the independently built core as a runtime module.
    if(NOT EMUCOREX_PREBUILT_CORE_DIR)
        set(_emucorex_default_core_dir
            "${CMAKE_CURRENT_SOURCE_DIR}/build-core-x64-msvc-vcpkg/emucorex-core")
        if(EXISTS "${_emucorex_default_core_dir}/EmuCoreXCore.dll")
            set(EMUCOREX_PREBUILT_CORE_DIR "${_emucorex_default_core_dir}")
        endif()
    endif()

    if(EMUCOREX_PREBUILT_CORE_DIR AND EXISTS "${EMUCOREX_PREBUILT_CORE_DIR}/EmuCoreXCore.dll")
        file(GLOB EMUCOREX_PREBUILT_CORE_DLLS CONFIGURE_DEPENDS
            "${EMUCOREX_PREBUILT_CORE_DIR}/*.dll")
        add_custom_command(TARGET EmuCoreXDesktop POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/cores/x64"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${EMUCOREX_PREBUILT_CORE_DLLS}
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/cores/x64/"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${EMUCOREX_PREBUILT_CORE_DIR}/resources"
                "$<TARGET_FILE_DIR:EmuCoreXDesktop>/cores/x64/resources"
            COMMENT "Deploying the MSVC PCSX2 x64 core module"
            COMMAND_EXPAND_LISTS
            VERBATIM
        )
    endif()
elseif(APPLE)
    set_target_properties(EmuCoreXDesktop PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "EmuCoreX"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.sbro.emucorex"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()

include(GNUInstallDirs)
install(TARGETS EmuCoreXDesktop
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET EmuCoreXDesktop
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
