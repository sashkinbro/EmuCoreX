package com.sbro.emucorex.core

import android.provider.DocumentsContract
import org.junit.Assert.assertEquals
import org.junit.Test

class SetupValidatorTest {
    @Test
    fun directoryMimeTypeIsRecognizedWithoutDisplayName() {
        assertEquals(
            SetupValidator.DocumentEntryKind.DIRECTORY,
            SetupValidator.classifyDocumentEntry(DocumentsContract.Document.MIME_TYPE_DIR, null)
        )
    }

    @Test
    fun gameFileIsRecognizedWhenProviderReturnsNullMimeType() {
        assertEquals(
            SetupValidator.DocumentEntryKind.GAME_FILE,
            SetupValidator.classifyDocumentEntry(null, "Shadow of the Colossus.ISO")
        )
    }

    @Test
    fun gameFileIsRecognizedWithGenericMimeType() {
        assertEquals(
            SetupValidator.DocumentEntryKind.GAME_FILE,
            SetupValidator.classifyDocumentEntry("application/octet-stream", "game.chd")
        )
    }

    @Test
    fun missingMimeTypeWithoutGameExtensionRemainsTraversable() {
        assertEquals(
            SetupValidator.DocumentEntryKind.UNKNOWN,
            SetupValidator.classifyDocumentEntry(null, "PS2 Games")
        )
    }

    @Test
    fun unsupportedRegularFileIsIgnored() {
        assertEquals(
            SetupValidator.DocumentEntryKind.OTHER,
            SetupValidator.classifyDocumentEntry("text/plain", "notes.txt")
        )
    }
}
