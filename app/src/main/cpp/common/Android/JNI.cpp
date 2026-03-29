//
// Created by Anonymous on 11/22/2025.
//
#include <jni.h>
#include <string>
#include <utility>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "SDL3/SDL.h"
#include "common/Android/Adapter.h"

std::unordered_map<std::string, Android::AdapterInfo> g_android_adapters;
std::mutex g_android_mutex;

static std::string getStringFieldFromObj(JNIEnv* env, jobject obj, jclass cls, const char* name) {
    jfieldID fid = env->GetFieldID(cls, name, "Ljava/lang/String;");
    jstring jstr = (jstring)env->GetObjectField(obj, fid);
    if (!jstr) return "";
    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string value(chars);
    env->ReleaseStringUTFChars(jstr, chars);
    return value;
}

static bool getBoolFieldFromObj(JNIEnv* env, jobject obj, jclass cls, const char* name) {
    jfieldID fid = env->GetFieldID(cls, name, "Z");
    return env->GetBooleanField(obj, fid);
}

static int getIntFieldFromObj(JNIEnv* env, jobject obj, jclass cls, const char* name) {
    jfieldID fid = env->GetFieldID(cls, name, "I");
    return env->GetIntField(obj, fid);
}

extern "C"
JNIEXPORT void JNICALL
Java_com_sbro_emucorex_core_utils_NetworkAdapterCollector_onAdaptersCollected(
        JNIEnv* env,
        jclass clazz,
        jobjectArray adaptersArray) {

    std::lock_guard<std::mutex> lock(g_android_mutex);
    g_android_adapters.clear();

    jsize adapterCount = env->GetArrayLength(adaptersArray);
    for (jsize i = 0; i < adapterCount; ++i) {
        jobject adapterObj = env->GetObjectArrayElement(adaptersArray, i);
        jclass adapterCls = env->GetObjectClass(adapterObj);

        Android::AdapterInfo adapter;

        // --- Strings ---
        auto getStringField = [&](const char* name) -> std::string {
            jfieldID fid = env->GetFieldID(adapterCls, name, "Ljava/lang/String;");
            jstring jstr = (jstring)env->GetObjectField(adapterObj, fid);
            if (!jstr) return "";
            const char* chars = env->GetStringUTFChars(jstr, nullptr);
            std::string val(chars);
            env->ReleaseStringUTFChars(jstr, chars);
            return val;
        };

        adapter.name = getStringField("name");
        adapter.displayName = getStringField("displayName");

        // --- Booleans ---
        auto getBoolField = [&](const char* name) -> bool {
            jfieldID fid = env->GetFieldID(adapterCls, name, "Z");
            return env->GetBooleanField(adapterObj, fid);
        };
        adapter.isUp = getBoolField("isUp");
        adapter.isLoopback = getBoolField("isLoopback");
        adapter.isVirtual = getBoolField("isVirtual");
        adapter.supportsMulticast = getBoolField("supportsMulticast");

        // --- MTU ---
        {
            jfieldID fid = env->GetFieldID(adapterCls, "mtu", "I");
            adapter.mtu = env->GetIntField(adapterObj, fid);
        }

        // --- IP addresses ---
        {
            jfieldID fid = env->GetFieldID(adapterCls, "ipAddresses", "[Ljava/lang/String;");
            jobjectArray ipArray = (jobjectArray)env->GetObjectField(adapterObj, fid);
            if (ipArray) {
                jsize ipCount = env->GetArrayLength(ipArray);
                for (jsize j = 0; j < ipCount; ++j) {
                    jstring jstr = (jstring)env->GetObjectArrayElement(ipArray, j);
                    const char* chars = env->GetStringUTFChars(jstr, nullptr);
                    adapter.ipAddresses.emplace_back(chars);
                    env->ReleaseStringUTFChars(jstr, chars);
                }
            }
        }

        // --- DNS servers ---
        {
            jfieldID fid = env->GetFieldID(adapterCls, "dnsServers", "[Ljava/lang/String;");
            jobjectArray dnsArray = (jobjectArray)env->GetObjectField(adapterObj, fid);
            if (dnsArray) {
                jsize dnsCount = env->GetArrayLength(dnsArray);
                for (jsize j = 0; j < dnsCount; ++j) {
                    jstring jstr = (jstring)env->GetObjectArrayElement(dnsArray, j);
                    const char* chars = env->GetStringUTFChars(jstr, nullptr);
                    adapter.dnsServers.emplace_back(chars);
                    env->ReleaseStringUTFChars(jstr, chars);
                }
            }
        }

        // --- Routes ---
        {
            jfieldID fid = env->GetFieldID(
                    adapterCls,
                    "routes",
                    "[Lcom/sbro/emucorex/core/utils/NetworkAdapterCollector$AdapterInfo$RouteInfo;"
            );

            jobjectArray routeArray = (jobjectArray)env->GetObjectField(adapterObj, fid);
            if (routeArray) {
                jsize routeCount = env->GetArrayLength(routeArray);
                for (jsize j = 0; j < routeCount; ++j) {

                    jobject routeObj = env->GetObjectArrayElement(routeArray, j);
                    jclass routeCls = env->GetObjectClass(routeObj);

                    Android::RouteInfo route;

                    // Strings
                    route.destination = getStringFieldFromObj(env, routeObj, routeCls, "destination");
                    route.address     = getStringFieldFromObj(env, routeObj, routeCls, "address");
                    route.gateway     = getStringFieldFromObj(env, routeObj, routeCls, "gateway");

                    // Integers
                    route.prefix = getIntFieldFromObj(env, routeObj, routeCls, "prefix");

                    // Booleans
                    route.isIPv6        = getBoolFieldFromObj(env, routeObj, routeCls, "isIPv6");
                    route.hasGateway    = getBoolFieldFromObj(env, routeObj, routeCls, "hasGateway");
                    route.isDefault     = getBoolFieldFromObj(env, routeObj, routeCls, "isDefault");
                    route.isHostRoute   = getBoolFieldFromObj(env, routeObj, routeCls, "isHostRoute");
                    route.isNetworkRoute= getBoolFieldFromObj(env, routeObj, routeCls, "isNetworkRoute");
                    route.isDirect      = getBoolFieldFromObj(env, routeObj, routeCls, "isDirect");

                    route.isAnyLocal    = getBoolFieldFromObj(env, routeObj, routeCls, "isAnyLocal");
                    route.isSiteLocal   = getBoolFieldFromObj(env, routeObj, routeCls, "isSiteLocal");
                    route.isLoopback    = getBoolFieldFromObj(env, routeObj, routeCls, "isLoopback");
                    route.isLinkLocal   = getBoolFieldFromObj(env, routeObj, routeCls, "isLinkLocal");
                    route.isMulticast   = getBoolFieldFromObj(env, routeObj, routeCls, "isMulticast");

                    adapter.routes.push_back(route);
                }
            }
        }

        g_android_adapters[adapter.name] = adapter;
    }
}

std::string generateUniqueMacFromJava()
{
    auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());

    jclass cls = env->FindClass("com/sbro/emucorex/core/utils/NetworkAdapterCollector");
    jmethodID mid = env->GetStaticMethodID(cls, "generateUniqueMac", "()Ljava/lang/String;");

    jstring jstr = (jstring)env->CallStaticObjectMethod(cls, mid);

    const char* chars = env->GetStringUTFChars(jstr, nullptr);
    std::string result(chars);
    env->ReleaseStringUTFChars(jstr, chars);

    return result;
}

std::vector<std::string> Android::getAdapterGateways(const std::string& adapter_name)
{
    std::lock_guard<std::mutex> lock(g_android_mutex);

    auto it = g_android_adapters.find(adapter_name);
    if (it == g_android_adapters.end()) {
        return {}; // adapter not found
    }

    std::vector<std::string> gameways;

    for(auto route : it->second.routes)
    {
        if(!route.isAnyLocal && !route.isIPv6 && !route.isDefault)
            gameways.push_back(route.gateway);
    }

    return gameways;
}

std::pair<std::string, std::string> Android::getAdapterDNS(const std::string& adapter_name)
{
    std::lock_guard<std::mutex> lock(g_android_mutex);

    auto it = g_android_adapters.find(adapter_name);
    if (it == g_android_adapters.end()) {
        return {"", ""}; // adapter not found
    }

    const auto& dnsList = it->second.dnsServers;
    std::string dns1 = dnsList.size() > 0 ? dnsList[0] : "";
    std::string dns2 = dnsList.size() > 1 ? dnsList[1] : "";

    return {dns1, dns2};
}

PacketReader::IP::IP_Address Android::ParseIpString(const std::string& ipStr)
{
    PacketReader::IP::IP_Address ip{};
    std::array<int, 4> parts = {0,0,0,0};

    if (sscanf(ipStr.c_str(), "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]) == 4) {
        for (int i = 0; i < 4; ++i) {
            ip.bytes[i] = static_cast<uint8_t>(parts[i]);
        }
    }
    return ip;
}