# 强制修复 UuidCreate 链接错误

## 问题
```
无法解析的外部符号 __imp_UuidCreate
```

## 原因
`Rpcrt4.lib` 虽然已添加到项目文件，但 Visual Studio 可能没有重新加载配置。

---

## ✅ 解决方案

### 方法 1：手动添加库依赖（最可靠）

1. **在 Visual Studio 中**
2. **右键项目** -> 属性
3. **配置属性** -> 链接器 -> 输入
4. **附加依赖项**，确保包含：
   ```
   Fwpuclnt.lib;ole32.lib;oleaut32.lib;Rpcrt4.lib;
   ```
5. **确保选择**：
   - 配置：所有配置
   - 平台：所有平台
6. **应用** -> **确定**
7. **生成** -> **清理解决方案**
8. **生成** -> **重新生成解决方案**

---

### 方法 2：关闭并重新打开 Visual Studio

1. **关闭 Visual Studio**
2. **删除缓存**：
   - 删除 `.vs` 文件夹
   - 删除 `x64` 文件夹
3. **重新打开** Visual Studio
4. **打开解决方案**
5. **重新生成**

---

### 方法 3：使用命令行编译

打开 **x64 Native Tools Command Prompt for VS 2022**：

```bash
cd F:\Project\AI\sanbox\LaunchProcessWithRestrictedTokenCC

# 清理
msbuild LaunchProcessWithRestrictedToken.sln /t:Clean /p:Configuration=Release /p:Platform=x64

# 重新生成
msbuild LaunchProcessWithRestrictedToken.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64 /p:AdditionalLibPaths="" /p:AdditionalDependencies="Fwpuclnt.lib;ole32.lib;oleaut32.lib;Rpcrt4.lib"
```

---

## 🔍 验证库依赖

### 检查项目属性

在 Visual Studio 中：
1. 右键项目 -> 属性
2. 配置属性 -> 链接器 -> 输入
3. 附加依赖项应该显示：
   ```
   Fwpuclnt.lib
   ole32.lib
   oleaut32.lib
   Rpcrt4.lib
   ```

### 检查项目文件

打开 `LaunchProcessWithRestrictedToken.vcxproj`，搜索 `AdditionalDependencies`，应该看到：
```xml
<AdditionalDependencies>Fwpuclnt.lib;ole32.lib;oleaut32.lib;Rpcrt4.lib;%(AdditionalDependencies)</AdditionalDependencies>
```

---

## 💡 为什么需要 Rpcrt4.lib？

`UuidCreate` 函数在 `WFPNetworkFilter.cpp` 中使用：
```cpp
UuidCreate(&g_providerKey);
UuidCreate(&g_sublayerKey);
```

这个函数来自 Windows RPC 库，需要链接 `Rpcrt4.lib`。

---

## 🎯 成功标志

编译成功后应该看到：
```
========== 生成: 1 成功，0 失败 ==========
```

没有 "无法解析的外部符号" 错误。

---

## 🆘 如果仍然失败

请尝试：

1. **完全清理项目**
   ```bash
   # 删除所有输出
   rmdir /s /q .vs
   rmdir /s /q x64
   rmdir /s /q LaunchProcessWithRestrictedToken\x64
   ```

2. **手动编辑项目文件**
   - 关闭 Visual Studio
   - 用记事本打开 `.vcxproj` 文件
   - 确认所有 `<Link>` 部分都包含 `Rpcrt4.lib`
   - 保存并重新打开 VS

3. **使用命令行编译**（方法 3）

---

**提示：** 最常见的原因是 Visual Studio 缓存了旧的配置。关闭 VS，删除 `.vs` 文件夹，重新打开通常可以解决问题。
