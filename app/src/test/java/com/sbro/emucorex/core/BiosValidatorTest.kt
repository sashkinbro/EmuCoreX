package com.sbro.emucorex.core

import android.provider.DocumentsContract
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class BiosValidatorTest {
    @Test
    fun documentCheckReturnsFalseForInvalidDocumentUri() {
        assertFalse(BiosValidator.documentCheckOrFalse {
            throw IllegalArgumentException("Not a document tree URI")
        })
    }

    @Test
    fun documentCheckReturnsFalseWhenUriPermissionWasRevoked() {
        assertFalse(BiosValidator.documentCheckOrFalse {
            throw SecurityException("Permission revoked")
        })
    }

    @Test
    fun documentCheckPreservesSuccessfulResult() {
        assertTrue(BiosValidator.documentCheckOrFalse { true })
    }

    @Test
    fun directoryMimeTypeIsRecognizedWithoutName() {
        assertEquals(
            BiosValidator.DocumentEntryKind.DIRECTORY,
            BiosValidator.classifyDocumentEntry(DocumentsContract.Document.MIME_TYPE_DIR, null)
        )
    }

    @Test
    fun biosFileIsRecognizedWhenProviderReturnsNullMimeType() {
        assertEquals(
            BiosValidator.DocumentEntryKind.BIOS_FILE,
            BiosValidator.classifyDocumentEntry(null, "SCPH-70012_BIOS_V12_USA_200.bin")
        )
    }

    @Test
    fun biosFileIsRecognizedWithGenericMimeType() {
        assertEquals(
            BiosValidator.DocumentEntryKind.BIOS_FILE,
            BiosValidator.classifyDocumentEntry("application/octet-stream", "ps2-0230a-20080220.bin")
        )
    }

    @Test
    fun unknownEntryRemainsTraversable() {
        assertEquals(
            BiosValidator.DocumentEntryKind.UNKNOWN,
            BiosValidator.classifyDocumentEntry(null, "Firmware")
        )
    }

    @Test
    fun unrelatedDocumentIsIgnored() {
        assertEquals(
            BiosValidator.DocumentEntryKind.OTHER,
            BiosValidator.classifyDocumentEntry("text/plain", "readme.txt")
        )
    }

    @Test
    fun validBiosAllowsUnknownProviderSize() {
        assertTrue(BiosValidator.isUsableMainBiosImage("bios.bin", 0L))
    }

    @Test
    fun validBiosAllowsExpectedSize() {
        assertTrue(BiosValidator.isUsableMainBiosImage("SCPH-39001.bin", 4L * 1024L * 1024L))
    }

    @Test
    fun selectedScph39001FileFromRealPickerIsAccepted() {
        assertTrue(BiosValidator.isUsableMainBiosImage("[SCPH39001].bin", 4L * 1024L * 1024L))
    }

    @Test
    fun genericBinRemainsCandidateForNativeContentValidation() {
        assertEquals(
            BiosValidator.DocumentEntryKind.BIOS_FILE,
            BiosValidator.classifyDocumentEntry("application/octet-stream", "70012.bin")
        )
    }

    @Test
    fun biosWithImplausibleSizeIsRejected() {
        assertFalse(BiosValidator.isUsableMainBiosImage("bios.bin", 64L * 1024L))
    }

    @Test
    fun unrelatedBinaryIsRejected() {
        assertFalse(BiosValidator.isUsableMainBiosImage("game.bin", 4L * 1024L * 1024L))
    }
}
