# 快速开始指南

## 🚀 5 分钟快速集成

### 步骤 1：添加文件到项目（2 分钟）

在 Visual Studio 中打开项目，添加以下文件：

```
LaunchProcessWithRestrictedToken/
├── ConfigReader.h
├── ConfigReader.cpp
├── NetworkControlManager.h
├── NetworkControlManager.cpp
├── FirewallControl.h
├── FirewallControl.cpp
├── WFPNetworkFilter.h
└── WFPNetworkFilter.cpp
```

### 步骤 2：添加库依赖（1 分钟）

项目属性 -> 链接器 -> 输入 -> 附加依赖项，添加：
```
Fwpuclnt.lib;ole32.lib;oleaut32.lib
```

### 步骤 3：编译（1 分钟）

```bash
# Visual Studio
按 Ctrl+Shift+B

# 或命令行
msbuild LaunchProcessWithRestrictedToken.sln /p:Configuration=Release /p:Platform=x64
```

### 步骤 4：配置（1 分钟）

复制 `config.ini` 到输出目录：
```bash
copy config.ini x64\Release\
```

编辑 `config.ini`：
```ini
[NetworkControl]
Method=WindowsFirewall
ProxyPort=8080
AllowLocalhost=true
AllowedDomains=*.example.com,*.github.com

[App]
networkFilterEnabled=true
```

### 步骤 5：运行测试（1 分钟）

以管理员身份运行：
```bash
cd x64\Release
LaunchProcessWithRestrictedToken.exe cmd.exe
```

在打开的命令提示符中测试：
```bash
# 测试允许的域名（应该成功）
curl http://www.example.com

# 测试不允许的域名（应该被阻止）
curl http://www.google.com
```

## ✅ 验证成功

如果看到以下日志，说明集成成功：

```
[NetworkControlManager] Configuration loaded:
  Method: Windows Firewall
  ProxyPort: 8080
  AllowLocalhost: true
  AllowedDomains: 2 domains

[NetworkControlManager] Network control initialized successfully
========================================
  Proxy URL: http://127.0.0.1:8080
  Control Method: Windows Firewall
  Process can only connect to proxy
========================================
```

## 🎯 三种使用场景

### 场景 1：开发测试（无强制控制）

```ini
[NetworkControl]
Method=None
AllowedDomains=*
```

### 场景 2：生产环境（推荐）

```ini
[NetworkControl]
Method=WindowsFirewall
AllowedDomains=*.company.com,api.partner.com
```

### 场景 3：高安全环境（最强）

```ini
[NetworkControl]
Method=WFP
AllowLocalhost=false
AllowedDomains=api.company.com
```

## 📖 详细文档

- **IMPLEMENTATION_SUMMARY.md** - 完整实现总结
- **PROJECT_INTEGRATION.md** - 详细集成步骤
- **NETWORK_CONTROL_UPGRADE.md** - 技术方案对比
- **BYPASS_TEST_GUIDE.md** - 安全测试指南

## 🆘 遇到问题？

### 编译失败
→ 检查是否添加了所有文件和库依赖

### 运行时访问被拒绝
→ 以管理员身份运行

### 网络仍可访问
→ 检查 config.ini 中 `networkFilterEnabled=true`

### 端口被占用
→ 修改 `ProxyPort` 为其他端口

## 🎉 完成！

现在你的沙箱程序已经具有强大的网络控制能力，无法被绕过！
