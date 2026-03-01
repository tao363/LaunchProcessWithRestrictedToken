#pragma once

#include <Windows.h>
#include <fwpmu.h>
#include <string>
#include <vector>

#pragma comment(lib, "Fwpuclnt.lib")

// Windows Filtering Platform 网络过滤器
// 使用 WFP 用户态 API 实现强制网络控制
class WFPNetworkFilter {
public:
    // 初始化 WFP 引擎
    static bool Initialize();

    // 清理 WFP 过滤器
    static void Cleanup();

    // 为指定进程添加网络过滤规则
    // processPath: 进程可执行文件完整路径
    // proxyPort: 允许连接的本地代理端口
    // allowLocalhost: 是否允许其他本地连接
    static bool AddProcessFilter(const std::wstring& processPath,
                                 int proxyPort,
                                 bool allowLocalhost = true);

    // 移除进程的过滤规则
    static bool RemoveProcessFilter(const std::wstring& processPath);

    // 检查是否已初始化
    static bool IsInitialized() { return g_engineHandle != nullptr; }

private:
    static HANDLE g_engineHandle;
    static GUID g_providerKey;
    static GUID g_sublayerKey;
    static std::vector<UINT64> g_filterIds;

    // 创建 WFP Provider
    static bool CreateProvider();

    // 创建 WFP Sublayer
    static bool CreateSublayer();

    // 添加阻止规则（阻止所有出站连接）
    static bool AddBlockFilter(const std::wstring& processPath, UINT64& filterId);

    // 添加允许规则（允许连接到代理）
    static bool AddAllowProxyFilter(const std::wstring& processPath,
                                    int proxyPort,
                                    UINT64& filterId);

    // 添加允许本地连接规则
    static bool AddAllowLocalhostFilter(const std::wstring& processPath,
                                       UINT64& filterId);

    // 将进程路径转换为 FWP_BYTE_BLOB
    static bool GetAppIdFromPath(const std::wstring& processPath,
                                 FWP_BYTE_BLOB** appId);

    // 释放 AppId
    static void FreeAppId(FWP_BYTE_BLOB* appId);
};
