// 网络控制集成示例
// 展示如何使用 FirewallControl 或 WFPNetworkFilter 来强制网络控制

#include "NetworkFilterPlugin.h"
#include "FirewallControl.h"
#include "WFPNetworkFilter.h"

// 方案选择
enum class NetworkControlMethod {
    None,              // 无强制控制（当前方式，可被绕过）
    WindowsFirewall,   // Windows 防火墙（推荐，简单有效）
    WFP               // Windows Filtering Platform（最强大）
};

// 使用 Windows Firewall 的示例
bool SetupNetworkControl_Firewall(const std::wstring& processPath, int proxyPort) {
    LogInfo(L"[NetworkControl] Using Windows Firewall method");

    // 1. 初始化防火墙控制
    if (!FirewallControl::Initialize()) {
        LogError(L"[NetworkControl] Failed to initialize FirewallControl");
        return false;
    }

    // 2. 启动代理服务器
    if (!NetworkFilterPlugin::Initialize(proxyPort)) {
        LogError(L"[NetworkControl] Failed to initialize proxy");
        FirewallControl::Cleanup();
        return false;
    }

    // 3. 配置允许的域名
    std::vector<std::wstring> allowedDomains = {
        L"*.example.com",
        L"api.trusted.com",
        L"*.github.com"
    };
    NetworkFilterPlugin::SetAllowedDomains(allowedDomains);

    // 4. 创建防火墙规则，强制进程只能连接到代理
    if (!FirewallControl::BlockProcessExceptProxy(processPath, proxyPort)) {
        LogError(L"[NetworkControl] Failed to create firewall rules");
        NetworkFilterPlugin::Shutdown();
        FirewallControl::Cleanup();
        return false;
    }

    LogInfo(L"[NetworkControl] Firewall-based network control established");
    LogInfo(L"[NetworkControl] Process can only connect to 127.0.0.1:%d", proxyPort);
    return true;
}

// 使用 WFP 的示例
bool SetupNetworkControl_WFP(const std::wstring& processPath, int proxyPort) {
    LogInfo(L"[NetworkControl] Using WFP method");

    // 1. 初始化 WFP
    if (!WFPNetworkFilter::Initialize()) {
        LogError(L"[NetworkControl] Failed to initialize WFPNetworkFilter");
        return false;
    }

    // 2. 启动代理服务器
    if (!NetworkFilterPlugin::Initialize(proxyPort)) {
        LogError(L"[NetworkControl] Failed to initialize proxy");
        WFPNetworkFilter::Cleanup();
        return false;
    }

    // 3. 配置允许的域名
    std::vector<std::wstring> allowedDomains = {
        L"*.example.com",
        L"api.trusted.com",
        L"*.github.com"
    };
    NetworkFilterPlugin::SetAllowedDomains(allowedDomains);

    // 4. 添加 WFP 过滤规则
    // allowLocalhost=true: 允许进程间本地通信
    if (!WFPNetworkFilter::AddProcessFilter(processPath, proxyPort, true)) {
        LogError(L"[NetworkControl] Failed to add WFP filters");
        NetworkFilterPlugin::Shutdown();
        WFPNetworkFilter::Cleanup();
        return false;
    }

    LogInfo(L"[NetworkControl] WFP-based network control established");
    LogInfo(L"[NetworkControl] All outbound connections are filtered at kernel level");
    return true;
}

// 清理网络控制
void CleanupNetworkControl(NetworkControlMethod method) {
    NetworkFilterPlugin::Shutdown();

    switch (method) {
    case NetworkControlMethod::WindowsFirewall:
        FirewallControl::Cleanup();
        LogInfo(L"[NetworkControl] Firewall rules removed");
        break;

    case NetworkControlMethod::WFP:
        WFPNetworkFilter::Cleanup();
        LogInfo(L"[NetworkControl] WFP filters removed");
        break;

    default:
        break;
    }
}

// 完整的使用示例
int Example_LaunchProcessWithNetworkControl() {
    // 选择网络控制方法
    NetworkControlMethod method = NetworkControlMethod::WindowsFirewall;
    // NetworkControlMethod method = NetworkControlMethod::WFP; // 或使用 WFP

    const wchar_t* targetExe = L"C:\\Windows\\System32\\cmd.exe";
    int proxyPort = 8080;

    // 1. 设置网络控制
    bool success = false;
    if (method == NetworkControlMethod::WindowsFirewall) {
        success = SetupNetworkControl_Firewall(targetExe, proxyPort);
    }
    else if (method == NetworkControlMethod::WFP) {
        success = SetupNetworkControl_WFP(targetExe, proxyPort);
    }

    if (!success) {
        LogError(L"Failed to setup network control");
        return 1;
    }

    // 2. 启动受限进程
    // ... 这里是你原有的进程启动代码 ...
    // CreateProcessAsUser / CreateProcessWithTokenW 等

    LogInfo(L"Process launched with enforced network control");
    LogInfo(L"The process CANNOT bypass the proxy - all direct connections are blocked");

    // 3. 等待进程结束
    // WaitForSingleObject(processHandle, INFINITE);

    // 4. 清理
    CleanupNetworkControl(method);

    return 0;
}

/*
 * 关键优势说明：
 *
 * 1. Windows Firewall 方法：
 *    - 在系统防火墙层面阻止进程的所有出站连接
 *    - 只允许连接到 127.0.0.1:8080（代理）
 *    - 即使程序使用原生 socket API 也无法绕过
 *    - 不需要驱动程序，纯用户态实现
 *
 * 2. WFP 方法：
 *    - 在内核网络栈层面拦截连接
 *    - 比防火墙更底层，更难绕过
 *    - 可以基于进程 ID 动态过滤（支持子进程）
 *    - 支持更细粒度的控制
 *
 * 3. 与当前实现的对比：
 *    当前：只是一个代理服务器，程序可以选择不用
 *    改进后：系统级强制，程序无法绕过
 *
 * 4. 安全性：
 *    - 程序无法直接连接外网
 *    - 所有流量必须经过代理
 *    - 代理根据域名白名单过滤
 *    - 即使程序尝试使用 socket/connect 等底层 API 也会被阻止
 */
