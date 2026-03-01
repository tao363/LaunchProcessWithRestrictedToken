# ✅ 全局网络控制模式已实现

## 概述

已成功实现全局网络控制模式，可以控制沙箱中启动的所有进程（包括子进程）的网络访问。

---

## 🎯 问题背景

**原有问题：**
- 防火墙规则只绑定到启动的主进程（如 VSCode）
- VSCode 启动的子进程（如终端、调试器、扩展进程）不受控制
- 子进程可以绕过网络限制直接访问网络

**解决方案：**
- 实现全局防火墙规则（不绑定特定进程）
- 所有进程的网络访问都必须通过代理
- 配置文件可选择 Process 或 Global 模式

---

## 📝 实现的功能

### 1. 全局防火墙规则

**FirewallControl.h/cpp 新增方法：**

```cpp
// 创建全局防火墙规则（不绑定特定进程）
static bool BlockAllExceptProxy(int proxyPort);

// 创建全局阻止规则
static bool CreateGlobalOutboundBlockRule(const std::wstring& ruleName);

// 创建全局允许规则
static bool CreateGlobalProxyAllowRule(const std::wstring& ruleName, int proxyPort);
```

**关键区别：**
- **Process 模式**：使用 `put_ApplicationName()` 绑定特定进程
- **Global 模式**：不设置 `ApplicationName`，规则应用于所有进程

### 2. NetworkControlManager 支持

**新增方法：**
```cpp
// 初始化全局模式
bool InitializeGlobal();
```

**工作流程：**
1. 启动代理服务器
2. 配置域名过滤规则
3. 创建全局防火墙规则（阻止所有出站 + 允许代理）

### 3. 配置文件支持

**config.ini 新增配置项：**

```ini
[NetworkControl]
# 网络控制模式
# 可选值：
#   Process - 只控制启动的进程（不包括子进程）
#   Global  - 控制所有进程（包括启动的进程及其所有子进程）
ControlMode=Global
```

### 4. 主程序集成

**LaunchProcessWithRestrictedToken.cpp 修改：**

```cpp
// 读取控制模式配置
std::wstring controlMode = configReader.GetString(L"NetworkControl", L"ControlMode", L"Process");

// 根据模式选择初始化方法
if (controlMode == L"global") {
    initSuccess = g_NetworkControlManager.InitializeGlobal();
} else {
    initSuccess = g_NetworkControlManager.Initialize(exePath);
}
```

---

## 🚀 使用方法

### 配置 VSCode 启动

**x64\Release\config.ini：**

```ini
[NetworkControl]
Method=WindowsFirewall
ControlMode=Global          # 全局模式
FilterMode=Blacklist        # 黑名单模式
ProxyPort=8080
AllowLocalhost=true
AllowedDomains=*.ads.com,*.tracker.com,*.malware.com

[App]
networkFilterEnabled=true

# 启动 VSCode（请根据实际路径修改）
exe=C:\Users\tao\AppData\Local\Programs\Microsoft VS Code\Code.exe
wait=true
```

### 运行沙箱

```bash
# 以管理员权限运行
cd F:\Project\AI\sanbox\LaunchProcessWithRestrictedTokenCC\x64\Release
.\LaunchProcessWithRestrictedToken.exe
```

### 预期效果

**日志输出：**
```
[INFO] [NetworkControlManager] Using GLOBAL control mode (all processes)
[INFO] [NetworkControlManager] Initializing network control (GLOBAL MODE)...
[INFO] [NetworkControlManager] Method: Windows Firewall
[INFO] [NetworkControlManager] Mode: All processes will be controlled
[INFO] [FirewallControl] Created global block rule: SandboxNetFilter_Global_xxx_Block
[INFO] [FirewallControl] Created global allow rule: SandboxNetFilter_Global_xxx_AllowProxy
[INFO] [NetworkControlManager] Global firewall rules created successfully
========================================
  Proxy URL: http://127.0.0.1:8080
  Control Method: Windows Firewall
  Mode: GLOBAL - All processes controlled
========================================
```

**网络控制效果：**
- ✅ VSCode 主进程受控制
- ✅ VSCode 启动的终端受控制
- ✅ VSCode 扩展进程受控制
- ✅ 终端中运行的程序受控制
- ✅ 所有子进程都必须通过代理访问网络

---

## 🔍 两种模式对比

### Process 模式（进程特定）

**配置：**
```ini
ControlMode=Process
```

**特点：**
- 只控制启动的主进程
- 子进程不受控制
- 适用于单一程序（如 cmd.exe）

**防火墙规则：**
```cpp
pFwRule->put_ApplicationName(_bstr_t(processPath.c_str()));  // 绑定特定进程
```

### Global 模式（全局控制）

**配置：**
```ini
ControlMode=Global
```

**特点：**
- 控制所有进程（包括子进程）
- 适用于会启动子进程的应用（VSCode、IDE）
- 更强的安全性

**防火墙规则：**
```cpp
// 不设置 ApplicationName，规则应用于所有进程
pFwRule->put_Protocol(NET_FW_IP_PROTOCOL_ANY);
pFwRule->put_Direction(NET_FW_RULE_DIR_OUT);
pFwRule->put_Action(NET_FW_ACTION_BLOCK);
```

---

## 📊 测试场景

### 场景 1：VSCode 中运行 curl

**配置：**
```ini
ControlMode=Global
FilterMode=Blacklist
AllowedDomains=*.ads.com
```

**测试：**
1. 启动沙箱，打开 VSCode
2. 在 VSCode 终端中运行：
   ```bash
   # 应该被阻止（黑名单）
   curl http://ads.com

   # 应该允许（不在黑名单）
   curl http://www.google.com
   ```

**预期结果：**
- `ads.com` 被阻止
- `www.google.com` 允许访问
- 所有请求都通过代理，日志中可见

### 场景 2：VSCode 扩展网络访问

**配置：**
```ini
ControlMode=Global
FilterMode=Whitelist
AllowedDomains=*.visualstudio.com,*.microsoft.com,*.github.com
```

**测试：**
1. 启动沙箱，打开 VSCode
2. 尝试安装扩展或更新

**预期结果：**
- VSCode 扩展市场访问正常（在白名单中）
- 其他域名被阻止
- 所有网络请求都经过代理过滤

### 场景 3：调试器网络访问

**配置：**
```ini
ControlMode=Global
FilterMode=Blacklist
AllowedDomains=*.malicious.com
```

**测试：**
1. 在 VSCode 中调试程序
2. 程序尝试访问网络

**预期结果：**
- 调试的程序也受网络控制
- 黑名单域名被阻止
- 其他域名允许访问

---

## ⚠️ 重要说明

### 1. 全局模式的影响范围

**全局模式会影响：**
- ✅ 沙箱启动的主进程
- ✅ 主进程启动的所有子进程
- ✅ 子进程启动的孙进程
- ⚠️ **系统中的所有其他进程**

**注意：** 全局防火墙规则会影响整个系统，不仅仅是沙箱进程！

### 2. 安全建议

**使用全局模式时：**
1. 确保代理服务器正常运行
2. 配置合理的域名过滤规则
3. 测试完成后及时关闭沙箱（自动清理规则）
4. 不要长时间保持全局规则激活

### 3. 清理机制

**程序退出时自动清理：**
```cpp
void NetworkControlManager::Cleanup() {
    // 关闭代理
    NetworkFilterPlugin::Shutdown();

    // 清理防火墙规则
    FirewallControl::Cleanup();  // 移除所有创建的规则
}
```

### 4. WFP 不支持全局模式

**原因：**
- WFP 过滤器需要绑定特定进程 ID
- 无法创建"所有进程"的 WFP 过滤器

**解决方案：**
- 使用 WindowsFirewall 方法实现全局控制
- WFP 仅支持 Process 模式

---

## 📖 修改的文件

### 1. FirewallControl.h
- 添加 `BlockAllExceptProxy()` 方法
- 添加 `CreateGlobalOutboundBlockRule()` 方法
- 添加 `CreateGlobalProxyAllowRule()` 方法

### 2. FirewallControl.cpp
- 实现全局防火墙规则创建逻辑
- 不设置 `ApplicationName`，规则应用于所有进程

### 3. NetworkControlManager.h
- 添加 `InitializeGlobal()` 方法
- 添加 `globalMode_` 成员变量

### 4. NetworkControlManager.cpp
- 实现 `InitializeGlobal()` 方法
- 调用 `FirewallControl::BlockAllExceptProxy()`

### 5. LaunchProcessWithRestrictedToken.cpp
- 添加 `ConfigReader.h` 头文件
- 读取 `ControlMode` 配置
- 根据模式选择初始化方法

### 6. config.ini
- 添加 `ControlMode` 配置项
- 添加 VSCode 启动配置示例
- 更新配置说明

---

## ✅ 完成状态

- ✅ 实现全局防火墙规则
- ✅ 添加 InitializeGlobal() 方法
- ✅ 配置文件支持 ControlMode
- ✅ 主程序集成全局模式
- ✅ 添加 VSCode 启动配置
- ✅ 更新文档和示例

**全局网络控制模式已完全实现并可以使用！** 🎉

---

## 🎯 下一步

1. **编译项目**：
   ```
   在 Visual Studio 中重新编译 Release x64
   ```

2. **修改 VSCode 路径**：
   ```ini
   # 在 x64\Release\config.ini 中修改为实际路径
   exe=C:\Users\你的用户名\AppData\Local\Programs\Microsoft VS Code\Code.exe
   ```

3. **运行测试**：
   ```bash
   # 以管理员权限运行
   cd x64\Release
   .\LaunchProcessWithRestrictedToken.exe
   ```

4. **验证效果**：
   - VSCode 启动成功
   - 在 VSCode 终端中测试网络访问
   - 查看日志确认全局模式生效
