package com.sbro.emucorex.core

import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class DeviceGpuSnapshotCacheTest {
    @Test
    fun delayedPreferencesCannotReplaceACompletedRendererProbe() {
        val cache = DeviceGpuSnapshotCache()
        val reading = CountDownLatch(1)
        val releaseRead = CountDownLatch(1)
        val executor = Executors.newSingleThreadExecutor()
        try {
            val read = executor.submit<DeviceGpuSnapshot> {
                cache.getOrResolve {
                    reading.countDown()
                    assertTrue(releaseRead.await(5, TimeUnit.SECONDS))
                    DeviceGpuInfoResolver.resolve(null, "Rockchip RK3566")
                }
            }
            assertTrue(reading.await(5, TimeUnit.SECONDS))
            val detected = DeviceGpuInfoResolver.resolve("Mali-G52 r1 MC1", "Rockchip RK3566")
            cache.publish(detected)
            releaseRead.countDown()
            assertEquals(detected, read.get(5, TimeUnit.SECONDS))
            assertEquals(detected, cache.peek())
            assertEquals(detected, cache.snapshot.value)
        } finally {
            releaseRead.countDown()
            executor.shutdownNow()
        }
    }

    @Test
    fun rendererProbeUpgradesAnEarlierSocFallback() {
        val cache = DeviceGpuSnapshotCache()
        val fallback = cache.getOrResolve { DeviceGpuInfoResolver.resolve(null, "Rockchip RK3566") }
        assertEquals(GpuInfoSource.SOC_CATALOG, fallback.source)
        val detected = DeviceGpuInfoResolver.resolve("Mali-G52 MC1", "Rockchip RK3566")
        cache.publish(detected)
        assertEquals(detected, cache.getOrResolve { error("The cached renderer should be reused") })
        assertEquals(detected, cache.snapshot.value)
    }
}
