#include "WFPNetworkFilter.h"
#include "LogFunctions.h"
#include <rpc.h>
#include <fwpmtypes.h>

HANDLE WFPNetworkFilter::g_engineHandle = nullptr;
GUID WFPNetworkFilter::g_providerKey = { 0 };
GUID WFPNetworkFilter::g_sublayerKey = { 0 };
std::vector<UINT64> WFPNetworkFilter::g_filterIds;

bool WFPNetworkFilter::Initialize() {
    if (g_engineHandle != nullptr) {
        LogWarn(L"[WFPNetworkFilter] Already initialized");
        return true;
    }

    // 生成唯一的 GUID
    UuidCreate(&g_providerKey);
    UuidCreate(&g_sublayerKey);

    // 打开 WFP 引擎
    FWPM_SESSION0 session = { 0 };
    session.flags = FWPM_SESSION_FLAG_DYNAMIC; // 进程退出时自动清理

    DWORD result = FwpmEngineOpen0(
        nullptr,
        RPC_C_AUTHN_WINNT,
        nullptr,
        &session,
        &g_engineHandle
    );

    if (result != ERROR_SUCCESS) {
        LogError(L"[WFPNetworkFilter] FwpmEngineOpen0 failed: %lu", result);
        return false;
    }

    // 创建 Provider
    if (!CreateProvider()) {
        FwpmEngineClose0(g_engineHandle);
        g_engineHandle = nullptr;
        return false;
    }

    // 创建 Sublayer
    if (!CreateSublayer()) {
        FwpmEngineClose0(g_engineHandle);
        g_engineHandle = nullptr;
        return false;
    }

    LogInfo(L"[WFPNetworkFilter] Initialized successfully");
    return true;
}

void WFPNetworkFilter::Cleanup() {
    if (g_engineHandle == nullptr) {
        return;
    }

    // 删除所有过滤器
    for (UINT64 filterId : g_filterIds) {
        FwpmFilterDeleteById0(g_engineHandle, filterId);
    }
    g_filterIds.clear();

    // 删除 Sublayer
    FwpmSubLayerDeleteByKey0(g_engineHandle, &g_sublayerKey);

    // 删除 Provider
    FwpmProviderDeleteByKey0(g_engineHandle, &g_providerKey);

    // 关闭引擎
    FwpmEngineClose0(g_engineHandle);
    g_engineHandle = nullptr;

    LogInfo(L"[WFPNetworkFilter] Cleanup complete");
}

bool WFPNetworkFilter::CreateProvider() {
    FWPM_PROVIDER0 provider = { 0 };
    provider.providerKey = g_providerKey;
    provider.displayData.name = (wchar_t*)L"Sandbox Network Filter Provider";
    provider.displayData.description = (wchar_t*)L"Provides network filtering for sandboxed processes";
    provider.flags = 0;

    DWORD result = FwpmProviderAdd0(g_engineHandle, &provider, nullptr);
    if (result != ERROR_SUCCESS) {
        LogError(L"[WFPNetworkFilter] FwpmProviderAdd0 failed: %lu", result);
        return false;
    }

    LogInfo(L"[WFPNetworkFilter] Provider created");
    return true;
}

bool WFPNetworkFilter::CreateSublayer() {
    FWPM_SUBLAYER0 sublayer = { 0 };
    sublayer.subLayerKey = g_sublayerKey;
    sublayer.displayData.name = (wchar_t*)L"Sandbox Network Filter Sublayer";
    sublayer.displayData.description = (wchar_t*)L"Sublayer for sandbox network filters";
    sublayer.providerKey = &g_providerKey;
    sublayer.weight = 0xFFFF; // 高优先级

    DWORD result = FwpmSubLayerAdd0(g_engineHandle, &sublayer, nullptr);
    if (result != ERROR_SUCCESS) {
        LogError(L"[WFPNetworkFilter] FwpmSubLayerAdd0 failed: %lu", result);
        return false;
    }

    LogInfo(L"[WFPNetworkFilter] Sublayer created");
    return true;
}

bool WFPNetworkFilter::AddProcessFilter(const std::wstring& processPath,
                                       int proxyPort,
                                       bool allowLocalhost) {
    if (g_engineHandle == nullptr) {
        LogError(L"[WFPNetworkFilter] Not initialized");
        return false;
    }

    // 1. 添加允许连接到代理的规则（最高优先级）
    UINT64 allowProxyFilterId = 0;
    if (!AddAllowProxyFilter(processPath, proxyPort, allowProxyFilterId)) {
        return false;
    }

    // 2. 如果需要，添加允许本地连接的规则
    UINT64 allowLocalhostFilterId = 0;
    if (allowLocalhost) {
        if (!AddAllowLocalhostFilter(processPath, allowLocalhostFilterId)) {
            FwpmFilterDeleteById0(g_engineHandle, allowProxyFilterId);
            return false;
        }
    }

    // 3. 添加阻止所有其他连接的规则（最低优先级）
    UINT64 blockFilterId = 0;
    if (!AddBlockFilter(processPath, blockFilterId)) {
        FwpmFilterDeleteById0(g_engineHandle, allowProxyFilterId);
        if (allowLocalhost) {
            FwpmFilterDeleteById0(g_engineHandle, allowLocalhostFilterId);
        }
        return false;
    }

    LogInfo(L"[WFPNetworkFilter] Added filters for process: %s", processPath.c_str());
    return true;
}

bool WFPNetworkFilter::AddAllowProxyFilter(const std::wstring& processPath,
                                          int proxyPort,
                                          UINT64& filterId) {
    FWP_BYTE_BLOB* appId = nullptr;
    if (!GetAppIdFromPath(processPath, &appId)) {
        return false;
    }

    FWPM_FILTER0 filter = { 0 };
    filter.subLayerKey = g_sublayerKey;
    filter.displayData.name = (wchar_t*)L"Allow Proxy Connection";
    filter.weight.type = FWP_UINT8;
    filter.weight.uint8 = 15; // 高优先级
    filter.action.type = FWP_ACTION_PERMIT;
    filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    filter.numFilterConditions = 3;

    FWPM_FILTER_CONDITION0 conditions[3] = { 0 };

    // 条件 1: 匹配应用程序
    conditions[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
    conditions[0].matchType = FWP_MATCH_EQUAL;
    conditions[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
    conditions[0].conditionValue.byteBlob = appId;

    // 条件 2: 匹配目标 IP (127.0.0.1)
    conditions[1].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
    conditions[1].matchType = FWP_MATCH_EQUAL;
    conditions[1].conditionValue.type = FWP_UINT32;
    conditions[1].conditionValue.uint32 = 0x0100007F; // 127.0.0.1 (网络字节序)

    // 条件 3: 匹配目标端口
    conditions[2].fieldKey = FWPM_CONDITION_IP_REMOTE_PORT;
    conditions[2].matchType = FWP_MATCH_EQUAL;
    conditions[2].conditionValue.type = FWP_UINT16;
    conditions[2].conditionValue.uint16 = (UINT16)proxyPort;

    filter.filterCondition = conditions;

    DWORD result = FwpmFilterAdd0(g_engineHandle, &filter, nullptr, &filterId);
    FreeAppId(appId);

    if (result != ERROR_SUCCESS) {
        LogError(L"[WFPNetworkFilter] Failed to add allow proxy filter: %lu", result);
        return false;
    }

    g_filterIds.push_back(filterId);
    LogInfo(L"[WFPNetworkFilter] Added allow proxy filter (port %d)", proxyPort);
    return true;
}

bool WFPNetworkFilter::AddAllowLocalhostFilter(const std::wstring& processPath,
                                               UINT64& filterId) {
    FWP_BYTE_BLOB* appId = nullptr;
    if (!GetAppIdFromPath(processPath, &appId)) {
        return false;
    }

    FWPM_FILTER0 filter = { 0 };
    filter.subLayerKey = g_sublayerKey;
    filter.displayData.name = (wchar_t*)L"Allow Localhost Connection";
    filter.weight.type = FWP_UINT8;
    filter.weight.uint8 = 10; // 中等优先级
    filter.action.type = FWP_ACTION_PERMIT;
    filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    filter.numFilterConditions = 2;

    FWPM_FILTER_CONDITION0 conditions[2] = { 0 };

    // 条件 1: 匹配应用程序
    conditions[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
    conditions[0].matchType = FWP_MATCH_EQUAL;
    conditions[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
    conditions[0].conditionValue.byteBlob = appId;

    // 条件 2: 匹配目标 IP (127.0.0.0/8)
    conditions[1].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
    conditions[1].matchType = FWP_MATCH_EQUAL;
    conditions[1].conditionValue.type = FWP_V4_ADDR_MASK;

    FWP_V4_ADDR_AND_MASK addrMask;
    addrMask.addr = 0x0000007F; // 127.0.0.0
    addrMask.mask = 0x000000FF; // 255.0.0.0
    conditions[1].conditionValue.v4AddrMask = &addrMask;

    filter.filterCondition = conditions;

    DWORD result = FwpmFilterAdd0(g_engineHandle, &filter, nullptr, &filterId);
    FreeAppId(appId);

    if (result != ERROR_SUCCESS) {
        LogError(L"[WFPNetworkFilter] Failed to add allow localhost filter: %lu", result);
        return false;
    }

    g_filterIds.push_back(filterId);
    LogInfo(L"[WFPNetworkFilter] Added allow localhost filter");
    return true;
}

bool WFPNetworkFilter::AddBlockFilter(const std::wstring& processPath,
                                      UINT64& filterId) {
    FWP_BYTE_BLOB* appId = nullptr;
    if (!GetAppIdFromPath(processPath, &appId)) {
        return false;
    }

    FWPM_FILTER0 filter = { 0 };
    filter.subLayerKey = g_sublayerKey;
    filter.displayData.name = (wchar_t*)L"Block All Outbound";
    filter.weight.type = FWP_UINT8;
    filter.weight.uint8 = 5; // 低优先级
    filter.action.type = FWP_ACTION_BLOCK;
    filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    filter.numFilterConditions = 1;

    FWPM_FILTER_CONDITION0 condition = { 0 };
    condition.fieldKey = FWPM_CONDITION_ALE_APP_ID;
    condition.matchType = FWP_MATCH_EQUAL;
    condition.conditionValue.type = FWP_BYTE_BLOB_TYPE;
    condition.conditionValue.byteBlob = appId;

    filter.filterCondition = &condition;

    DWORD result = FwpmFilterAdd0(g_engineHandle, &filter, nullptr, &filterId);
    FreeAppId(appId);

    if (result != ERROR_SUCCESS) {
        LogError(L"[WFPNetworkFilter] Failed to add block filter: %lu", result);
        return false;
    }

    g_filterIds.push_back(filterId);
    LogInfo(L"[WFPNetworkFilter] Added block filter");
    return true;
}

bool WFPNetworkFilter::GetAppIdFromPath(const std::wstring& processPath,
                                       FWP_BYTE_BLOB** appId) {
    DWORD result = FwpmGetAppIdFromFileName0(processPath.c_str(), appId);
    if (result != ERROR_SUCCESS) {
        LogError(L"[WFPNetworkFilter] FwpmGetAppIdFromFileName0 failed: %lu", result);
        return false;
    }
    return true;
}

void WFPNetworkFilter::FreeAppId(FWP_BYTE_BLOB* appId) {
    if (appId != nullptr) {
        FwpmFreeMemory0((void**)&appId);
    }
}

bool WFPNetworkFilter::RemoveProcessFilter(const std::wstring& processPath) {
    // 在 Cleanup 中统一处理所有过滤器
    // 如果需要单独移除，需要维护 processPath -> filterIds 的映射
    LogWarn(L"[WFPNetworkFilter] RemoveProcessFilter not implemented, use Cleanup instead");
    return true;
}
