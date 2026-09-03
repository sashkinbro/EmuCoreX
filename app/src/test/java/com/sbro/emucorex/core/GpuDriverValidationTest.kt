package com.sbro.emucorex.core

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class GpuDriverValidationTest {
    @Test
    fun `accepts a file with ELF magic`() {
        val file = File.createTempFile("driver", ".so")
        try {
            file.writeBytes(byteArrayOf(0x7F, 'E'.code.toByte(), 'L'.code.toByte(), 'F'.code.toByte(), 2, 1, 1))
            assertTrue(isValidDriverLibrary(file))
        } finally {
            file.delete()
        }
    }

    @Test
    fun `rejects empty files and non ELF content`() {
        val empty = File.createTempFile("empty", ".so")
        val html = File.createTempFile("error", ".so")
        try {
            assertFalse(isValidDriverLibrary(empty))
            html.writeText("<html>Not Found</html>")
            assertFalse(isValidDriverLibrary(html))
            assertFalse(isValidDriverLibrary(File(html.parent, "missing.so")))
        } finally {
            empty.delete()
            html.delete()
        }
    }
}
