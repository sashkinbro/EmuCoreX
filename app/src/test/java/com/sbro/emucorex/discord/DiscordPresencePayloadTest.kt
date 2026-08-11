package com.sbro.emucorex.discord

import org.junit.Assert.assertEquals
import org.junit.Test

class DiscordPresencePayloadTest {
    @Test
    fun libraryDoesNotLeakGameMetadata() {
        val payload = payload(title = "", serial = "SLUS-00001", shareTitle = true, shareSerial = true)

        assertEquals("Library", payload.details)
        assertEquals("", payload.state)
        assertEquals("https://example.test/icon.png", payload.coverUrl)
    }

    @Test
    fun privateModeHidesTitleCoverAndSerial() {
        val payload = payload(
            title = "Shadow of the Colossus",
            serial = "SCUS-97472",
            shareTitle = false,
            shareSerial = false
        )

        assertEquals("Private game", payload.details)
        assertEquals("", payload.state)
        assertEquals("https://example.test/icon.png", payload.coverUrl)
    }

    @Test
    fun explicitSharingIncludesTitleSerialAndCover() {
        val payload = payload(
            title = "Gran Turismo 4",
            serial = "SCUS-97328",
            shareTitle = true,
            shareSerial = true
        )

        assertEquals("Playing Gran Turismo 4", payload.details)
        assertEquals("SCUS-97328", payload.state)
        assertEquals("https://example.test/cover.jpg", payload.coverUrl)
    }

    @Test
    fun pausedStateTakesPriorityOverSerial() {
        val payload = payload(
            title = "God of War II",
            serial = "SCUS-97481",
            paused = true,
            shareTitle = true,
            shareSerial = true
        )

        assertEquals("Paused", payload.state)
    }

    @Test
    fun idleIconMatchesTheAppEdition() {
        assertEquals(
            "https://raw.githubusercontent.com/sashkinbro/EmuCoreX/main/app/src/main/res/drawable-nodpi/ic_drawer_app.png",
            discordIdleImageUrl(isProUnlocked = false)
        )
        assertEquals(
            "https://raw.githubusercontent.com/sashkinbro/EmuCoreX/main/app/src/main/res/drawable-nodpi/ic_drawer_app_pro.png",
            discordIdleImageUrl(isProUnlocked = true)
        )
    }

    @Test
    fun friendsProtocolPreservesEmptyActivityAndAvatar() {
        val encoded = "Alice\u001fBully\u001fhttps://cdn.test/a.webp\u001eBob\u001f\u001f"

        assertEquals(
            listOf(
                DiscordFriend("Alice", "Bully", "https://cdn.test/a.webp"),
                DiscordFriend("Bob", "", "")
            ),
            parseDiscordFriends(encoded)
        )
    }

    private fun payload(
        title: String,
        serial: String,
        paused: Boolean = false,
        shareTitle: Boolean,
        shareSerial: Boolean
    ) = buildDiscordPresencePayload(
        gameTitle = title,
        gameSerial = serial,
        paused = paused,
        shareGameTitle = shareTitle,
        shareGameSerial = shareSerial,
        libraryText = "Library",
        privateGameText = "Private game",
        pausedText = "Paused",
        titleText = { "Playing $it" },
        coverUrl = "https://example.test/cover.jpg",
        idleImageUrl = "https://example.test/icon.png"
    )
}
