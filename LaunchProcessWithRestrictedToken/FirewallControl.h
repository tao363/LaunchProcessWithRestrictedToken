#pragma once

#include <Windows.h>
#include <netfw.h>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

// Windows Firewall 控制类
// 通过防火墙规则强制所有流量通过代理
class FirewallControl {
public:
    // 初始化防火墙控制
    static bool Initialize();

    // 清理防火墙规则
    static void Cleanup();

    // 为指定进程创建防火墙规则，阻止所有出站连接（除了到代理的连接）
    static bool BlockProcessExceptProxy(const std::wstring& processPath, int proxyPort);

    // 创建全局防火墙规则（不绑定特定进程，控制所有进程）
    static bool BlockAllExceptProxy(int proxyPort);

    // 移除进程的防火墙规则
    static bool RemoveProcessRules(const std::wstring& processPath);

private:
    static INetFwPolicy2* g_pNetFwPolicy2;
    static std::vector<std::wstring> g_createdRuleNames;

    static bool CreateOutboundBlockRule(const std::wstring& ruleName,
                                       const std::wstring& processPath);
    static bool CreateGlobalOutboundBlockRule(const std::wstring& ruleName);
    static bool CreateProxyAllowRule(const std::wstring& ruleName,
                                    const std::wstring& processPath,
                                    int proxyPort);
    static bool CreateGlobalProxyAllowRule(const std::wstring& ruleName,
                                          int proxyPort);
};
