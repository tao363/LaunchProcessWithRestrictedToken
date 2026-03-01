# ⚠️ 链接错误快速修复

## 问题
```
无法解析的外部符号 NetworkControlManager::...
```

## 原因
`NetworkControlManager.cpp` 没有被编译到项目中

---

## ✅ 快速解决方案（3 分钟）

### 在 Visual Studio 中：

#### 1️⃣ 添加缺失的文件（1 分钟）

右键项目 -> 添加 -> 现有项，选择这 4 个文件：
```
ConfigReader.cpp
FirewallControl.cpp
NetworkControlManager.cpp        ⚠️ 最重要
WFPNetworkFilter.cpp
```

#### 2️⃣ 添加库依赖（1 分钟）

右键项目 -> 属性 -> 链接器 -> 输入 -> 附加依赖项：
```
Fwpuclnt.lib;ole32.lib;oleaut32.lib;
```

#### 3️⃣ 重新编译（1 分钟）

```
生成 -> 清理解决方案
生成 -> 重新生成解决方案
```

---

## 🔍 验证

编译时应该看到：
```
1>ConfigReader.cpp
1>FirewallControl.cpp
1>NetworkControlManager.cpp        ⚠️ 必须看到这一行
1>WFPNetworkFilter.cpp
```

**如果没有看到 `NetworkControlManager.cpp`，说明文件没有添加成功！**

---

## 📖 详细指南

如果快速方案不起作用，请查看：
- **MANUAL_FIX_GUIDE.md** - 详细的手动修复指南
- **force_reload.bat** - 强制重新加载脚本

---

## 💡 关键点

✅ 项目文件已自动更新（但 VS 可能没有识别）
✅ 需要手动在 VS 中添加文件
✅ 添加后立即重新编译

**最重要：确保在解决方案资源管理器中能看到 `NetworkControlManager.cpp`！**
