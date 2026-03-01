# 网络控制绕过测试与防护验证

## 测试场景

### 场景 1：当前实现（可被绕过）

**测试代码**（绕过代理的恶意程序）：
```cpp
// bypass_test.cpp - 演示如何绕过简单的代理控制
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>

#pragma comment(lib, "ws2_32.lib")

int main() {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // 直接连接到外部服务器，完全忽略代理设置
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &addr.sin_addr);  // Google DNS

    // 当前实现：这个连接会成功！
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) == 0) {
        printf("SUCCESS: Bypassed proxy control!\n");
        printf("Direct connection to 8.8.8.8:80 established\n");

        // 发送 HTTP 请求
        const char* request = "GET / HTTP/1.0\r\n\r\n";
        send(sock, request, strlen(request), 0);

        char buffer[1024];
        int received = recv(sock, buffer, sizeof(buffer), 0);
        printf("Received %d bytes from server\n", received);
    } else {
        printf("BLOCKED: Connection failed\n");
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
```

**结果**：✗ 绕过成功，可以直接连接外网

---

### 场景 2：使用 Windows Firewall（防护成功）

**配置**：
```cpp
// 启动时配置
FirewallControl::Initialize();
NetworkFilterPlugin::Initialize(8080);
FirewallControl::BlockProcessExceptProxy(L"C:\\test\\bypass_test.exe", 8080);
```

**测试同样的绕过代码**：
```cpp
// 运行 bypass_test.exe
```

**结果**：✓ 连接被阻止
```
BLOCKED: Connection failed
Error: WSAEACCES (10013) - Permission denied
```

**原因**：防火墙规则在系统层面阻止了该进程的所有出站连接（除了到 127.0.0.1:8080）

---

### 场景 3：使用 WFP（防护成功，更强）

**配置**：
```cpp
WFPNetworkFilter::Initialize();
NetworkFilterPlugin::Initialize(8080);
WFPNetworkFilter::AddProcessFilter(L"C:\\test\\bypass_test.exe", 8080, true);
```

**测试同样的绕过代码**：

**结果**：✓ 连接被阻止
```
BLOCKED: Connection failed
Error: WSAEACCES (10013) - Permission denied
```

**原因**：WFP 在内核网络栈层面拦截，比防火墙更底层

---

## 各种绕过尝试测试

### 尝试 1：使用不同的 API

```cpp
// 尝试使用 WinHTTP（高级 API）
HINTERNET hSession = WinHttpOpen(L"Test",
    WINHTTP_ACCESS_TYPE_NO_PROXY,  // 明确不使用代理
    WINHTTP_NO_PROXY_NAME,
    WINHTTP_NO_PROXY_BYPASS, 0);

HINTERNET hConnect = WinHttpConnect(hSession, L"www.google.com", 80, 0);
HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/",
    NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);

// 当前实现：成功 ✗
// Firewall/WFP：失败 ✓（底层 socket 被阻止）
BOOL result = WinHttpSendRequest(hRequest, ...);
```

### 尝试 2：使用 URLDownloadToFile

```cpp
// 尝试使用 COM API 下载文件
#include <urlmon.h>
#pragma comment(lib, "urlmon.lib")

HRESULT hr = URLDownloadToFile(NULL,
    L"http://www.google.com/index.html",
    L"C:\\temp\\downloaded.html", 0, NULL);

// 当前实现：成功 ✗
// Firewall/WFP：失败 ✓
```

### 尝试 3：启动子进程

```cpp
// 启动 curl.exe 下载文件
system("curl.exe http://www.google.com -o output.txt");

// 当前实现：成功 ✗（子进程不受限制）
// Firewall：需要为 curl.exe 单独添加规则
// WFP：需要为 curl.exe 单独添加规则
```

**注意**：子进程需要单独处理，可以通过 Job Object 或监控进程创建来自动添加规则

### 尝试 4：修改代理设置

```cpp
// 尝试修改 IE 代理设置
INTERNET_PER_CONN_OPTION_LIST list;
// ... 设置代理为空或其他地址

InternetSetOption(NULL, INTERNET_OPTION_PER_CONNECTION_OPTION, &list, ...);

// 当前实现：可能影响某些程序 ✗
// Firewall/WFP：无影响 ✓（不依赖代理设置）
```

### 尝试 5：使用原始套接字

```cpp
// 尝试使用原始套接字（需要管理员权限）
SOCKET rawSock = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);

// 当前实现：如果有权限则成功 ✗
// Firewall/WFP：被阻止 ✓
```

---

## 防护效果对比表

| 绕过方法 | 当前实现 | Windows Firewall | WFP |
|---------|---------|-----------------|-----|
| 直接 socket 连接 | ✗ 可绕过 | ✓ 阻止 | ✓ 阻止 |
| WinHTTP API | ✗ 可绕过 | ✓ 阻止 | ✓ 阻止 |
| WinINet API | ✗ 可绕过 | ✓ 阻止 | ✓ 阻止 |
| URLDownloadToFile | ✗ 可绕过 | ✓ 阻止 | ✓ 阻止 |
| 修改代理设置 | ✗ 可绕过 | ✓ 无影响 | ✓ 无影响 |
| 原始套接字 | ✗ 可绕过 | ✓ 阻止 | ✓ 阻止 |
| 子进程 | ✗ 可绕过 | △ 需额外配置 | △ 需额外配置 |
| UDP 流量 | ✗ 可绕过 | ✓ 可配置阻止 | ✓ 可配置阻止 |
| ICMP ping | ✗ 可绕过 | ✓ 可配置阻止 | ✓ 可配置阻止 |

---

## 实际测试步骤

### 1. 编译测试程序

```bash
# 编译绕过测试程序
cl bypass_test.cpp /Fe:bypass_test.exe ws2_32.lib

# 编译主沙箱程序（包含新的网络控制）
# 在 Visual Studio 中添加以下文件到项目：
# - FirewallControl.cpp/h
# - WFPNetworkFilter.cpp/h
# - NetworkControlIntegration.cpp
```

### 2. 测试当前实现

```bash
# 运行沙箱，启动 bypass_test.exe
LaunchProcessWithRestrictedToken.exe bypass_test.exe

# 观察：bypass_test.exe 可以直接连接外网 ✗
```

### 3. 测试 Firewall 防护

```cpp
// 修改主程序，使用 FirewallControl
NetworkControlMethod method = NetworkControlMethod::WindowsFirewall;
```

```bash
# 重新编译并运行
LaunchProcessWithRestrictedToken.exe bypass_test.exe

# 观察：bypass_test.exe 连接被阻止 ✓
# 日志显示：WSAEACCES (10013)
```

### 4. 测试 WFP 防护

```cpp
// 修改主程序，使用 WFP
NetworkControlMethod method = NetworkControlMethod::WFP;
```

```bash
# 重新编译并运行
LaunchProcessWithRestrictedToken.exe bypass_test.exe

# 观察：bypass_test.exe 连接被阻止 ✓
# 更底层的拦截
```

### 5. 验证代理功能正常

```bash
# 测试通过代理访问允许的域名
# 在沙箱中运行：
curl --proxy http://127.0.0.1:8080 http://allowed-domain.com

# 应该成功 ✓

# 测试访问不允许的域名
curl --proxy http://127.0.0.1:8080 http://blocked-domain.com

# 应该被代理拒绝（403 Forbidden）✓
```

---

## 性能影响

| 方法 | CPU 开销 | 内存开销 | 延迟影响 |
|-----|---------|---------|---------|
| 当前实现 | 低 | 低 | 中等（代理转发） |
| Windows Firewall | 极低 | 极低 | 几乎无 |
| WFP | 低 | 低 | 极低 |

---

## 推荐配置

### 生产环境推荐

```cpp
// 使用 Windows Firewall（最佳平衡）
NetworkControlMethod method = NetworkControlMethod::WindowsFirewall;

// 配置
int proxyPort = 8080;
std::vector<std::wstring> allowedDomains = {
    L"*.company.com",      // 公司域名
    L"api.trusted.com",    // 可信 API
    L"cdn.example.com"     // CDN
};
```

### 高安全环境推荐

```cpp
// 使用 WFP（最强防护）
NetworkControlMethod method = NetworkControlMethod::WFP;

// 更严格的配置
bool allowLocalhost = false;  // 禁止本地连接（除了代理）
```

---

## 已知限制

1. **子进程**：需要监控进程创建并自动添加规则
2. **管理员权限**：两种方法都需要管理员权限
3. **驱动签名**：WFP 驱动方式需要签名（用户态 API 不需要）
4. **IPv6**：当前示例只处理 IPv4，需要添加 IPv6 支持

---

## 总结

✓ **Windows Firewall 方法**：简单、有效、推荐使用
✓ **WFP 方法**：更强大、更底层、适合高安全需求
✗ **当前实现**：容易被绕过，不推荐继续使用
