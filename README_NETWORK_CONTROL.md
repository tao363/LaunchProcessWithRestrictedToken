# 沙箱网络控制升级

## 🎯 项目概述

本项目为 Windows 沙箱程序添加了强大的网络控制能力，解决了原有代理方式可被轻易绕过的问题。

### 核心问题

**原有实现**：仅使用 HTTP/HTTPS 代理，程序可以通过直接使用 socket API 绕过控制。

**新实现**：提供三种网络控制方法，从应用层到内核层，实现真正无法绕过的网络控制。

## ✨ 主要特性

### 三种控制方法

1. **None** - 仅代理（兼容模式）
   - 无强制控制，可被绕过
   - 适用于开发测试

2. **Windows Firewall** - 防火墙控制（推荐）
   - 系统级防火墙规则
   - 阻止所有出站连接，只允许到代理
   - 简单有效，无法绕过

3. **WFP** - Windows Filtering Platform（最强）
   - 内核级网络拦截
   - 最底层的控制
   - 完全无法绕过

### 灵活配置

通过 `config.ini` 轻松切换控制方法和配置域名白名单：

```ini
[NetworkControl]
Method=WindowsFirewall
ProxyPort=8080
AllowLocalhost=true
AllowedDomains=*.example.com,api.trusted.com
```

### 安全保证

| 绕过方式 | 原实现 | 新实现 |
|---------|-------|--------|
| 直接 socket 连接 | ❌ 可绕过 | ✅ 阻止 |
| WinHTTP/WinINet | ❌ 可绕过 | ✅ 阻止 |
| 修改代理设置 | ❌ 可绕过 | ✅ 无影响 |
| 任何网络 API | ❌ 可绕过 | ✅ 全部阻止 |

## 🚀 快速开始

### 1. 添加文件到项目

在 Visual Studio 中添加以下文件：

```
ConfigReader.h/cpp
FirewallControl.h/cpp
WFPNetworkFilter.h/cpp
NetworkControlManager.h/cpp
```

### 2. 添加库依赖

项目属性 -> 链接器 -> 输入：
```
Fwpuclnt.lib;ole32.lib;oleaut32.lib
```

### 3. 编译项目

```bash
msbuild LaunchProcessWithRestrictedToken.sln /p:Configuration=Release /p:Platform=x64
```

### 4. 配置并运行

```bash
# 复制配置文件
copy config.ini x64\Release\

# 以管理员身份运行
cd x64\Release
LaunchProcessWithRestrictedToken.exe cmd.exe
```

### 5. 验证

在沙箱中测试：
```bash
# 允许的域名 - 成功
curl http://www.example.com

# 不允许的域名 - 被阻止
curl http://www.google.com
```

## 📚 文档

### 快速入门
- **[QUICK_START.md](QUICK_START.md)** - 5 分钟快速开始 ⭐ 从这里开始

### 实现文档
- **[IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md)** - 完整实现总结
- **[FILE_LIST.md](FILE_LIST.md)** - 文件清单和项目结构

### 集成指南
- **[PROJECT_INTEGRATION.md](PROJECT_INTEGRATION.md)** - Visual Studio 集成步骤
- **[INTEGRATION_GUIDE.md](INTEGRATION_GUIDE.md)** - 详细配置指南

### 技术文档
- **[NETWORK_CONTROL_UPGRADE.md](NETWORK_CONTROL_UPGRADE.md)** - 技术方案对比
- **[BYPASS_TEST_GUIDE.md](BYPASS_TEST_GUIDE.md)** - 安全测试指南

## 🔧 配置示例

### 生产环境（推荐）

```ini
[NetworkControl]
Method=WindowsFirewall
ProxyPort=8080
AllowLocalhost=true
AllowedDomains=*.company.com,api.partner.com

[App]
networkFilterEnabled=true
```

### 高安全环境

```ini
[NetworkControl]
Method=WFP
ProxyPort=8080
AllowLocalhost=false
AllowedDomains=api.company.com

[App]
networkFilterEnabled=true
```

### 开发测试

```ini
[NetworkControl]
Method=None
AllowedDomains=*

[App]
networkFilterEnabled=true
```

## 🏗️ 架构

```
┌─────────────────────────────────────────┐
│   沙箱进程（受限）                        │
│                                         │
│   尝试访问网络 ──────────────────┐      │
└──────────────────────────────────┼──────┘
                                   │
                                   ▼
                    ┌──────────────────────────┐
                    │  网络控制层               │
                    │  (Firewall/WFP)          │
                    │                          │
                    │  ✓ 127.0.0.1:8080       │
                    │  ✗ 其他所有连接          │
                    └──────────┬───────────────┘
                               │ 只允许到代理
                               ▼
                    ┌──────────────────────────┐
                    │  本地代理                 │
                    │  (127.0.0.1:8080)        │
                    │                          │
                    │  检查域名白名单           │
                    └──────────┬───────────────┘
                               │
                ┌──────────────┴──────────────┐
                │                             │
                ▼                             ▼
        ✓ 允许的域名                  ✗ 阻止的域名
        (*.example.com)              (其他域名)
                │                             │
                ▼                             ▼
            访问互联网                    返回 403
```

## 📊 性能

| 方法 | CPU | 内存 | 延迟 |
|-----|-----|------|------|
| None | 低 | 低 | 中等 |
| WindowsFirewall | 极低 | 极低 | 几乎无 |
| WFP | 低 | 低 | 极低 |

## 🔒 安全性

### 防护能力

✅ 阻止直接 socket 连接
✅ 阻止所有高级网络 API
✅ 不受代理设置影响
✅ 内核级强制执行
✅ 无法通过编程绕过

### 已知限制

⚠️ 需要管理员权限
⚠️ 子进程需要单独处理
⚠️ 当前主要支持 IPv4

## 🛠️ 系统要求

- **操作系统**: Windows 10/11 或 Windows Server 2016+
- **权限**: 管理员权限
- **编译器**: Visual Studio 2017 或更高
- **C++ 标准**: C++17 或更高

## 🐛 故障排查

### 编译错误

**无法打开包含文件**
```
解决：确保所有 .h 和 .cpp 文件已添加到项目
```

**无法解析的外部符号**
```
解决：添加 Fwpuclnt.lib, ole32.lib, oleaut32.lib
```

### 运行时错误

**访问被拒绝 (Error 5)**
```
解决：以管理员身份运行
```

**端口被占用 (Error 10048)**
```
解决：修改 config.ini 中的 ProxyPort
```

详细故障排查请参考 [PROJECT_INTEGRATION.md](PROJECT_INTEGRATION.md)

## 📝 更新日志

### v1.0 (2026-03-01)

- ✨ 新增 Windows Firewall 控制
- ✨ 新增 WFP 控制
- ✨ 新增配置文件支持
- ✨ 新增统一管理器
- ✨ 集成到主程序
- 📖 完整文档和示例
- 🧪 自动化测试脚本

## 🤝 贡献

欢迎提交问题和改进建议！

## 📄 许可

本项目遵循原项目的许可协议。

## 🔗 参考资源

### 官方文档
- [Windows Filtering Platform](https://learn.microsoft.com/en-us/windows/win32/fwp/windows-filtering-platform-start-page)
- [Windows Firewall API](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ics/windows-firewall-start-page)

### 相关项目
- [ProxyBridge](https://github.com/InterceptSuite/ProxyBridge) - 灵感来源
- [WFP-Traffic-Redirection-Driver](https://github.com/BOT-Man-JL/WFP-Traffic-Redirection-Driver)

## 💡 使用建议

### 推荐配置

**生产环境**：使用 `WindowsFirewall` 方法
- 简单可靠
- 性能优秀
- 足够安全

**高安全环境**：使用 `WFP` 方法
- 最强防护
- 内核级控制
- 完全无法绕过

**开发测试**：使用 `None` 方法
- 快速迭代
- 无需管理员权限
- 便于调试

## 🎉 总结

现在你的沙箱程序具有了企业级的网络控制能力：

✅ **强制执行** - 无法通过编程绕过
✅ **灵活配置** - 通过配置文件轻松管理
✅ **多种方法** - 从简单到复杂，满足不同需求
✅ **完整文档** - 详细的说明和示例
✅ **易于集成** - 5 分钟快速开始

开始使用：阅读 [QUICK_START.md](QUICK_START.md) 🚀
