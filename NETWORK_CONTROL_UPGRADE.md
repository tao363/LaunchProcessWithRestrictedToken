# 网络控制能力升级方案

## 当前问题分析

当前实现使用简单的 HTTP/HTTPS 代理（127.0.0.1:8080），存在以下安全问题：

1. **应用层控制**：只是一个普通代理，程序可以选择不使用
2. **容易绕过**：
   - 程序可以直接使用 socket API 连接任何地址
   - 可以使用其他端口或协议
   - 可以通过编程方式忽略代理设置
3. **无强制性**：依赖应用程序主动配置代理

## 推荐升级方案

### 方案 1：Windows Firewall API（推荐，易实现）

**优点**：
- 实现简单，纯用户态代码
- 不需要驱动程序
- 系统级控制，难以绕过
- 稳定可靠

**原理**：
1. 为沙箱进程创建防火墙规则，阻止所有出站连接
2. 添加例外规则，只允许连接到本地代理（127.0.0.1:8080）
3. 进程只能通过代理访问网络

**实现**：已创建 `FirewallControl.cpp/h`

**使用方法**：
```cpp
// 初始化
FirewallControl::Initialize();

// 启动代理
NetworkFilterPlugin::Initialize(8080);

// 启动进程并获取可执行文件路径
std::wstring exePath = L"C:\\path\\to\\sandboxed.exe";

// 创建防火墙规则
FirewallControl::BlockProcessExceptProxy(exePath, 8080);

// 清理
FirewallControl::Cleanup();
```

**限制**：
- 需要管理员权限
- 基于进程路径匹配（子进程需要单独处理）

---

### 方案 2：Windows Filtering Platform (WFP)（最强大）

**优点**：
- 内核级拦截，完全无法绕过
- 可以基于进程 ID 过滤（支持动态进程）
- 支持所有协议（TCP/UDP/ICMP 等）
- 可以重定向连接（透明代理）

**原理**：
1. 注册 WFP Callout 驱动或使用用户态 WFP API
2. 在 CONNECT 层拦截所有出站连接
3. 检查目标地址，非代理地址则阻止或重定向

**实现复杂度**：中等（用户态 API）到高（内核驱动）

**参考资源**：
- [Using Bind or Connect Redirection](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/using-bind-or-connect-redirection)
- [Windows Filtering Platform API](https://learn.microsoft.com/en-us/windows/win32/fwp/windows-filtering-platform-start-page)
- [WFP Traffic Redirection Driver](https://github.com/BOT-Man-JL/WFP-Traffic-Redirection-Driver)

---

### 方案 3：LSP/Winsock Hooking（不推荐）

**优点**：
- 可以拦截所有 Winsock 调用
- 用户态实现

**缺点**：
- LSP 已被微软标记为过时
- 容易被检测和绕过
- 可能影响系统稳定性
- 不支持原生 socket API

---

### 方案 4：Job Object + Network Isolation（部分场景）

**优点**：
- 可以完全禁用网络访问
- 实现简单

**缺点**：
- 只能完全禁用，无法选择性允许
- 不适合需要网络访问的场景

---

## 推荐实施步骤

### 第一阶段：Windows Firewall（立即可用）

1. 集成 `FirewallControl` 到主程序
2. 在启动沙箱进程前创建防火墙规则
3. 测试验证无法绕过

### 第二阶段：WFP 用户态 API（增强版）

如果需要更强的控制，实现 WFP 用户态过滤：

```cpp
// WFP 过滤示例（伪代码）
FWPM_SESSION0 session = {0};
HANDLE engineHandle;

// 1. 打开 WFP 引擎
FwpmEngineOpen0(NULL, RPC_C_AUTHN_WINNT, NULL, &session, &engineHandle);

// 2. 添加过滤器
FWPM_FILTER0 filter = {0};
filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
filter.action.type = FWP_ACTION_BLOCK;

// 3. 添加条件：匹配进程 ID
FWPM_FILTER_CONDITION0 condition = {0};
condition.fieldKey = FWPM_CONDITION_ALE_APP_ID;
condition.matchType = FWP_MATCH_EQUAL;
// ... 设置进程路径

FwpmFilterAdd0(engineHandle, &filter, NULL, NULL);
```

### 第三阶段：WFP 驱动（终极方案）

如果需要透明重定向（类似 ProxyBridge），需要开发 WFP Callout 驱动。

---

## 对比 ProxyBridge

ProxyBridge 使用 WFP 实现透明代理重定向：

1. **拦截层**：FWPM_LAYER_ALE_CONNECT_REDIRECT_V4
2. **重定向**：将所有连接重定向到本地代理
3. **上下文保留**：使用 WFP 的 redirect context 保存原始目标
4. **代理处理**：代理读取 redirect context，连接到真实目标

**优势**：
- 应用程序完全无感知
- 支持所有使用 Winsock 的程序
- 内核级强制，无法绕过

**劣势**：
- 实现复杂
- 需要驱动签名（生产环境）
- 调试困难

---

## 建议

**立即实施**：方案 1（Windows Firewall）
- 实现简单，效果好
- 足以防止大部分绕过尝试
- 不需要驱动开发

**长期规划**：方案 2（WFP 用户态）
- 如果需要更细粒度控制
- 支持动态进程管理
- 更难绕过

**仅在必要时**：WFP 驱动
- 需要透明代理
- 需要支持不遵守代理设置的程序
- 有驱动开发和维护能力

---

## 参考资源

- [Windows Filtering Platform](https://learn.microsoft.com/en-us/windows/win32/fwp/windows-filtering-platform-start-page)
- [Using Bind or Connect Redirection](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/using-bind-or-connect-redirection)
- [WFP Traffic Redirection Driver](https://github.com/BOT-Man-JL/WFP-Traffic-Redirection-Driver)
- [windows-network-wfp-redirect](https://github.com/iamasbcx/windows-network-wfp-redirect)
- [Introduction to WFP](https://scorpiosoftware.net/2022/12/25/introduction-to-the-windows-filtering-platform/)
