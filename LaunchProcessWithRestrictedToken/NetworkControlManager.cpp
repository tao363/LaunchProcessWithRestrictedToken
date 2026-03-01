#include "NetworkControlManager.h"
#include "NetworkFilterPlugin.h"
#include "FirewallControl.h"
#include "WFPNetworkFilter.h"
#include "LogFunctions.h"
#include <algorithm>

NetworkControlManager::NetworkControlManager()
    : method_(NetworkControlMethod::None)
    , proxyPort_(8080)
    , allowLocalhost_(true)
    , initialized_(false)
    , globalMode_(false) {
}

NetworkControlManager::~NetworkControlManager() {
    Cleanup();
}

bool NetworkControlManager::LoadConfig(const std::wstring& configPath) {
    ConfigReader config;
    if (!config.Load(configPath)) {
        LogWarn(L"[NetworkControlManager] Failed to load config, using defaults");
        // 使用默认值
        method_ = NetworkControlMethod::WindowsFirewall;
        proxyPort_ = 8080;
        allowedDomains_ = { L"*" };
        allowLocalhost_ = true;
        return false;
    }

    // 读取网络控制方法
    std::wstring methodStr = config.GetString(L"NetworkControl", L"Method", L"WindowsFirewall");
    method_ = ParseMethod(methodStr);

    // 读取代理端口
    proxyPort_ = config.GetInt(L"NetworkControl", L"ProxyPort", 8080);

    // 读取是否允许本地连接
    allowLocalhost_ = config.GetBool(L"NetworkControl", L"AllowLocalhost", true);

    // 读取允许的域名列表
    allowedDomains_ = config.GetStringList(L"NetworkControl", L"AllowedDomains");
    if (allowedDomains_.empty()) {
        LogWarn(L"[NetworkControlManager] No allowed domains configured, allowing all");
        allowedDomains_.push_back(L"*");
    }

    // 读取过滤模式
    std::wstring filterModeStr = config.GetString(L"NetworkControl", L"FilterMode", L"Whitelist");
    FilterMode filterMode = FilterMode::Whitelist;
    std::wstring filterModeLower = filterModeStr;
    std::transform(filterModeLower.begin(), filterModeLower.end(), filterModeLower.begin(), ::towlower);
    if (filterModeLower == L"blacklist") {
        filterMode = FilterMode::Blacklist;
    }

    LogInfo(L"[NetworkControlManager] Configuration loaded:");
    LogInfo(L"  Method: %s", GetMethodName(method_).c_str());
    LogInfo(L"  ProxyPort: %d", proxyPort_);
    LogInfo(L"  AllowLocalhost: %s", allowLocalhost_ ? L"true" : L"false");
    LogInfo(L"  FilterMode: %s", (filterMode == FilterMode::Whitelist) ? L"Whitelist" : L"Blacklist");
    LogInfo(L"  AllowedDomains: %llu domains", (unsigned long long)allowedDomains_.size());

    // 设置过滤模式
    NetworkFilterPlugin::SetFilterMode(filterMode);

    return true;
}

bool NetworkControlManager::Initialize(const std::wstring& processPath) {
    if (initialized_) {
        LogWarn(L"[NetworkControlManager] Already initialized");
        return true;
    }

    processPath_ = processPath;

    LogInfo(L"[NetworkControlManager] Initializing network control...");
    LogInfo(L"  Method: %s", GetMethodName(method_).c_str());
    LogInfo(L"  Process: %s", processPath.c_str());

    // 1. 启动代理服务器（所有方法都需要）
    if (!NetworkFilterPlugin::Initialize(proxyPort_)) {
        LogError(L"[NetworkControlManager] Failed to initialize proxy");
        return false;
    }

    // 2. 配置允许的域名
    NetworkFilterPlugin::SetAllowedDomains(allowedDomains_);

    // 3. 根据方法初始化强制控制
    bool success = false;

    switch (method_) {
    case NetworkControlMethod::None:
        LogWarn(L"[NetworkControlManager] Using None method - network control can be bypassed!");
        success = true;
        break;

    case NetworkControlMethod::WindowsFirewall:
        LogInfo(L"[NetworkControlManager] Initializing Windows Firewall control...");
        if (!FirewallControl::Initialize()) {
            LogError(L"[NetworkControlManager] Failed to initialize FirewallControl");
            NetworkFilterPlugin::Shutdown();
            return false;
        }

        if (!FirewallControl::BlockProcessExceptProxy(processPath, proxyPort_)) {
            LogError(L"[NetworkControlManager] Failed to create firewall rules");
            FirewallControl::Cleanup();
            NetworkFilterPlugin::Shutdown();
            return false;
        }

        LogInfo(L"[NetworkControlManager] Firewall rules created successfully");
        success = true;
        break;

    case NetworkControlMethod::WFP:
        LogInfo(L"[NetworkControlManager] Initializing WFP control...");
        if (!WFPNetworkFilter::Initialize()) {
            LogError(L"[NetworkControlManager] Failed to initialize WFPNetworkFilter");
            NetworkFilterPlugin::Shutdown();
            return false;
        }

        if (!WFPNetworkFilter::AddProcessFilter(processPath, proxyPort_, allowLocalhost_)) {
            LogError(L"[NetworkControlManager] Failed to add WFP filters");
            WFPNetworkFilter::Cleanup();
            NetworkFilterPlugin::Shutdown();
            return false;
        }

        LogInfo(L"[NetworkControlManager] WFP filters added successfully");
        success = true;
        break;

    default:
        LogError(L"[NetworkControlManager] Unknown network control method");
        NetworkFilterPlugin::Shutdown();
        return false;
    }

    if (success) {
        initialized_ = true;
        LogInfo(L"[NetworkControlManager] Network control initialized successfully");
        LogInfo(L"========================================");
        LogInfo(L"  Proxy URL: %s", GetProxyUrl().c_str());
        LogInfo(L"  Control Method: %s", GetMethodName(method_).c_str());
        LogInfo(L"  Process can only connect to proxy");
        LogInfo(L"========================================");
    }

    return success;
}

bool NetworkControlManager::InitializeGlobal() {
    if (initialized_) {
        LogWarn(L"[NetworkControlManager] Already initialized");
        return true;
    }

    globalMode_ = true;

    LogInfo(L"[NetworkControlManager] Initializing network control (GLOBAL MODE)...");
    LogInfo(L"  Method: %s", GetMethodName(method_).c_str());
    LogInfo(L"  Mode: All processes will be controlled");

    // 1. 启动代理服务器（所有方法都需要）
    if (!NetworkFilterPlugin::Initialize(proxyPort_)) {
        LogError(L"[NetworkControlManager] Failed to initialize proxy");
        return false;
    }

    // 2. 配置允许的域名
    NetworkFilterPlugin::SetAllowedDomains(allowedDomains_);

    // 3. 根据方法初始化强制控制（全局模式）
    bool success = false;

    switch (method_) {
    case NetworkControlMethod::None:
        LogWarn(L"[NetworkControlManager] Using None method - network control can be bypassed!");
        success = true;
        break;

    case NetworkControlMethod::WindowsFirewall:
        LogInfo(L"[NetworkControlManager] Initializing Windows Firewall control (GLOBAL)...");
        if (!FirewallControl::Initialize()) {
            LogError(L"[NetworkControlManager] Failed to initialize FirewallControl");
            NetworkFilterPlugin::Shutdown();
            return false;
        }

        if (!FirewallControl::BlockAllExceptProxy(proxyPort_)) {
            LogError(L"[NetworkControlManager] Failed to create global firewall rules");
            FirewallControl::Cleanup();
            NetworkFilterPlugin::Shutdown();
            return false;
        }

        LogInfo(L"[NetworkControlManager] Global firewall rules created successfully");
        success = true;
        break;

    case NetworkControlMethod::WFP:
        LogWarn(L"[NetworkControlManager] WFP does not support global mode, falling back to process-specific mode");
        LogInfo(L"[NetworkControlManager] Please use WindowsFirewall method for global control");
        NetworkFilterPlugin::Shutdown();
        return false;

    default:
        LogError(L"[NetworkControlManager] Unknown network control method");
        NetworkFilterPlugin::Shutdown();
        return false;
    }

    if (success) {
        initialized_ = true;
        LogInfo(L"[NetworkControlManager] Network control initialized successfully (GLOBAL MODE)");
        LogInfo(L"========================================");
        LogInfo(L"  Proxy URL: %s", GetProxyUrl().c_str());
        LogInfo(L"  Control Method: %s", GetMethodName(method_).c_str());
        LogInfo(L"  Mode: GLOBAL - All processes controlled");
        LogInfo(L"========================================");
    }

    return success;
}

void NetworkControlManager::Cleanup() {
    if (!initialized_) {
        return;
    }

    LogInfo(L"[NetworkControlManager] Cleaning up network control...");

    // 关闭代理
    NetworkFilterPlugin::Shutdown();

    // 清理强制控制
    switch (method_) {
    case NetworkControlMethod::WindowsFirewall:
        FirewallControl::Cleanup();
        LogInfo(L"[NetworkControlManager] Firewall rules removed");
        break;

    case NetworkControlMethod::WFP:
        WFPNetworkFilter::Cleanup();
        LogInfo(L"[NetworkControlManager] WFP filters removed");
        break;

    default:
        break;
    }

    initialized_ = false;
    LogInfo(L"[NetworkControlManager] Network control cleanup complete");
}

std::wstring NetworkControlManager::GetProxyUrl() const {
    return L"http://127.0.0.1:" + std::to_wstring(proxyPort_);
}

NetworkControlMethod NetworkControlManager::ParseMethod(const std::wstring& methodStr) {
    std::wstring lower = methodStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);

    if (lower == L"none") {
        return NetworkControlMethod::None;
    }
    else if (lower == L"firewall" || lower == L"windowsfirewall") {
        return NetworkControlMethod::WindowsFirewall;
    }
    else if (lower == L"wfp") {
        return NetworkControlMethod::WFP;
    }
    else {
        LogWarn(L"[NetworkControlManager] Unknown method '%s', using WindowsFirewall", methodStr.c_str());
        return NetworkControlMethod::WindowsFirewall;
    }
}

std::wstring NetworkControlManager::GetMethodName(NetworkControlMethod method) {
    switch (method) {
    case NetworkControlMethod::None:
        return L"None (Proxy Only)";
    case NetworkControlMethod::WindowsFirewall:
        return L"Windows Firewall";
    case NetworkControlMethod::WFP:
        return L"Windows Filtering Platform";
    default:
        return L"Unknown";
    }
}
