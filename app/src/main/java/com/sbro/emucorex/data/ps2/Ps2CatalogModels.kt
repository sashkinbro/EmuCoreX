package com.sbro.emucorex.data.ps2

import com.sbro.emucorex.data.pcsx2.Pcsx2CompatibilityEntry

data class Ps2CatalogSummary(
    val igdbId: Long,
    val name: String,
    val normalizedName: String,
    val storyline: String?,
    val summary: String?,
    val year: Int?,
    val rating: Double?,
    val coverUrl: String?,
    val heroUrl: String?,
    val genres: List<String> = emptyList(),
    val primarySerial: String? = null,
    val pcsx2Compatibility: Pcsx2CompatibilityEntry? = null
)

data class Ps2CatalogDetails(
    val igdbId: Long,
    val name: String,
    val normalizedName: String,
    val year: Int?,
    val rating: Double?,
    val storyline: String?,
    val summary: String?,
    val genres: List<String>,
    val screenshots: List<String>,
    val videos: List<String>,
    val coverUrl: String?,
    val heroUrl: String?,
    val primarySerial: String? = null,
    val pcsx2Compatibility: Pcsx2CompatibilityEntry? = null
)

data class Ps2CatalogMatch(
    val details: Ps2CatalogDetails,
    val score: Int,
    val matchedBySerial: Boolean,
    val matchedBy: String = if (matchedBySerial) "serial" else "title"
)
