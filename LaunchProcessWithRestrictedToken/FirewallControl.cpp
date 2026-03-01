#include "FirewallControl.h"
#include "LogFunctions.h"
#include <comdef.h>

INetFwPolicy2* FirewallControl::g_pNetFwPolicy2 = nullptr;
std::vector<std::wstring> FirewallControl::g_createdRuleNames;

bool FirewallControl::Initialize() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        LogError(L"[FirewallControl] CoInitializeEx failed: 0x%08X", hr);
        return false;
    }

    hr = CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(INetFwPolicy2), (void**)&g_pNetFwPolicy2);
    if (FAILED(hr)) {
        LogError(L"[FirewallControl] Failed to create NetFwPolicy2: 0x%08X", hr);
        return false;
    }

    LogInfo(L"[FirewallControl] Initialized successfully");
    return true;
}

void FirewallControl::Cleanup() {
    // 移除所有创建的规则
    if (g_pNetFwPolicy2) {
        INetFwRules* pFwRules = nullptr;
        HRESULT hr = g_pNetFwPolicy2->get_Rules(&pFwRules);
        if (SUCCEEDED(hr) && pFwRules) {
            for (const auto& ruleName : g_createdRuleNames) {
                pFwRules->Remove(_bstr_t(ruleName.c_str()));
                LogInfo(L"[FirewallControl] Removed rule: %s", ruleName.c_str());
            }
            pFwRules->Release();
        }
        g_pNetFwPolicy2->Release();
        g_pNetFwPolicy2 = nullptr;
    }

    g_createdRuleNames.clear();
    CoUninitialize();
    LogInfo(L"[FirewallControl] Cleanup complete");
}

bool FirewallControl::BlockProcessExceptProxy(const std::wstring& processPath, int proxyPort) {
    if (!g_pNetFwPolicy2) {
        LogError(L"[FirewallControl] Not initialized");
        return false;
    }

    // 规则名称（使用进程路径的哈希避免冲突）
    std::wstring baseRuleName = L"SandboxNetFilter_" + std::to_wstring(GetTickCount64());

    // 1. 创建阻止所有出站连接的规则
    std::wstring blockRuleName = baseRuleName + L"_Block";
    if (!CreateOutboundBlockRule(blockRuleName, processPath)) {
        return false;
    }

    // 2. 创建允许连接到代理的规则（优先级更高）
    std::wstring allowRuleName = baseRuleName + L"_AllowProxy";
    if (!CreateProxyAllowRule(allowRuleName, processPath, proxyPort)) {
        // 如果失败，移除阻止规则
        RemoveProcessRules(processPath);
        return false;
    }

    LogInfo(L"[FirewallControl] Created firewall rules for process: %s", processPath.c_str());
    return true;
}

bool FirewallControl::BlockAllExceptProxy(int proxyPort) {
    if (!g_pNetFwPolicy2) {
        LogError(L"[FirewallControl] Not initialized");
        return false;
    }

    // 规则名称
    std::wstring baseRuleName = L"SandboxNetFilter_Global_" + std::to_wstring(GetTickCount64());

    // 1. 创建全局阻止所有出站连接的规则（不绑定特定进程）
    std::wstring blockRuleName = baseRuleName + L"_Block";
    if (!CreateGlobalOutboundBlockRule(blockRuleName)) {
        return false;
    }

    // 2. 创建全局允许连接到代理的规则（优先级更高）
    std::wstring allowRuleName = baseRuleName + L"_AllowProxy";
    if (!CreateGlobalProxyAllowRule(allowRuleName, proxyPort)) {
        return false;
    }

    LogInfo(L"[FirewallControl] Created global firewall rules (all processes controlled)");
    return true;
}

bool FirewallControl::CreateOutboundBlockRule(const std::wstring& ruleName,
                                              const std::wstring& processPath) {
    INetFwRules* pFwRules = nullptr;
    HRESULT hr = g_pNetFwPolicy2->get_Rules(&pFwRules);
    if (FAILED(hr)) {
        LogError(L"[FirewallControl] Failed to get Rules: 0x%08X", hr);
        return false;
    }

    INetFwRule* pFwRule = nullptr;
    hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(INetFwRule), (void**)&pFwRule);
    if (FAILED(hr)) {
        pFwRules->Release();
        LogError(L"[FirewallControl] Failed to create rule: 0x%08X", hr);
        return false;
    }

    pFwRule->put_Name(_bstr_t(ruleName.c_str()));
    pFwRule->put_Description(_bstr_t(L"Block all outbound connections for sandboxed process"));
    pFwRule->put_ApplicationName(_bstr_t(processPath.c_str()));
    pFwRule->put_Protocol(NET_FW_IP_PROTOCOL_ANY);
    pFwRule->put_Direction(NET_FW_RULE_DIR_OUT);
    pFwRule->put_Action(NET_FW_ACTION_BLOCK);
    pFwRule->put_Enabled(VARIANT_TRUE);
    pFwRule->put_Profiles(NET_FW_PROFILE2_ALL);

    hr = pFwRules->Add(pFwRule);
    pFwRule->Release();
    pFwRules->Release();

    if (FAILED(hr)) {
        LogError(L"[FirewallControl] Failed to add block rule: 0x%08X", hr);
        return false;
    }

    g_createdRuleNames.push_back(ruleName);
    LogInfo(L"[FirewallControl] Created block rule: %s", ruleName.c_str());
    return true;
}

bool FirewallControl::CreateProxyAllowRule(const std::wstring& ruleName,
                                          const std::wstring& processPath,
                                          int proxyPort) {
    INetFwRules* pFwRules = nullptr;
    HRESULT hr = g_pNetFwPolicy2->get_Rules(&pFwRules);
    if (FAILED(hr)) {
        LogError(L"[FirewallControl] Failed to get Rules: 0x%08X", hr);
        return false;
    }

    INetFwRule* pFwRule = nullptr;
    hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(INetFwRule), (void**)&pFwRule);
    if (FAILED(hr)) {
        pFwRules->Release();
        LogError(L"[FirewallControl] Failed to create rule: 0x%08X", hr);
        return false;
    }

    pFwRule->put_Name(_bstr_t(ruleName.c_str()));
    pFwRule->put_Description(_bstr_t(L"Allow connection to local proxy"));
    pFwRule->put_ApplicationName(_bstr_t(processPath.c_str()));
    pFwRule->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
    pFwRule->put_RemoteAddresses(_bstr_t(L"127.0.0.1"));
    pFwRule->put_RemotePorts(_bstr_t(std::to_wstring(proxyPort).c_str()));
    pFwRule->put_Direction(NET_FW_RULE_DIR_OUT);
    pFwRule->put_Action(NET_FW_ACTION_ALLOW);
    pFwRule->put_Enabled(VARIANT_TRUE);
    pFwRule->put_Profiles(NET_FW_PROFILE2_ALL);

    hr = pFwRules->Add(pFwRule);
    pFwRule->Release();
    pFwRules->Release();

    if (FAILED(hr)) {
        LogError(L"[FirewallControl] Failed to add allow rule: 0x%08X", hr);
        return false;
    }

    g_createdRuleNames.push_back(ruleName);
    LogInfo(L"[FirewallControl] Created allow rule: %s", ruleName.c_str());
    return true;
}

bool FirewallControl::CreateGlobalOutboundBlockRule(const std::wstring& ruleName) {
    INetFwRules* pFwRules = nullptr;
    HRESULT hr = g_pNetFwPolicy2->get_Rules(&pFwRules);
    if (FAILED(hr)) {
        LogError(L"[FirewallControl] Failed to get Rules: 0x%08X", hr);
        return false;
    }

    INetFwRule* pFwRule = nullptr;
    hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(INetFwRule), (void**)&pFwRule);
    if (FAILED(hr)) {
        pFwRules->Release();
        LogError(L"[FirewallControl] Failed to create rule: 0x%08X", hr);
        return false;
    }

    pFwRule->put_Name(_bstr_t(ruleName.c_str()));
    pFwRule->put_Description(_bstr_t(L"Block all outbound connections (sandbox global rule)"));
    // 不设置 ApplicationName，规则应用于所有进程
    pFwRule->put_Protocol(NET_FW_IP_PROTOCOL_ANY);
    pFwRule->put_Direction(NET_FW_RULE_DIR_OUT);
    pFwRule->put_Action(NET_FW_ACTION_BLOCK);
    pFwRule->put_Enabled(VARIANT_TRUE);
    pFwRule->put_Profiles(NET_FW_PROFILE2_ALL);

    hr = pFwRules->Add(pFwRule);
    pFwRule->Release();
    pFwRules->Release();

    if (FAILED(hr)) {
        LogError(L"[FirewallControl] Failed to add global block rule: 0x%08X", hr);
        return false;
    }

    g_createdRuleNames.push_back(ruleName);
    LogInfo(L"[FirewallControl] Created global block rule: %s", ruleName.c_str());
    return true;
}

bool FirewallControl::CreateGlobalProxyAllowRule(const std::wstring& ruleName, int proxyPort) {
    INetFwRules* pFwRules = nullptr;
    HRESULT hr = g_pNetFwPolicy2->get_Rules(&pFwRules);
    if (FAILED(hr)) {
        LogError(L"[FirewallControl] Failed to get Rules: 0x%08X", hr);
        return false;
    }

    INetFwRule* pFwRule = nullptr;
    hr = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                         __uuidof(INetFwRule), (void**)&pFwRule);
    if (FAILED(hr)) {
        pFwRules->Release();
        LogError(L"[FirewallControl] Failed to create rule: 0x%08X", hr);
        return false;
    }

    pFwRule->put_Name(_bstr_t(ruleName.c_str()));
    pFwRule->put_Description(_bstr_t(L"Allow connection to local proxy (sandbox global rule)"));
    // 不设置 ApplicationName，规则应用于所有进程
    pFwRule->put_Protocol(NET_FW_IP_PROTOCOL_TCP);
    pFwRule->put_RemoteAddresses(_bstr_t(L"127.0.0.1"));
    pFwRule->put_RemotePorts(_bstr_t(std::to_wstring(proxyPort).c_str()));
    pFwRule->put_Direction(NET_FW_RULE_DIR_OUT);
    pFwRule->put_Action(NET_FW_ACTION_ALLOW);
    pFwRule->put_Enabled(VARIANT_TRUE);
    pFwRule->put_Profiles(NET_FW_PROFILE2_ALL);

    hr = pFwRules->Add(pFwRule);
    pFwRule->Release();
    pFwRules->Release();

    if (FAILED(hr)) {
        LogError(L"[FirewallControl] Failed to add global allow rule: 0x%08X", hr);
        return false;
    }

    g_createdRuleNames.push_back(ruleName);
    LogInfo(L"[FirewallControl] Created global allow rule: %s", ruleName.c_str());
    return true;
}

bool FirewallControl::RemoveProcessRules(const std::wstring& processPath) {
    // 在 Cleanup 中统一处理
    return true;
}
