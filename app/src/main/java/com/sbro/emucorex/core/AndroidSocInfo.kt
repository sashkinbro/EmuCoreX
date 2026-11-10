package com.sbro.emucorex.core

import java.io.File

/** Best-effort hints for ROMs that leave Build.SOC_MODEL unset. Read once per process. */
internal object AndroidSocInfo {
    val identifiers: List<String> by lazy {
        buildList {
            // Some ROMs expose the SoC only in these read-only properties. Hidden
            // API restrictions may reject access; sysfs/DT and Build still work.
            runCatching {
                val getter = Class.forName("android.os.SystemProperties")
                    .getMethod("get", String::class.java)
                listOf("ro.soc.model", "ro.vendor.soc.model", "ro.board.platform", "ro.hardware")
                    .forEach { key ->
                        runCatching { getter.invoke(null, key) as? String }.getOrNull()
                            ?.takeIf { it.isNotBlank() }?.let(::add)
                    }
            }
            listOf(
                "/sys/devices/soc0/machine", "/sys/devices/soc0/family",
                "/sys/devices/system/soc/soc0/machine", "/sys/devices/system/soc/soc0/family",
                "/sys/firmware/devicetree/base/compatible", "/sys/firmware/devicetree/base/model"
            ).forEach { path ->
                runCatching {
                    File(path).inputStream().use { input ->
                        val bytes = ByteArray(4096)
                        val size = input.read(bytes).coerceAtLeast(0)
                        String(bytes, 0, size, Charsets.UTF_8)
                    }
                }.getOrNull()?.split('\u0000')?.filter { it.isNotBlank() }?.let(::addAll)
            }
            runCatching {
                File("/proc/cpuinfo").useLines { lines ->
                    lines.filter { it.substringBefore(':').trim().equals("Hardware", ignoreCase = true) }
                        .map { it.substringAfter(':').trim() }.toList()
                }
            }.getOrNull()?.let(::addAll)
        }.distinct()
    }
}
