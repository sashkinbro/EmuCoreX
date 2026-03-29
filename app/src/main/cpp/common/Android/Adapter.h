//
// Created by Anonymous on 11/22/2025.
//

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include "DEV9/PacketReader/IP/IP_Address.h"

#ifndef ANDROID_ADAPTER_H
#define ANDROID_ADAPTER_H

namespace Android {
    // The only things you cannot get — because Android hides them — are:
    // * Route metric
    // * Kernel routing flags
    // * Route scope
    // * Per-route MTU
    // * ARP table
    // * MAC address (restricted on Android 10+)

    struct RouteInfo {
        std::string destination; // "10.80.129.236/30"
        std::string address;     // "10.80.129.236"
        int prefix = -1;
        bool isIPv6 = false;

        std::string gateway;
        bool hasGateway = false;
        bool isDefault = false;
        bool isHostRoute = false;
        bool isNetworkRoute = false;
        bool isDirect = false;

        bool isAnyLocal = false;
        bool isSiteLocal = false;
        bool isLoopback = false;
        bool isLinkLocal = false;
        bool isMulticast = false;
    };

    struct AdapterInfo {
        std::string name;
        std::string displayName;

        bool isUp = false;
        bool isLoopback = false;
        bool isVirtual = false;
        bool supportsMulticast = false;

        int mtu = 0;

        std::vector<std::string> ipAddresses;
        std::vector<std::string> dnsServers;
        std::vector<RouteInfo> routes;
    };

    std::vector<std::string> getAdapterGateways(const std::string& adapter_name);
    std::pair<std::string, std::string> getAdapterDNS(const std::string& adapter_name);

    PacketReader::IP::IP_Address ParseIpString(const std::string& ipStr);
}

extern std::unordered_map<std::string, Android::AdapterInfo> g_android_adapters;
extern std::mutex g_android_mutex;

#endif //ANDROID_ADAPTER_H
