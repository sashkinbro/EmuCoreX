package com.sbro.emucorex.data

/**
 * DEV9 network settings grouped into one value. Keeps SettingsSnapshot/SettingsUiState below the
 * DEX 255-register limit for constructor/copy$default calls (see SettingsStateArityTest).
 */
data class Dev9Settings(
    val ethernetEnabled: Boolean = false,
    val ethernetDevice: String = "Auto",
    val interceptDhcp: Boolean = false,
    val dns1Mode: String = AppPreferences.DEV9_DNS_MODE_AUTO,
    val dns1: String = "0.0.0.0",
    val dns2Mode: String = AppPreferences.DEV9_DNS_MODE_AUTO,
    val dns2: String = "0.0.0.0",
    val logDhcp: Boolean = false,
    val logDns: Boolean = false,
    val localLinkMode: Int = AppPreferences.DEV9_LOCAL_LINK_OFF,
    val localLinkAddress: String = "192.168.43.1",
    val localLinkPort: Int = AppPreferences.DEFAULT_LOCAL_LINK_PORT,
    val localLinkPeerId: Int = 2,
    val localLinkRoomCode: String = ""
)
