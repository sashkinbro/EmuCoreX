cmake_minimum_required(VERSION 3.22.1)

project(EmuCoreXArmAndroid LANGUAGES C CXX)

set(EMUCOREX_CPP_DIR "${CMAKE_CURRENT_LIST_DIR}/.." CACHE PATH "EmuCoreX native source root")
set(PCSX2_UPSTREAM_DIR "${EMUCOREX_CPP_DIR}/PCSX2" CACHE PATH "Clean PCSX2 upstream checkout")
option(EMUCOREX_LINK_UPSTREAM_PCSX2 "Link the upstream PCSX2 core into libemucore.so" ON)
option(EMUCOREX_ENABLE_NATIVE_SELF_TESTS "Enable debug-only native core self-tests" OFF)
set(EMUCOREX_ANDROID_HOST_PAGE_SIZE "0x1000" CACHE STRING "Android runtime page size expected by the PCSX2 core")
set(EMUCOREX_NATIVE_LIBRARY_NAME "emucore_4k" CACHE STRING "Output name of the Android emulator core library")
set(EMUCOREX_PCSX2_THIRD_PARTY_DIR "${PCSX2_UPSTREAM_DIR}/3rdparty" CACHE PATH "PCSX2 third-party dependency root")
set(EMUCOREX_OAKNUT_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/oaknut/include" CACHE PATH "Oaknut include directory")

if(NOT EXISTS "${EMUCOREX_OAKNUT_INCLUDE_DIR}/oaknut/oaknut.hpp")
	message(FATAL_ERROR "Oaknut is required for Phase 2 Android JIT at ${EMUCOREX_OAKNUT_INCLUDE_DIR}")
endif()
set(EMUCOREX_HAS_OAKNUT 1)
set(EMUCOREX_HAS_CAPSTONE 0)

if(NOT EXISTS "${PCSX2_UPSTREAM_DIR}/pcsx2/VMManager.h")
	message(FATAL_ERROR "PCSX2 upstream checkout is missing at ${PCSX2_UPSTREAM_DIR}")
endif()

if(NOT EMUCOREX_ANDROID_HOST_PAGE_SIZE MATCHES "^0x(1000|4000)$")
	message(FATAL_ERROR "EMUCOREX_ANDROID_HOST_PAGE_SIZE must be 0x1000 or 0x4000")
endif()

if(NOT EMUCOREX_NATIVE_LIBRARY_NAME MATCHES "^[A-Za-z0-9_]+$")
	message(FATAL_ERROR "EMUCOREX_NATIVE_LIBRARY_NAME contains unsupported characters")
endif()

# RetroAchievements is compiled from the tracked Android overlay below. These
# guards prevent unreviewed local edits in the vendor checkout from silently
# changing client identity or Hardcore behavior in a release build.
function(emucorex_verify_upstream_file relative_path expected_sha256)
	set(_path "${PCSX2_UPSTREAM_DIR}/${relative_path}")
	if(NOT EXISTS "${_path}")
		message(FATAL_ERROR "Required PCSX2 upstream file is missing: ${_path}")
	endif()
	file(SHA256 "${_path}" _actual_sha256)
	if(NOT _actual_sha256 STREQUAL expected_sha256)
		message(FATAL_ERROR
			"PCSX2 upstream file ${relative_path} does not match v2.7.316. "
			"Rebase ARM_ANDROID/src/integration before changing the pinned source. "
			"Expected ${expected_sha256}, got ${_actual_sha256}.")
	endif()
endfunction()

emucorex_verify_upstream_file("pcsx2/Achievements.cpp" "80f6bbdf45e7604dc30ab02d18c8008a606fa8cb29d2859ad0193633a11f08c2")
emucorex_verify_upstream_file("pcsx2/Achievements.h" "dbdcfe13a8e583ca9f691b4c94d0812a632fa6412bc9d68c0d5cc829b83b5c5c")
emucorex_verify_upstream_file("pcsx2/Host.cpp" "4c6b27e56f8c1887f8c7411348cf667070b8aad9a2266cf8a76246c315e839e4")

if(EMUCOREX_LINK_UPSTREAM_PCSX2)
	set(EMUCOREX_THIRD_PARTY_DIR "${EMUCOREX_PCSX2_THIRD_PARTY_DIR}")
	set(TOP_CMAKE_WAS_SOURCED TRUE)
	set(ARCH_ARM64 TRUE)
	set(LINUX FALSE)
	set(USE_OPENGL ON CACHE BOOL "" FORCE)
	set(USE_VULKAN ON CACHE BOOL "" FORCE)
	set(USE_VTUNE OFF)
	set(USE_BACKTRACE OFF)
	set(LTO_PCSX2_CORE ON)
	set(DISABLE_ADVANCE_SIMD OFF)
	set(PCSX2_DEFS
		__POSIX__
		ENABLE_OPENGL
		ENABLE_VULKAN
		OVERRIDE_HOST_PAGE_SIZE=${EMUCOREX_ANDROID_HOST_PAGE_SIZE}
		OVERRIDE_HOST_CACHE_LINE_SIZE=64
	)
	set(PCSX2_WARNINGS -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -Wno-missing-field-initializers)
	set(FFMPEG_INCLUDE_DIRS "${PCSX2_UPSTREAM_DIR}/3rdparty/ffmpeg/include")
	if(NOT TARGET Threads::Threads)
		add_library(Threads::Threads INTERFACE IMPORTED)
	endif()
	file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/common/include")
	file(WRITE "${CMAKE_BINARY_DIR}/common/include/svnrev.h"
		"#define GIT_HASH \"53e3838c1dd59f611fa3d2fd36915903a1304c6e\"\n"
		"#define GIT_TAG \"v2.7.316\"\n"
		"#define GIT_DATE \"2026-05-06\"\n"
		"#define GIT_REV \"v2.7.316\"\n"
		"#define GIT_TAGGED_COMMIT 1\n"
		"#define GIT_TAG_HI 2\n"
		"#define GIT_TAG_MID 7\n"
		"#define GIT_TAG_LO 316\n"
	)

	function(fixup_file_properties target)
	endfunction()

	function(source_groups_from_vcxproj_filters)
	endfunction()

	function(force_include_last target pattern)
	endfunction()

	function(emucorex_add_interface_target name)
		if(NOT TARGET ${name})
			add_library(${name} INTERFACE IMPORTED)
		endif()
	endfunction()

	function(emucorex_import_static_target name library_name)
		if(NOT TARGET ${name})
			add_library(${name} STATIC IMPORTED GLOBAL)
			set_target_properties(${name} PROPERTIES
				IMPORTED_LOCATION "${EMUCOREX_THIRD_PARTY_DIR}/libs/${library_name}"
				INTERFACE_INCLUDE_DIRECTORIES "${EMUCOREX_THIRD_PARTY_DIR}/include"
			)
		endif()
	endfunction()

	function(emucorex_remove_target_sources target)
		get_target_property(_sources ${target} SOURCES)
		if(NOT _sources)
			return()
		endif()
		foreach(_remove IN LISTS ARGN)
			list(FILTER _sources EXCLUDE REGEX "${_remove}$")
		endforeach()
		set_property(TARGET ${target} PROPERTY SOURCES "${_sources}")
	endfunction()

	add_subdirectory("${EMUCOREX_THIRD_PARTY_DIR}/zlib" "${CMAKE_CURRENT_BINARY_DIR}/upstream/zlib" EXCLUDE_FROM_ALL)

	emucorex_import_static_target(JPEG::JPEG libjpeg.a)
	emucorex_import_static_target(PNG::PNG libpng16.a)
	emucorex_import_static_target(PCAP::PCAP libpcap.a)
	emucorex_import_static_target(OpenSSL::Crypto libcrypto.a)
	emucorex_import_static_target(OpenSSL::SSL libssl.a)
	emucorex_import_static_target(NGHTTP2::nghttp2 libnghttp2.a)
	emucorex_import_static_target(CURL::libcurl libcurl.a)
	target_link_libraries(PNG::PNG INTERFACE ZLIB::ZLIB)
	target_link_libraries(OpenSSL::SSL INTERFACE OpenSSL::Crypto)
	target_link_libraries(CURL::libcurl INTERFACE OpenSSL::SSL OpenSSL::Crypto NGHTTP2::nghttp2 ZLIB::ZLIB PCAP::PCAP)

	set(SDL_SHARED OFF CACHE BOOL "" FORCE)
	set(SDL_STATIC ON CACHE BOOL "" FORCE)
	set(SDL_TESTS OFF CACHE BOOL "" FORCE)
	set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
	set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
	set(HAVE_WL_VERSION_SCRIPT ON CACHE INTERNAL "Android NDK lld supports linker version scripts")
	add_subdirectory("${EMUCOREX_THIRD_PARTY_DIR}/SDL3" "${CMAKE_CURRENT_BINARY_DIR}/upstream/SDL3" EXCLUDE_FROM_ALL)

	set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
	set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
	set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
	set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
	set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
	set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
	set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
	set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
	set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
	add_subdirectory("${EMUCOREX_THIRD_PARTY_DIR}/libwebp" "${CMAKE_CURRENT_BINARY_DIR}/upstream/libwebp" EXCLUDE_FROM_ALL)
	if(TARGET webp AND NOT TARGET WebP::libwebp)
		add_library(WebP::libwebp ALIAS webp)
	endif()

	add_subdirectory("${EMUCOREX_THIRD_PARTY_DIR}/zstd" "${CMAKE_CURRENT_BINARY_DIR}/upstream/zstd" EXCLUDE_FROM_ALL)

	add_subdirectory("${EMUCOREX_THIRD_PARTY_DIR}/lz4" "${CMAKE_CURRENT_BINARY_DIR}/upstream/lz4" EXCLUDE_FROM_ALL)

	add_subdirectory("${EMUCOREX_THIRD_PARTY_DIR}/rapidyaml" "${CMAKE_CURRENT_BINARY_DIR}/upstream/rapidyaml" EXCLUDE_FROM_ALL)
	if(TARGET pcsx2-rapidyaml AND NOT TARGET ryml::ryml)
		add_library(ryml::ryml ALIAS pcsx2-rapidyaml)
	endif()

	set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
	set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
	set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
	set(FT_DISABLE_HVF ON CACHE BOOL "" FORCE)
	set(FT_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
	set(FT_REQUIRE_PNG OFF CACHE BOOL "" FORCE)
	add_subdirectory("${EMUCOREX_THIRD_PARTY_DIR}/harfbuzz" "${CMAKE_CURRENT_BINARY_DIR}/upstream/harfbuzz" EXCLUDE_FROM_ALL)
	add_subdirectory("${EMUCOREX_THIRD_PARTY_DIR}/freetype" "${CMAKE_CURRENT_BINARY_DIR}/upstream/freetype" EXCLUDE_FROM_ALL)
	if(TARGET freetype AND NOT TARGET Freetype::Freetype)
		add_library(Freetype::Freetype ALIAS freetype)
	endif()

	set(PLUTOSVG_ENABLE_FREETYPE OFF CACHE BOOL "" FORCE)
	set(PLUTOSVG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	add_subdirectory("${EMUCOREX_THIRD_PARTY_DIR}/plutosvg1" "${CMAKE_CURRENT_BINARY_DIR}/upstream/plutosvg1" EXCLUDE_FROM_ALL)

	emucorex_add_interface_target(JPEG::JPEG)
	emucorex_add_interface_target(PNG::PNG)
	emucorex_add_interface_target(WebP::libwebp)
	emucorex_add_interface_target(PCAP::PCAP)
	target_include_directories(PCAP::PCAP INTERFACE "${PCSX2_UPSTREAM_DIR}/3rdparty/include")
	emucorex_add_interface_target(X11::X11)
	emucorex_add_interface_target(X11::Xrandr)
	emucorex_add_interface_target(X11::Xi)
	emucorex_add_interface_target(SDL3::SDL3)
	emucorex_add_interface_target(LZ4::LZ4)
	emucorex_add_interface_target(Zstd::Zstd)
	emucorex_add_interface_target(ryml::ryml)

	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/fast_float" "${CMAKE_CURRENT_BINARY_DIR}/upstream/fast_float" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/fmt" "${CMAKE_CURRENT_BINARY_DIR}/upstream/fmt" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/cpuinfo" "${CMAKE_CURRENT_BINARY_DIR}/upstream/cpuinfo" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/lzma" "${CMAKE_CURRENT_BINARY_DIR}/upstream/lzma" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/libchdr" "${CMAKE_CURRENT_BINARY_DIR}/upstream/libchdr" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/libzip" "${CMAKE_CURRENT_BINARY_DIR}/upstream/libzip" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/simpleini" "${CMAKE_CURRENT_BINARY_DIR}/upstream/simpleini" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/soundtouch" "${CMAKE_CURRENT_BINARY_DIR}/upstream/soundtouch" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/imgui" "${CMAKE_CURRENT_BINARY_DIR}/upstream/imgui" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/glad" "${CMAKE_CURRENT_BINARY_DIR}/upstream/glad" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/rcheevos" "${CMAKE_CURRENT_BINARY_DIR}/upstream/rcheevos" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/rapidjson" "${CMAKE_CURRENT_BINARY_DIR}/upstream/rapidjson" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/discord-rpc" "${CMAKE_CURRENT_BINARY_DIR}/upstream/discord-rpc" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/freesurround" "${CMAKE_CURRENT_BINARY_DIR}/upstream/freesurround" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/vulkan" "${CMAKE_CURRENT_BINARY_DIR}/upstream/vulkan" EXCLUDE_FROM_ALL)
	add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/third_party/libadrenotools" "${CMAKE_CURRENT_BINARY_DIR}/third_party/libadrenotools" EXCLUDE_FROM_ALL)
	set_property(TARGET freesurround PROPERTY CXX_STANDARD 20)
	set_property(TARGET freesurround PROPERTY CXX_STANDARD_REQUIRED ON)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/demangler" "${CMAKE_CURRENT_BINARY_DIR}/upstream/demangler" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/ccc" "${CMAKE_CURRENT_BINARY_DIR}/upstream/ccc" EXCLUDE_FROM_ALL)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/3rdparty/cubeb" "${CMAKE_CURRENT_BINARY_DIR}/upstream/cubeb" EXCLUDE_FROM_ALL)

	add_subdirectory("${PCSX2_UPSTREAM_DIR}/common" "${CMAKE_CURRENT_BINARY_DIR}/upstream/common")
	emucorex_remove_target_sources(common "DynamicLibrary.cpp" "Linux/LnxHostSys.cpp" "Linux/LnxMisc.cpp")
	target_sources(common PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/src/platform/android_dynamic_library.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/src/platform/android_hostsys.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/src/platform/android_misc.cpp"
	)
	target_include_directories(common PRIVATE "${CMAKE_CURRENT_LIST_DIR}/include")
	set_property(TARGET common PROPERTY CXX_STANDARD 20)
	set_property(TARGET common PROPERTY CXX_STANDARD_REQUIRED ON)
	add_subdirectory("${PCSX2_UPSTREAM_DIR}/pcsx2" "${CMAKE_CURRENT_BINARY_DIR}/upstream/pcsx2")
	target_link_libraries(PCSX2_FLAGS INTERFACE JPEG::JPEG)
	target_include_directories(PCSX2 PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/include"
		"${EMUCOREX_OAKNUT_INCLUDE_DIR}"
		"${PCSX2_UPSTREAM_DIR}/3rdparty/glad/include"
	)
	target_include_directories(PCSX2 BEFORE PRIVATE
		"${PCSX2_UPSTREAM_DIR}/3rdparty/include"
	)
	emucorex_remove_target_sources(PCSX2
		"Achievements.cpp"
		"Host/SDLAudioStream.cpp"
		"Input/SDLInputSource.cpp"
	)
	target_sources(PCSX2 PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/src/integration/pcsx2_achievements_android.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/src/platform/android_aaudio_stream.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/src/platform/android_network_adapters.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/src/platform/android_posix_compat.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/src/platform/android_pcap_unavailable.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/src/platform/android_sdl_input_source.cpp"
	)
	target_compile_definitions(PCSX2 PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
	target_compile_definitions(PCSX2 PRIVATE EMUCOREX_ANDROID_JIT=1)
	target_link_libraries(PCSX2 PRIVATE adrenotools)
	target_compile_options(PCSX2 PRIVATE
		"SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/include/emucorex/android_posix_compat.h"
	)
	set_property(TARGET PCSX2 PROPERTY CXX_STANDARD 20)
	set_property(TARGET PCSX2 PROPERTY CXX_STANDARD_REQUIRED ON)

	if(LTO_PCSX2_CORE)
		target_include_directories(PCSX2_LTO PRIVATE
			"${CMAKE_CURRENT_LIST_DIR}/include"
			"${EMUCOREX_OAKNUT_INCLUDE_DIR}"
			"${PCSX2_UPSTREAM_DIR}/3rdparty/glad/include"
		)
		target_include_directories(PCSX2_LTO BEFORE PRIVATE
			"${PCSX2_UPSTREAM_DIR}/3rdparty/include"
		)
		target_compile_definitions(PCSX2_LTO PRIVATE VK_USE_PLATFORM_ANDROID_KHR)
		target_compile_definitions(PCSX2_LTO PRIVATE EMUCOREX_ANDROID_JIT=1)
		target_link_libraries(PCSX2_LTO PRIVATE adrenotools)
		target_compile_options(PCSX2_LTO PRIVATE
			"SHELL:-include ${CMAKE_CURRENT_LIST_DIR}/include/emucorex/android_posix_compat.h"
		)
		set_property(TARGET PCSX2_LTO PROPERTY CXX_STANDARD 20)
		set_property(TARGET PCSX2_LTO PROPERTY CXX_STANDARD_REQUIRED ON)
	endif()
endif()

if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/third_party/capstone/CMakeLists.txt")
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
	set(CAPSTONE_ARCHITECTURE_DEFAULT OFF CACHE BOOL "" FORCE)
	set(CAPSTONE_ARM64_SUPPORT ON CACHE BOOL "" FORCE)
	set(CAPSTONE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(CAPSTONE_BUILD_CSTOOL OFF CACHE BOOL "" FORCE)
	set(CAPSTONE_BUILD_CSTEST OFF CACHE BOOL "" FORCE)
	set(CAPSTONE_INSTALL OFF CACHE BOOL "" FORCE)
	add_subdirectory(third_party/capstone EXCLUDE_FROM_ALL)
	set(EMUCOREX_HAS_CAPSTONE 1)
endif()

add_library(emucore SHARED
	src/jni/native_app_jni.cpp
	src/jni/support_jni.cpp
	src/integration/retro_achievements_android.cpp
	src/platform/android_disc_io.cpp
	src/runtime/android_crash_diagnostics.cpp
	src/runtime/android_input_services.cpp
	src/runtime/android_host_services.cpp
	src/runtime/android_runtime.cpp
	src/runtime/upstream_vm_bridge.cpp
)

set_target_properties(emucore PROPERTIES OUTPUT_NAME "${EMUCOREX_NATIVE_LIBRARY_NAME}")
# A 16 KiB ELF alignment is valid on both 4 KiB and 16 KiB Android systems.
# The PCSX2 fastmem page size remains a separate compile-time choice above.
target_link_options(emucore PRIVATE
	"LINKER:-z,max-page-size=16384"
	"LINKER:-z,common-page-size=16384"
)

target_compile_features(emucore PRIVATE cxx_std_20)
target_compile_options(emucore PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(emucore
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/include"
		"${PCSX2_UPSTREAM_DIR}"
		"${PCSX2_UPSTREAM_DIR}/pcsx2"
		"${PCSX2_UPSTREAM_DIR}/common/include"
		"${PCSX2_UPSTREAM_DIR}/3rdparty/fmt/include"
		"${EMUCOREX_PCSX2_THIRD_PARTY_DIR}/SDL3/include"
)
if(EMUCOREX_HAS_OAKNUT)
	target_include_directories(emucore PRIVATE "${EMUCOREX_OAKNUT_INCLUDE_DIR}")
endif()
if(EMUCOREX_HAS_CAPSTONE)
	target_include_directories(emucore PRIVATE "${CMAKE_CURRENT_LIST_DIR}/third_party/capstone/include")
endif()
target_compile_definitions(emucore
	PRIVATE
		EMUCOREX_PCSX2_TAG="v2.7.316"
		EMUCOREX_PCSX2_COMMIT="53e3838c1dd59f611fa3d2fd36915903a1304c6e"
		EMUCOREX_HAS_OAKNUT=${EMUCOREX_HAS_OAKNUT}
		EMUCOREX_HAS_CAPSTONE=${EMUCOREX_HAS_CAPSTONE}
)
if(EMUCOREX_ENABLE_NATIVE_SELF_TESTS)
	target_compile_definitions(emucore PRIVATE EMUCOREX_ENABLE_NATIVE_SELF_TESTS=1)
endif()
target_link_libraries(emucore PRIVATE aaudio android EGL GLESv3 log)
if(EMUCOREX_HAS_CAPSTONE)
	target_link_libraries(emucore PRIVATE capstone_static)
endif()

if(EMUCOREX_LINK_UPSTREAM_PCSX2)
	target_link_libraries(emucore PRIVATE PCSX2 PCSX2_FLAGS)
	add_dependencies(emucore main_hook file_redirect_hook gsl_alloc_hook hook_impl)
endif()
