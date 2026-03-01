# 项目集成步骤

## 1. 添加新文件到 Visual Studio 项目

### 方法 A：使用 Visual Studio IDE

1. 打开 `LaunchProcessWithRestrictedToken.sln`
2. 在解决方案资源管理器中，右键点击 `LaunchProcessWithRestrictedToken` 项目
3. 选择 "添加" -> "现有项"
4. 添加以下文件：

**核心实现文件：**
- `ConfigReader.h`
- `ConfigReader.cpp`
- `NetworkControlManager.h`
- `NetworkControlManager.cpp`
- `FirewallControl.h`
- `FirewallControl.cpp`
- `WFPNetworkFilter.h`
- `WFPNetworkFilter.cpp`

5. 确保 `config.ini` 在项目根目录

### 方法 B：手动编辑项目文件

编辑 `LaunchProcessWithRestrictedToken.vcxproj`，在 `<ItemGroup>` 中添加：

```xml
<ItemGroup>
  <ClCompile Include="ConfigReader.cpp" />
  <ClCompile Include="NetworkControlManager.cpp" />
  <ClCompile Include="FirewallControl.cpp" />
  <ClCompile Include="WFPNetworkFilter.cpp" />
  <ClCompile Include="LaunchProcessWithRestrictedToken.cpp" />
  <ClCompile Include="NetworkFilterPlugin.cpp" />
</ItemGroup>
<ItemGroup>
  <ClInclude Include="ConfigReader.h" />
  <ClInclude Include="NetworkControlManager.h" />
  <ClInclude Include="FirewallControl.h" />
  <ClInclude Include="WFPNetworkFilter.h" />
  <ClInclude Include="NetworkFilterPlugin.h" />
</ItemGroup>
```

## 2. 配置项目属性

### 添加库依赖

1. 右键项目 -> 属性
2. 配置属性 -> 链接器 -> 输入 -> 附加依赖项
3. 添加以下库（如果尚未添加）：

```
Fwpuclnt.lib
ole32.lib
oleaut32.lib
ws2_32.lib
Userenv.lib
Advapi32.lib
```

### 设置 C++ 标准

1. 配置属性 -> C/C++ -> 语言
2. C++ 语言标准：ISO C++17 标准 (/std:c++17) 或更高

## 3. 编译项目

### 使用 Visual Studio

1. 选择配置：Release 或 Debug
2. 选择平台：x64
3. 生成 -> 生成解决方案 (Ctrl+Shift+B)

### 使用命令行

```bash
# 使用 MSBuild
msbuild LaunchProcessWithRestrictedToken.sln /p:Configuration=Release /p:Platform=x64

# 或使用项目自带的 build.bat
build.bat
```

## 4. 配置 config.ini

将 `config.ini` 复制到可执行文件所在目录：

```bash
# 复制到输出目录
copy config.ini x64\Release\
copy config.ini x64\Debug\
```

或者在项目属性中设置自动复制：

1. 右键 `config.ini` -> 属性
2. 配置属性 -> 常规
3. 项目中排除：否
4. 内容：是
5. 复制到输出目录：如果较新则复制

## 5. 测试运行

### 以管理员权限运行

**重要**：网络控制功能需要管理员权限！

```bash
# 方法 1：右键 -> 以管理员身份运行
LaunchProcessWithRestrictedToken.exe

# 方法 2：使用命令提示符（管理员）
cd x64\Release
LaunchProcessWithRestrictedToken.exe cmd.exe
```

### 验证配置

1. 检查日志输出，确认网络控制方法：
   ```
   [NetworkControlManager] Configuration loaded:
     Method: Windows Firewall
     ProxyPort: 8080
     AllowLocalhost: true
     AllowedDomains: 4 domains
   ```

2. 测试网络访问：
   ```bash
   # 在沙箱中运行
   curl http://allowed-domain.com    # 应该成功
   curl http://blocked-domain.com    # 应该被阻止
   ```

## 6. 故障排查

### 编译错误

**错误：无法打开包含文件**
```
fatal error C1083: 无法打开包含文件: "ConfigReader.h"
```
**解决**：确保所有 .h 和 .cpp 文件都已添加到项目中

**错误：无法解析的外部符号**
```
error LNK2019: 无法解析的外部符号 FwpmEngineOpen0
```
**解决**：添加 `Fwpuclnt.lib` 到链接器依赖项

### 运行时错误

**错误：访问被拒绝**
```
[FirewallControl] FwpmEngineOpen0 failed: 5 (Access Denied)
```
**解决**：以管理员权限运行程序

**错误：配置文件未找到**
```
[ConfigReader] Config file not found: config.ini
```
**解决**：将 config.ini 复制到可执行文件所在目录

**错误：端口被占用**
```
[NetworkFilterPlugin] bind() failed on port 8080: 10048
```
**解决**：修改 config.ini 中的 ProxyPort 为其他端口

## 7. 部署

### 发布版本

1. 编译 Release 版本
2. 复制以下文件到部署目录：
   ```
   LaunchProcessWithRestrictedToken.exe
   config.ini
   ```

3. 确保目标机器满足要求：
   - Windows 10/11 或 Windows Server 2016+
   - 管理员权限
   - .NET Framework 或 VC++ Runtime（如果需要）

### 配置文件模板

为不同环境准备配置模板：

```
config.ini.production    # 生产环境（严格控制）
config.ini.development   # 开发环境（宽松控制）
config.ini.testing       # 测试环境（中等控制）
```

## 8. 验证集成成功

运行以下检查清单：

- [ ] 项目编译无错误
- [ ] 程序以管理员权限启动
- [ ] 日志显示网络控制方法已加载
- [ ] 防火墙规则已创建（检查 Windows 防火墙）
- [ ] 代理服务器已启动（检查端口监听）
- [ ] 允许的域名可以访问
- [ ] 不允许的域名被阻止
- [ ] 程序退出后防火墙规则被清理

## 9. 下一步

- 阅读 `NETWORK_CONTROL_UPGRADE.md` 了解技术细节
- 阅读 `BYPASS_TEST_GUIDE.md` 进行安全测试
- 根据需求调整 `config.ini` 配置
- 考虑添加日志分析工具
- 考虑添加子进程监控功能

## 10. 常见问题

**Q: 可以在非管理员模式下运行吗？**
A: 不可以。防火墙和 WFP 控制都需要管理员权限。如果不需要强制控制，可以设置 `Method=None`。

**Q: 如何临时禁用网络控制？**
A: 在 config.ini 中设置 `[App] networkFilterEnabled=false`

**Q: 支持 IPv6 吗？**
A: 当前版本主要支持 IPv4。IPv6 支持需要额外的过滤器规则。

**Q: 子进程会被控制吗？**
A: 防火墙方法基于进程路径，子进程需要单独添加规则。可以通过监控进程创建事件自动添加。

**Q: 性能影响如何？**
A: 防火墙和 WFP 的性能开销极小（< 1%）。代理转发会有一定延迟，但通常可以忽略。
