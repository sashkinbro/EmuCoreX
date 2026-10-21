package com.sbro.emucorex.core

import org.junit.Assert.assertEquals
import org.junit.Test

class GpuTierClassifierTest {
    private fun adreno(name: String) = GpuModelInfo(GpuVendor.ADRENO, name)
    private fun mali(name: String) = GpuModelInfo(GpuVendor.MALI, name)
    private fun powerVr(name: String) = GpuModelInfo(GpuVendor.POWERVR, name)
    private fun xclipse(name: String) = GpuModelInfo(GpuVendor.XCLIPSE, name)

    @Test
    fun `classifies Adreno tiers`() {
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(adreno("Adreno 308")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(adreno("Adreno 505")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(adreno("Adreno 610")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(adreno("Adreno 616")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(adreno("Adreno 618")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(adreno("Adreno 619L")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(adreno("Adreno 620")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(adreno("Adreno 630")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(adreno("Adreno 642L")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(adreno("Adreno 640")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(adreno("Adreno 660")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(adreno("Adreno 680")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(adreno("Adreno 730")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(adreno("Adreno 840")))
    }

    @Test
    fun `classifies Mali tiers`() {
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(mali("Mali-400")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(mali("Mali-T880 MP12")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(mali("Mali-G31")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(mali("Mali-G52 MC2")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(mali("Mali-G71 MP2")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(mali("Mali-G57 MC1")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(mali("Mali-G310")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(mali("Mali-G57")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(mali("Mali-G57 MC2")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(mali("Mali-G68 MP5")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(mali("Mali-G76 MP12")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(mali("Mali-G510")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(mali("Mali-G77 MC9")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(mali("Mali-G78 MP14")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(mali("Mali-G610 MC4")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(mali("Mali-G715 MP7")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(mali("Immortalis-G715 MC11")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(mali("Immortalis-G925")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(mali("Mali-G1 Ultra MC12")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(mali("Mali-G2 Ultra NX MP12")))
    }

    @Test
    fun `classifies PowerVR and Xclipse tiers`() {
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(powerVr("PowerVR GE8320")))
        assertEquals(GpuTier.RED, GpuTierClassifier.classify(powerVr("PowerVR GM9446")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(powerVr("PowerVR DXT-48-1536")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(powerVr("PowerVR CXTP-48-1536")))
        assertEquals(GpuTier.YELLOW, GpuTierClassifier.classify(xclipse("Xclipse 530")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(xclipse("Xclipse 920")))
        assertEquals(GpuTier.GREEN, GpuTierClassifier.classify(xclipse("Xclipse 940")))
    }

    @Test
    fun `unknown models fail open`() {
        assertEquals(GpuTier.UNKNOWN, GpuTierClassifier.classify(null))
        assertEquals(GpuTier.UNKNOWN, GpuTierClassifier.classify(GpuModelInfo(GpuVendor.UNKNOWN, "Something")))
        assertEquals(GpuTier.UNKNOWN, GpuTierClassifier.classify(mali("Mali-G999")))
        assertEquals(GpuTier.UNKNOWN, GpuTierClassifier.classify(adreno("Adreno 617")))
    }
}
