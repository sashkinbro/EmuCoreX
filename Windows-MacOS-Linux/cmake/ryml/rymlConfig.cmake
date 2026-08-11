if(NOT DEFINED EMUCOREX_RYML_SOURCE)
    if(EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/rapidyaml/CMakeLists.txt")
        set(EMUCOREX_RYML_SOURCE "${CMAKE_SOURCE_DIR}/3rdparty/rapidyaml")
    elseif(EXISTS "${EMUCOREX_DESKTOP_ROOT}/../app/src/main/cpp/PCSX2/3rdparty/rapidyaml/CMakeLists.txt")
        set(EMUCOREX_RYML_SOURCE "${EMUCOREX_DESKTOP_ROOT}/../app/src/main/cpp/PCSX2/3rdparty/rapidyaml")
    else()
        message(FATAL_ERROR "rapidyaml source was not found; set EMUCOREX_RYML_SOURCE")
    endif()
endif()

if(NOT TARGET pcsx2-rapidyaml)
    add_subdirectory(
        "${EMUCOREX_RYML_SOURCE}"
        "${CMAKE_BINARY_DIR}/3rdparty/rapidyaml"
        EXCLUDE_FROM_ALL
    )
endif()

if(NOT TARGET ryml::ryml)
    add_library(ryml::ryml ALIAS pcsx2-rapidyaml)
endif()

set(ryml_FOUND TRUE)
