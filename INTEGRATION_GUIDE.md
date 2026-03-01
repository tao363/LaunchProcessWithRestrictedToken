# 集成指南 - 升级网络控制能力

## 快速开始

### 步骤 1：添加新文件到项目

在 Visual Studio 中，将以下文件添加到项目：

**方案 1：Windows Firewall（推荐）**
- `FirewallControl.h`
- `FirewallControl.cpp`

**方案 2：WFP（高级）**
- `WFPNetworkFilter.h`
- `WFPNetworkFilter.cpp`

**集成示例**
- `NetworkControlIntegration.cpp`

### 步骤 2：修改项目设置

在项目属性中添加以下库：

```
配置属性 -> 链接器 -> 输入 -> 附加依赖项
```

添加：
- `Fwpuclnt.lib` （如果使用 WFP）
- `ole32.lib` （如果使用 Firewall）
- `oleaut32.lib` （如果使用 Firewall）

### 步骤 3：修改主程序

在 `LaunchProcessWithRestrictedToken.cpp` 中集成网络控制：

```cpp
#include "FirewallControl.h"  // 或 "WFPNetworkFilter.h"
#include "NetworkFilterPlugin.h"

int wmain(int argc, wchar_t* argv[]) {
    // ... 原有的初始化代码 ...

    // === 新增：网络控制初始化 ===
    const int PROXY_PORT = 8080;

    // 方案 1：使用 Windows Firewall
    if (!FirewallControl::Initialize()) {
        LogError(L"Failed to initialize firewall control");
        return 1;
    }

    // 启动代理
    if (!NetworkFilterPlugin::Initialize(PROXY_PORT)) {
        LogError(L"Failed to start proxy");
        FirewallControl::Cleanup();
        return 1;
    }

    // 配置允许的域名
    std::vector<std::wstring> allowedDomains = {
        L"*.example.com",
        L"api.trusted.com"
    };
    NetworkFilterPlugin::SetAllowedDomains(allowedDomains);

    // 获取目标程序路径（需要完整路径）
    wchar_t fullPath[MAX_PATH];
    GetFullPathNameW(targetExePath, MAX_PATH, fullPath, nullptr);

    // 创建防火墙规则
    if (!FirewallControl::BlockProcessExceptProxy(fullPath, PROXY_PORT)) {
        LogError(L"Failed to create firewall rules");
        NetworkFilterPlugin::Shutdown();
        FirewallControl::Cleanup();
        return 1;
    }

    LogInfo(L"Network control established - process can only access proxy");

    // ... 原有的进程启动代码 ...
    // CreateProcessAsUser / CreateProcessWithTokenW 等

    // ... 等待进程结束 ...

    // === 新增：清理 ===
    NetworkFilterPlugin::Shutdown();
    FirewallControl::Cleanup();

    return 0;
}
```

### 步骤 4：编译和测试

```bash
# 在 Visual Studio 中编译
# 或使用命令行：
msbuild LaunchProcessWithRestrictedToken.sln /p:Configuration=Release

# 以管理员权限运行
.\x64\Release\LaunchProcessWithRestrictedToken.exe "C:\Windows\System32\cmd.exe"
```

---

## 详细集成示例

### 示例 1：最小集成（仅防火墙）

```cpp
#include "FirewallControl.h"
#include "NetworkFilterPlugin.h"

bool SetupNetworkControl(const wchar_t* exePath) {
    // 1. 初始化防火墙
    if (!FirewallControl::Initialize()) {
        return false;
    }

    // 2. 启动代理
    if (!NetworkFilterPlugin::Initialize(8080)) {
        FirewallControl::Cleanup();
        return false;
    }

    // 3. 设置域名白名单
    std::vector<std::wstring> domains = { L"*.allowed.com" };
    NetworkFilterPlugin::SetAllowedDomains(domains);

    // 4. 创建防火墙规则
    if (!FirewallControl::BlockProcessExceptProxy(exePath, 8080)) {
        NetworkFilterPlugin::Shutdown();
        FirewallControl::Cleanup();
        return false;
    }

    return true;
}

void CleanupNetworkControl() {
    NetworkFilterPlugin::Shutdown();
    FirewallControl::Cleanup();
}
```

### 示例 2：使用 WFP（更强控制）

```cpp
#include "WFPNetworkFilter.h"
#include "NetworkFilterPlugin.h"

bool SetupNetworkControl_WFP(const wchar_t* exePath) {
    // 1. 初始化 WFP
    if (!WFPNetworkFilter::Initialize()) {
        return false;
    }

    // 2. 启动代理
    if (!NetworkFilterPlugin::Initialize(8080)) {
        WFPNetworkFilter::Cleanup();
        return false;
    }

    // 3. 设置域名白名单
    std::vector<std::wstring> domains = { L"*.allowed.com" };
    NetworkFilterPlugin::SetAllowedDomains(domains);

    // 4. 添加 WFP 过滤规则
    // allowLocalhost=true: 允许本地进程间通信
    if (!WFPNetworkFilter::AddProcessFilter(exePath, 8080, true)) {
        NetworkFilterPlugin::Shutdown();
        WFPNetworkFilter::Cleanup();
        return false;
    }

    return true;
}

void CleanupNetworkControl_WFP() {
    NetworkFilterPlugin::Shutdown();
    WFPNetworkFilter::Cleanup();
}
```

### 示例 3：完整集成（带错误处理）

```cpp
#include "FirewallControl.h"
#include "NetworkFilterPlugin.h"
#include <memory>

class NetworkControlGuard {
public:
    NetworkControlGuard() : initialized_(false) {}

    ~NetworkControlGuard() {
        Cleanup();
    }

    bool Initialize(const wchar_t* exePath, int proxyPort,
                   const std::vector<std::wstring>& allowedDomains) {
        // 1. 初始化防火墙
        if (!FirewallControl::Initialize()) {
            LogError(L"FirewallControl::Initialize failed");
            return false;
        }

        // 2. 启动代理
        if (!NetworkFilterPlugin::Initialize(proxyPort)) {
            LogError(L"NetworkFilterPlugin::Initialize failed");
            FirewallControl::Cleanup();
            return false;
        }

        // 3. 配置域名
        NetworkFilterPlugin::SetAllowedDomains(allowedDomains);

        // 4. 创建防火墙规则
        if (!FirewallControl::BlockProcessExceptProxy(exePath, proxyPort)) {
            LogError(L"FirewallControl::BlockProcessExceptProxy failed");
            NetworkFilterPlugin::Shutdown();
            FirewallControl::Cleanup();
            return false;
        }

        initialized_ = true;
        LogInfo(L"Network control initialized successfully");
        return true;
    }

    void Cleanup() {
        if (initialized_) {
            NetworkFilterPlugin::Shutdown();
            FirewallControl::Cleanup();
            initialized_ = false;
            LogInfo(L"Network control cleaned up");
        }
    }

private:
    bool initialized_;
};

// 使用示例
int main() {
    NetworkControlGuard guard;

    std::vector<std::wstring> domains = {
        L"*.example.com",
        L"api.trusted.com"
    };

    if (!guard.Initialize(L"C:\\test\\app.exe", 8080, domains)) {
        return 1;
    }

    // ... 启动和运行进程 ...

    // guard 析构时自动清理
    return 0;
}
```

---

## 配置选项

### 代理端口选择

```cpp
// 默认端口
const int DEFAULT_PROXY_PORT = 8080;

// 如果 8080 被占用，可以使用其他端口
const int ALTERNATIVE_PORT = 8888;

// 动态选择可用端口
int FindAvailablePort() {
    for (int port = 8080; port < 9000; port++) {
        // 尝试绑定端口
        // ...
    }
    return -1;
}
```

### 域名白名单配置

```cpp
// 从配置文件读取
std::vector<std::wstring> LoadAllowedDomains(const wchar_t* configFile) {
    std::vector<std::wstring> domains;

    // 读取配置文件
    // 每行一个域名
    // 支持通配符：*.example.com

    return domains;
}

// 示例配置
std::vector<std::wstring> domains = {
    L"*",                    // 允许所有（不推荐）
    L"*.company.com",        // 公司域名
    L"api.service.com",      // 特定 API
    L"*.cdn.net",            // CDN
    L"localhost",            // 本地
    L"127.0.0.1"            // 本地 IP
};
```

---

## 故障排查

### 问题 1：防火墙规则创建失败

**错误**：`FwpmProviderAdd0 failed: 5 (Access Denied)`

**解决**：
- 确保以管理员权限运行
- 检查 Windows Firewall 服务是否启动

```bash
# 检查服务状态
sc query mpssvc

# 启动服务
net start mpssvc
```

### 问题 2：代理无法启动

**错误**：`bind() failed on port 8080: 10048`

**解决**：
- 端口已被占用，更换端口
- 检查是否有其他代理程序运行

```bash
# 查看端口占用
netstat -ano | findstr :8080

# 结束占用进程
taskkill /PID <pid> /F
```

### 问题 3：进程仍能访问外网

**可能原因**：
1. 防火墙规则未生效
2. 进程路径不匹配
3. 子进程未被限制

**检查**：
```bash
# 查看防火墙规则
netsh advfirewall firewall show rule name=all | findstr "Sandbox"

# 查看 WFP 过滤器
netsh wfp show filters
```

### 问题 4：合法连接也被阻止

**检查**：
1. 域名是否在白名单中
2. 代理日志查看拒绝原因
3. 防火墙规则是否正确

---

## 性能优化

### 1. 减少日志输出

```cpp
// 在生产环境中减少详细日志
#ifdef _DEBUG
    #define LOG_VERBOSE
#endif
```

### 2. 优化域名匹配

```cpp
// 使用哈希表加速精确匹配
std::unordered_set<std::wstring> exactDomains;
std::vector<std::wstring> wildcardDomains;

// 分类存储
for (const auto& domain : allowedDomains) {
    if (domain.find(L'*') != std::wstring::npos) {
        wildcardDomains.push_back(domain);
    } else {
        exactDomains.insert(domain);
    }
}
```

### 3. 连接池

```cpp
// 复用到目标服务器的连接
// 减少频繁建立连接的开销
```

---

## 安全建议

1. **最小权限原则**：只允许必需的域名
2. **定期审计**：记录所有网络访问日志
3. **监控异常**：检测频繁的连接失败
4. **更新白名单**：根据实际需求调整
5. **测试验证**：使用 BYPASS_TEST_GUIDE.md 中的测试方法验证

---

## 下一步

1. ✓ 集成 FirewallControl 或 WFPNetworkFilter
2. ✓ 配置域名白名单
3. ✓ 测试绕过防护
4. ☐ 添加子进程监控（可选）
5. ☐ 添加日志分析工具（可选）
6. ☐ 实现配置文件支持（可选）

---

## 参考文档

- `NETWORK_CONTROL_UPGRADE.md` - 方案对比和技术细节
- `BYPASS_TEST_GUIDE.md` - 绕过测试和验证方法
- `NetworkControlIntegration.cpp` - 完整集成示例
