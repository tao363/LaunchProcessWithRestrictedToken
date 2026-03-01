# 🔧 手动修复链接错误指南

## 问题诊断

你遇到的 "无法解析的外部符号" 错误说明：
- ✅ 头文件正确（否则会有编译错误）
- ❌ `NetworkControlManager.cpp` 没有被编译到项目中

## 解决方案：手动在 Visual Studio 中添加文件

### 方法 1：通过 Visual Studio 界面添加（推荐）

#### 步骤 1：打开项目
1. 打开 Visual Studio
2. 打开 `LaunchProcessWithRestrictedToken.sln`

#### 步骤 2：检查现有文件
在解决方案资源管理器中，展开项目，查看是否有以下文件：
- ConfigReader.cpp
- FirewallControl.cpp
- NetworkControlManager.cpp ⚠️ 重点检查这个
- WFPNetworkFilter.cpp

#### 步骤 3：如果文件缺失，手动添加

**添加源文件：**
1. 右键项目 `LaunchProcessWithRestrictedToken`
2. 添加 -> 现有项
3. 浏览到 `LaunchProcessWithRestrictedToken` 文件夹
4. 选择以下文件（按住 Ctrl 多选）：
   ```
   ConfigReader.cpp
   FirewallControl.cpp
   NetworkControlManager.cpp
   WFPNetworkFilter.cpp
   ```
5. 点击"添加"

**添加头文件：**
1. 右键项目 -> 添加 -> 现有项
2. 选择以下文件：
   ```
   ConfigReader.h
   FirewallControl.h
   NetworkControlManager.h
   WFPNetworkFilter.h
   ```
3. 点击"添加"

#### 步骤 4：添加库依赖

1. 右键项目 -> 属性
2. 配置属性 -> 链接器 -> 输入
3. 附加依赖项，在最前面添加：
   ```
   Fwpuclnt.lib;ole32.lib;oleaut32.lib;
   ```
4. 确保选择"所有配置"和"所有平台"
5. 应用 -> 确定

#### 步骤 5：重新编译

1. 生成 -> 清理解决方案
2. 生成 -> 重新生成解决方案

---

### 方法 2：卸载并重新加载项目

如果方法 1 不起作用：

1. 在解决方案资源管理器中，右键项目
2. 卸载项目
3. 右键项目（显示为不可用）
4. 重新加载项目
5. 重新编译

---

### 方法 3：手动编辑项目文件

如果 Visual Studio 没有识别自动修改：

1. 关闭 Visual Studio
2. 用记事本打开 `LaunchProcessWithRestrictedToken\LaunchProcessWithRestrictedToken.vcxproj`
3. 找到 `<ItemGroup>` 部分
4. 确认包含以下内容：

```xml
<ItemGroup>
  <ClCompile Include="ConfigReader.cpp" />
  <ClCompile Include="FirewallControl.cpp" />
  <ClCompile Include="LaunchProcessWithRestrictedToken.cpp" />
  <ClCompile Include="NetworkControlManager.cpp" />
  <ClCompile Include="NetworkFilterPlugin.cpp" />
  <ClCompile Include="WFPNetworkFilter.cpp" />
</ItemGroup>
<ItemGroup>
  <ClInclude Include="ConfigReader.h" />
  <ClInclude Include="FirewallControl.h" />
  <ClInclude Include="NetworkControlManager.h" />
  <ClInclude Include="NetworkFilterPlugin.h" />
  <ClInclude Include="WFPNetworkFilter.h" />
</ItemGroup>
```

5. 找到所有 `<Link>` 部分，确认包含：

```xml
<Link>
  <SubSystem>Console</SubSystem>
  <GenerateDebugInformation>true</GenerateDebugInformation>
  <AdditionalDependencies>Fwpuclnt.lib;ole32.lib;oleaut32.lib;%(AdditionalDependencies)</AdditionalDependencies>
</Link>
```

6. 保存文件
7. 重新打开 Visual Studio
8. 打开解决方案
9. 重新编译

---

## 验证步骤

### 1. 检查文件是否在项目中

在解决方案资源管理器中，应该看到：

```
LaunchProcessWithRestrictedToken
├── 头文件
│   ├── ConfigReader.h
│   ├── FirewallControl.h
│   ├── NetworkControlManager.h
│   ├── NetworkFilterPlugin.h
│   └── WFPNetworkFilter.h
└── 源文件
    ├── ConfigReader.cpp
    ├── FirewallControl.cpp
    ├── LaunchProcessWithRestrictedToken.cpp
    ├── NetworkControlManager.cpp
    ├── NetworkFilterPlugin.cpp
    └── WFPNetworkFilter.cpp
```

### 2. 检查编译输出

编译时，输出窗口应该显示：

```
1>------ 已启动生成: 项目: LaunchProcessWithRestrictedToken ------
1>ConfigReader.cpp
1>FirewallControl.cpp
1>NetworkControlManager.cpp          ⚠️ 必须看到这一行
1>WFPNetworkFilter.cpp
1>NetworkFilterPlugin.cpp
1>LaunchProcessWithRestrictedToken.cpp
```

**如果没有看到 `NetworkControlManager.cpp`，说明文件没有被添加到项目中！**

### 3. 检查库依赖

项目属性 -> 链接器 -> 输入 -> 附加依赖项，应该包含：
```
Fwpuclnt.lib;ole32.lib;oleaut32.lib
```

---

## 常见问题

### Q1: 我添加了文件，但编译时没有看到它被编译

**A:** 文件可能被排除在生成之外

1. 在解决方案资源管理器中找到该文件
2. 右键文件 -> 属性
3. 配置属性 -> 常规
4. 确认"从生成中排除"设置为"否"

### Q2: 编译输出显示文件被编译，但仍有链接错误

**A:** 可能是编译配置不匹配

1. 确认所有文件使用相同的配置（Debug/Release, x86/x64）
2. 清理解决方案
3. 删除 `.vs` 文件夹和 `x64` 文件夹
4. 重新生成

### Q3: 仍然提示编码错误

**A:** 在 Visual Studio 中：

1. 打开 `NetworkControlManager.cpp`
2. 文件 -> 高级保存选项
3. 编码：Unicode (UTF-8 带签名) - 代码页 65001
4. 保存
5. 对所有新文件重复此操作

---

## 最后的手段：使用命令行编译

如果 Visual Studio 仍然有问题，可以尝试命令行编译：

```bash
# 打开 x64 Native Tools Command Prompt for VS 2022
cd F:\Project\AI\sanbox\LaunchProcessWithRestrictedTokenCC

# 清理
msbuild LaunchProcessWithRestrictedToken.sln /t:Clean /p:Configuration=Release /p:Platform=x64

# 重新生成
msbuild LaunchProcessWithRestrictedToken.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64
```

---

## 成功的标志

编译成功后，应该看到：

```
========== 生成: 1 成功，0 失败，0 最新，0 跳过 ==========
```

并且生成了：
```
x64\Release\LaunchProcessWithRestrictedToken.exe
```

---

## 需要帮助？

如果按照以上所有步骤仍然无法编译，请提供：

1. Visual Studio 版本
2. 完整的编译输出（包括哪些文件被编译）
3. 解决方案资源管理器的截图
4. 项目属性 -> 链接器 -> 输入的截图

---

**提示：** 最常见的问题是文件没有真正添加到项目中。请仔细检查解决方案资源管理器中是否能看到所有新文件。
