package com.sbro.emucorex.core.utils

object NetworkAdapterCollector {

    @JvmStatic
    fun generateUniqueMac(): String = "02:00:00:00:00:01"

    data class AdapterInfo(
        val name: String = "",
        val displayName: String = "",
        val isUp: Boolean = false,
        val isLoopback: Boolean = false,
        val isVirtual: Boolean = false,
        val supportsMulticast: Boolean = false,
        val mtu: Int = 0,
        val ipAddresses: Array<String> = emptyArray(),
        val dnsServers: Array<String> = emptyArray(),
        val routes: Array<RouteInfo> = emptyArray()
    ) {
        override fun equals(other: Any?): Boolean {
            if (this === other) return true
            if (javaClass != other?.javaClass) return false

            other as AdapterInfo

            if (name != other.name) return false
            if (displayName != other.displayName) return false
            if (isUp != other.isUp) return false
            if (isLoopback != other.isLoopback) return false
            if (isVirtual != other.isVirtual) return false
            if (supportsMulticast != other.supportsMulticast) return false
            if (mtu != other.mtu) return false
            if (!ipAddresses.contentEquals(other.ipAddresses)) return false
            if (!dnsServers.contentEquals(other.dnsServers) ) return false
            if (!routes.contentEquals(other.routes)) return false

            return true
        }

        override fun hashCode(): Int {
            var result = name.hashCode()
            result = 31 * result + displayName.hashCode()
            result = 31 * result + isUp.hashCode()
            result = 31 * result + isLoopback.hashCode()
            result = 31 * result + isVirtual.hashCode()
            result = 31 * result + supportsMulticast.hashCode()
            result = 31 * result + mtu
            result = 31 * result + ipAddresses.contentHashCode()
            result = 31 * result + dnsServers.contentHashCode()
            result = 31 * result + routes.contentHashCode()
            return result
        }

        data class RouteInfo(
            val destination: String = "",
            val address: String = "",
            val gateway: String = "",
            val prefix: Int = 0,
            val isIPv6: Boolean = false,
            val hasGateway: Boolean = false,
            val isDefault: Boolean = false,
            val isHostRoute: Boolean = false,
            val isNetworkRoute: Boolean = false,
            val isDirect: Boolean = false,
            val isAnyLocal: Boolean = false,
            val isSiteLocal: Boolean = false,
            val isLoopback: Boolean = false,
            val isLinkLocal: Boolean = false,
            val isMulticast: Boolean = false
        )
    }
}
