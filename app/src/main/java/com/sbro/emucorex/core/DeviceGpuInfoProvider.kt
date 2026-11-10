package com.sbro.emucorex.core

import android.content.Context
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

internal object DeviceGpuInfoResolver {
    private val CORE_COUNT = Regex("""(?:mc|mp)\s*\d{1,2}""", RegexOption.IGNORE_CASE)

    fun resolve(
        renderer: String?,
        socName: String?,
        catalog: (String?) -> GpuModelInfo? = SocGpuCatalog::forSoc
    ): DeviceGpuSnapshot {
        val parsed = GpuModelParser.parse(renderer)
        val fromCatalog = catalog(socName)
        val model = parsed?.let { mergeCoreCount(it, fromCatalog) } ?: fromCatalog
        val source = when {
            parsed != null -> GpuInfoSource.GL_RENDERER
            fromCatalog != null -> GpuInfoSource.SOC_CATALOG
            else -> GpuInfoSource.UNKNOWN
        }
        return DeviceGpuSnapshot(
            model = model,
            tier = GpuTierClassifier.classify(model),
            renderer = renderer,
            source = source
        )
    }

    private fun mergeCoreCount(parsed: GpuModelInfo, catalog: GpuModelInfo?): GpuModelInfo {
        if (catalog == null) return parsed
        if (parsed.vendor != catalog.vendor) return parsed
        if (CORE_COUNT.containsMatchIn(parsed.displayName)) return parsed
        if (!CORE_COUNT.containsMatchIn(catalog.displayName)) return parsed
        if (baseModel(parsed.displayName) != baseModel(catalog.displayName)) return parsed
        return catalog
    }

    private fun baseModel(displayName: String): String =
        displayName.replace(Regex("""\s*(?:mc|mp)\s*\d{1,2}$""", RegexOption.IGNORE_CASE), "")
            .trim()
            .lowercase()
}

/** A slow preferences read must never overwrite an already-completed GL probe. */
internal class DeviceGpuSnapshotCache {
    private val monitor = Any()
    @Volatile private var cached: DeviceGpuSnapshot? = null
    private val state = MutableStateFlow<DeviceGpuSnapshot?>(null)
    val snapshot: StateFlow<DeviceGpuSnapshot?> = state.asStateFlow()

    fun peek(): DeviceGpuSnapshot? = cached

    fun getOrResolve(resolve: () -> DeviceGpuSnapshot): DeviceGpuSnapshot {
        cached?.let { return it }
        val candidate = resolve()
        return synchronized(monitor) {
            cached ?: candidate.also {
                cached = it
                state.value = it
            }
        }
    }

    fun publish(value: DeviceGpuSnapshot) = synchronized(monitor) {
        cached = value
        state.value = value
    }
}

object DeviceGpuInfoProvider {
    const val CLASSIFIER_VERSION = 4

    private const val PREFS_NAME = "device_gpu_profile"
    private const val KEY_VERSION = "classifier_version"
    private const val KEY_VENDOR = "gpu_vendor"
    private const val KEY_MODEL = "gpu_model"
    private const val KEY_TIER = "gpu_tier"
    private const val KEY_SOURCE = "gpu_source"
    private const val KEY_RENDERER = "gpu_renderer"

    private val lock = Mutex()

    private val cache = DeviceGpuSnapshotCache()
    val snapshot: StateFlow<DeviceGpuSnapshot?> = cache.snapshot

    internal var rendererProvider: () -> String? = { GpuRendererQuery.query() }
    internal var socNameProvider: () -> String = { MobileSocNameMapper.currentDeviceName() }

    fun get(context: Context): DeviceGpuSnapshot = cache.getOrResolve {
        val stored = runCatching { readStored(context) }.getOrNull()
        stored ?: DeviceGpuInfoResolver.resolve(
            renderer = null,
            socName = runCatching { socNameProvider() }.getOrNull()
        )
    }

    suspend fun preload(context: Context): DeviceGpuSnapshot = lock.withLock {
        cache.peek()?.takeIf { it.source == GpuInfoSource.GL_RENDERER }?.let { return it }
        val resolved = runCatching {
            DeviceGpuInfoResolver.resolve(
                renderer = rendererProvider(),
                socName = socNameProvider()
            )
        }.getOrElse {
            DeviceGpuInfoResolver.resolve(renderer = null, socName = null)
        }
        cache.publish(resolved)
        runCatching { writeStored(context, resolved) }
        resolved
    }

    private fun readStored(context: Context): DeviceGpuSnapshot? {
        val prefs = context.applicationContext
            .getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        if (prefs.getInt(KEY_VERSION, -1) != CLASSIFIER_VERSION) return null
        val vendor = enumValue<GpuVendor>(prefs.getString(KEY_VENDOR, null)) ?: return null
        val displayName = prefs.getString(KEY_MODEL, null)?.takeIf { it.isNotBlank() } ?: return null
        val tier = enumValue<GpuTier>(prefs.getString(KEY_TIER, null)) ?: GpuTier.UNKNOWN
        val source = enumValue<GpuInfoSource>(prefs.getString(KEY_SOURCE, null)) ?: GpuInfoSource.UNKNOWN
        return DeviceGpuSnapshot(
            model = GpuModelInfo(vendor, displayName),
            tier = tier,
            renderer = prefs.getString(KEY_RENDERER, null),
            source = source
        )
    }

    private fun writeStored(context: Context, snapshot: DeviceGpuSnapshot) {
        val model = snapshot.model
        context.applicationContext
            .getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putInt(KEY_VERSION, CLASSIFIER_VERSION)
            .putString(KEY_VENDOR, model?.vendor?.name)
            .putString(KEY_MODEL, model?.displayName)
            .putString(KEY_TIER, snapshot.tier.name)
            .putString(KEY_SOURCE, snapshot.source.name)
            .putString(KEY_RENDERER, snapshot.renderer)
            .apply()
    }

    private inline fun <reified T : Enum<T>> enumValue(raw: String?): T? {
        if (raw.isNullOrBlank()) return null
        return enumValues<T>().firstOrNull { it.name == raw }
    }
}

object HardwareWarningPolicy {
    fun shouldShow(tier: GpuTier, warningAlreadyShown: Boolean): Boolean =
        tier == GpuTier.RED && !warningAlreadyShown
}
