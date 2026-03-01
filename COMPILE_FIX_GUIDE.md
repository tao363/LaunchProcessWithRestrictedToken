# 编译错误修复指南

## 问题诊断

你遇到的编译错误主要有以下几类：

1. **文件编码问题** - "该文件包含不能在当前代码页(936)中表示的字符"
2. **未声明的标识符** - "g_NetworkControlManager": 未声明的标识符
3. **语法错误** - 类定义相关的错误

## 已完成的修复

✅ **文件编码已转换** - 所有新文件已转换为 UTF-8 with BOM

## 需要手动检查的步骤

### 步骤 1：确认文件已添加到项目

在 Visual Studio 的解决方案资源管理器中，确认以下文件已添加：

```
LaunchProcessWithRestrictedToken 项目
├── 头文件
│   ├── ConfigReader.h
│   ├── FirewallControl.h
│   ├── WFPNetworkFilter.h
│   └── NetworkControlManager.h
└── 源文件
    ├── ConfigReader.cpp
    ├── FirewallControl.cpp
    ├── WFPNetworkFilter.cpp
    ├── NetworkControlManager.cpp
    └── LaunchProcessWithRestrictedToken.cpp
```

**如果文件未添加：**
1. 右键项目 -> 添加 -> 现有项
2. 选择所有 .h 和 .cpp 文件
3. 点击添加

### 步骤 2：检查文件编码

在 Visual Studio 中：

1. 打开每个新添加的文件
2. 文件 -> 高级保存选项
3. 确认编码为：**Unicode (UTF-8 带签名) - 代码页 65001**
4. 如果不是，选择该编码并保存

**需要检查的文件：**
- ConfigReader.h/cpp
- FirewallControl.h/cpp
- WFPNetworkFilter.h/cpp
- NetworkControlManager.h/cpp

### 步骤 3：添加库依赖

1. 右键项目 -> 属性
2. 配置属性 -> 链接器 -> 输入
3. 附加依赖项中添加：

```
Fwpuclnt.lib
ole32.lib
oleaut32.lib
```

4. 点击应用 -> 确定

### 步骤 4：清理并重新生成

1. 生成 -> 清理解决方案
2. 生成 -> 重新生成解决方案

## 如果仍有错误

### 错误 1：未声明的标识符

**错误信息：**
```
"g_NetworkControlManager": 未声明的标识符
```

**原因：** 头文件包含顺序或文件未正确添加

**解决方法：**

1. 确认 `LaunchProcessWithRestrictedToken.cpp` 顶部有：
```cpp
#include "NetworkFilterPlugin.h"
#include "NetworkControlManager.h"
```

2. 确认 `NetworkControlManager.h` 已添加到项目

3. 检查 `NetworkControlManager.h` 中类定义是否完整

### 错误 2：语法错误

**错误信息：**
```
语法错误:"public"
缺少类型说明符 - 假定为 int
```

**原因：** 文件编码问题或头文件损坏

**解决方法：**

1. 重新检查文件编码（UTF-8 with BOM）
2. 确认文件内容完整，没有乱码
3. 如果有乱码，重新从源文件复制内容

### 错误 3：重定义错误

**错误信息：**
```
"NetworkControlManager": 重定义；以前的定义是"函数"
```

**原因：** 可能有重复的定义或包含

**解决方法：**

1. 确认每个 .h 文件都有 `#pragma once`
2. 检查是否有重复包含
3. 清理解决方案后重新生成

## 完整的编译步骤

### 1. 准备工作

```bash
# 运行编码转换脚本（已完成）
# 所有文件已转换为 UTF-8 with BOM
```

### 2. 在 Visual Studio 中

**A. 添加文件**
- 右键项目 -> 添加 -> 现有项
- 选择所有 8 个文件（4 对 .h/.cpp）

**B. 配置项目**
- 项目属性 -> 链接器 -> 输入
- 添加：`Fwpuclnt.lib;ole32.lib;oleaut32.lib`

**C. 检查编码**
- 打开每个新文件
- 文件 -> 高级保存选项
- 确认：Unicode (UTF-8 带签名) - 代码页 65001

**D. 编译**
- 生成 -> 清理解决方案
- 生成 -> 重新生成解决方案

### 3. 预期结果

编译成功，输出：
```
========== 生成: 1 成功，0 失败，0 最新，0 跳过 ==========
```

## 常见问题排查

### Q1: 仍然提示编码错误

**A:** 手动转换文件编码：

1. 在 Visual Studio 中打开文件
2. 文件 -> 另存为
3. 保存按钮旁边的下拉箭头 -> 编码保存
4. 选择：Unicode (UTF-8 带签名) - 代码页 65001
5. 保存

### Q2: 找不到头文件

**A:** 检查文件路径：

1. 确认所有文件在同一目录：`LaunchProcessWithRestrictedToken/`
2. 在解决方案资源管理器中查看文件路径
3. 如果路径不对，移除文件后重新添加

### Q3: 链接错误

**A:** 检查库依赖：

```
无法解析的外部符号 FwpmEngineOpen0
-> 缺少 Fwpuclnt.lib

无法解析的外部符号 CoCreateInstance
-> 缺少 ole32.lib 和 oleaut32.lib
```

添加缺少的库到链接器依赖项。

### Q4: 编译成功但运行失败

**A:** 检查配置文件：

1. 复制 `config.ini` 到输出目录
2. 以管理员权限运行程序
3. 检查日志输出

## 验证编译成功

编译成功后，应该看到：

```
1>------ 已启动生成: 项目: LaunchProcessWithRestrictedToken, 配置: Release x64 ------
1>ConfigReader.cpp
1>FirewallControl.cpp
1>WFPNetworkFilter.cpp
1>NetworkControlManager.cpp
1>LaunchProcessWithRestrictedToken.cpp
1>正在生成代码
1>已完成代码的生成
1>LaunchProcessWithRestrictedToken.vcxproj -> F:\...\x64\Release\LaunchProcessWithRestrictedToken.exe
========== 生成: 1 成功，0 失败，0 最新，0 跳过 ==========
```

## 下一步

编译成功后：

1. 复制 `config.ini` 到 `x64\Release\`
2. 以管理员权限运行程序
3. 测试网络控制功能

## 需要帮助？

如果按照以上步骤仍然无法编译，请提供：

1. 完整的错误信息
2. Visual Studio 版本
3. 项目配置（Debug/Release, x86/x64）
4. 文件列表截图

---

**提示：** 最常见的问题是文件编码和库依赖。确保这两项正确配置即可解决大部分编译错误。
