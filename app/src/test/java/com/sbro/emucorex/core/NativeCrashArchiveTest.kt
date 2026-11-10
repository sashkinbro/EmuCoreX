package com.sbro.emucorex.core

import org.junit.Assert.*
import org.junit.Test
import java.io.ByteArrayOutputStream

class NativeCrashArchiveTest {
    @Test fun traceAtTheLimitIsPreservedByteForByte() {
        val bytes = ByteArray(8193) { (it % 251).toByte() }
        val output = ByteArrayOutputStream()
        copyBoundedTrace(bytes.inputStream(), output, bytes.size)
        assertArrayEquals(bytes, output.toByteArray())
    }

    @Test fun oversizedSystemTraceCannotGrowTheArchivePastItsBound() {
        val output = ByteArrayOutputStream()
        assertNotNull(runCatching { copyBoundedTrace(ByteArray(8193).inputStream(), output, 8192) }.exceptionOrNull())
        assertEquals(8192, output.size())
    }
}
