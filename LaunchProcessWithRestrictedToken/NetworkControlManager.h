#pragma once

#include <Windows.h>
#include <string>
#include <vector>
#include "ConfigReader.h"

// 网络控制方法
enum class NetworkControlMethod {
    None,              // 无强制控制（仅代理，可被绕过）
    WindowsFirewall,   // Windows 防火墙（推荐）
    WFP                // Windows Filtering Platform（最强）
};

// 网络控制管理器
// 统一管理不同的网络控制方案
class NetworkControlManager {
public:
    NetworkControlManager();
    ~NetworkControlManager();

    // 从配置文件加载设置
    bool LoadConfig(const std::wstring& configPath);

    // 初始化网络控制
    bool Initialize(const std::wstring& processPath);

    // 初始化网络控制（全局模式，控制所有进程）
    bool InitializeGlobal();

    // 清理网络控制
    void Cleanup();

    // 获取当前使用的方法
    NetworkControlMethod GetMethod() const { return method_; }

    // 获取代理端口
    int GetProxyPort() const { return proxyPort_; }

    // 获取代理 URL
    std::wstring GetProxyUrl() const;

    // 检查是否已初始化
    bool IsInitialized() const { return initialized_; }

private:
    NetworkControlMethod method_;
    int proxyPort_;
    std::vector<std::wstring> allowedDomains_;
    bool allowLocalhost_;
    bool initialized_;
    std::wstring processPath_;
    bool globalMode_;  // 是否为全局模式（控制所有进程）

    // 解析网络控制方法
    static NetworkControlMethod ParseMethod(const std::wstring& methodStr);

    // 获取方法名称
    static std::wstring GetMethodName(NetworkControlMethod method);
};
