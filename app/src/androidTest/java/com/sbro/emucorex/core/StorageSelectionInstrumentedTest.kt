package com.sbro.emucorex.core

import android.provider.DocumentsContract
import androidx.documentfile.provider.DocumentFile
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.sbro.emucorex.data.AppPreferences
import com.sbro.emucorex.data.GameRepository
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class StorageSelectionInstrumentedTest {
    private val context = ApplicationProvider.getApplicationContext<android.content.Context>()
    private val treeUri = DocumentsContract.buildTreeDocumentUri(
        TestGameDocumentsProvider.AUTHORITY,
        TestGameDocumentsProvider.ROOT_ID
    )

    @Test
    fun treeWithNullMimeDirectoryAndGameIsReadable() {
        val root = requireNotNull(DocumentFile.fromTreeUri(context, treeUri))
        context.contentResolver.query(
            root.uri,
            arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID),
            null,
            null,
            null
        )?.use { cursor -> assertTrue("Test provider root must be queryable", cursor.moveToFirst()) }
            ?: error("Test provider returned a null root cursor")
        val rootChildren = root.listFiles()
        assertTrue("Root must expose the null-MIME nested folder", rootChildren.size == 1)
        val nestedChildren = rootChildren.single().listFiles()
        assertTrue("Nested folder must expose the null-MIME ISO", nestedChildren.size == 1)
        context.contentResolver.openFileDescriptor(nestedChildren.single().uri, "r")?.use { descriptor ->
            assertTrue("Provider ISO descriptor must be non-empty", descriptor.statSize != 0L)
        } ?: error("Provider returned no ISO descriptor")
        assertTrue(SetupValidator.isGameFolderPresentForStartup(context, treeUri.toString()))
        assertTrue(SetupValidator.isGameFolderAccessible(context, treeUri.toString()))
        assertTrue(SetupValidator.hasCoreReadableGameFile(context, treeUri.toString()))
    }

    @Test
    fun selectedTreeSurvivesPreferencesRoundTrip() = runBlocking {
        val preferences = AppPreferences(context)
        val rawPath = treeUri.toString()
        preferences.removeGamePath(rawPath)
        try {
            preferences.addGamePath(rawPath)
            assertTrue(rawPath in preferences.gamePaths.first())
        } finally {
            preferences.removeGamePath(rawPath)
        }
    }

    @Test
    fun emptySelectedTreeRemainsAccessibleButIsNotGameReady() {
        val emptyTreeUri = DocumentsContract.buildTreeDocumentUri(
            TestGameDocumentsProvider.AUTHORITY,
            TestGameDocumentsProvider.EMPTY_ROOT_ID
        )
        assertTrue(SetupValidator.isGameFolderPresentForStartup(context, emptyTreeUri.toString()))
        assertTrue(SetupValidator.isGameFolderAccessible(context, emptyTreeUri.toString()))
        assertFalse(SetupValidator.hasCoreReadableGameFile(context, emptyTreeUri.toString()))
    }

    @Test
    fun libraryScannerFindsGameFromNullMimeProvider() {
        val games = GameRepository().scanDirectoryFromUri(treeUri, context)
        assertTrue(games.any { game ->
            game.fileName == "Gran Turismo 4.ISO" && game.path.endsWith("game.iso")
        })
    }

    @Test
    fun invalidDocumentUriFailsWithoutCrashing() {
        val singleUri = DocumentsContract.buildDocumentUri(
            TestGameDocumentsProvider.AUTHORITY,
            "games-root/nested/game.iso"
        )
        assertFalse(SetupValidator.isGameFolderAccessible(context, singleUri.toString()))
        assertFalse(BiosValidator.hasUsableBiosFiles(context, singleUri.toString()))
    }
}
