cmake_minimum_required(VERSION 3.22.1)

project(EmuCoreXAndroidEntrypoint LANGUAGES C CXX)

# Discord Social SDK is an optional local build input used by the isolated helper process.
if(NOT DISCORD_SDK_DIR AND DEFINED ENV{DISCORD_SDK_DIR})
	set(DISCORD_SDK_DIR "$ENV{DISCORD_SDK_DIR}")
endif()

set(DISCORD_SDK_SO "${DISCORD_SDK_DIR}/${ANDROID_ABI}/libdiscord_partner_sdk.so")
set(DISCORD_SDK_INCLUDE "${DISCORD_SDK_DIR}/include")

if(DISCORD_SDK_DIR AND EXISTS "${DISCORD_SDK_SO}" AND EXISTS "${DISCORD_SDK_INCLUDE}/discordpp.h")
	add_library(discord_partner_sdk SHARED IMPORTED)
	set_target_properties(discord_partner_sdk PROPERTIES
		IMPORTED_LOCATION "${DISCORD_SDK_SO}"
		INTERFACE_INCLUDE_DIRECTORIES "${DISCORD_SDK_INCLUDE}")

	add_library(emucorex_discord SHARED discord_bridge.cpp)
	target_link_libraries(emucorex_discord PRIVATE discord_partner_sdk log)
	set_target_properties(emucorex_discord PROPERTIES OUTPUT_NAME "emucorex_discord")
	message(STATUS "Discord Social SDK enabled: ${DISCORD_SDK_SO}")
else()
	message(STATUS "Discord Social SDK unavailable; Discord integration will report unavailable")
endif()

add_subdirectory(
	"${CMAKE_CURRENT_LIST_DIR}/ARM_ANDROID"
	"${CMAKE_CURRENT_BINARY_DIR}/arm_android"
)
