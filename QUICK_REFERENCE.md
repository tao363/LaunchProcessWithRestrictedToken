# 🚀 快速参考卡片

## ✅ 编译错误已修复

**文件编码问题已解决** - 所有文件都是 UTF-8 with BOM 编码

---

## 📋 现在需要做的 3 件事

### 1️⃣ 添加文件到项目（2 分钟）

在 Visual Studio 中：
- 右键项目 -> 添加 -> 现有项
- 选择这 8 个文件并添加：
  ```
  ConfigReader.h/cpp
  FirewallControl.h/cpp
  WFPNetworkFilter.h/cpp
  NetworkControlManager.h/cpp
  ```

### 2️⃣ 添加库依赖（1 分钟）

项目属性 -> 链接器 -> 输入 -> 附加依赖项：
```
Fwpuclnt.lib;ole32.lib;oleaut32.lib
```

### 3️⃣ 重新编译（1 分钟）

```
生成 -> 清理解决方案
生成 -> 重新生成解决方案
```

---

## 🎯 如果仍有错误

| 错误类型 | 解决方法 |
|---------|---------|
| 未声明的标识符 | 确认文件已添加到项目 |
| 语法错误 | 检查文件编码（UTF-8 with BOM） |
| 无法解析的外部符号 | 添加库依赖 |

**详细指南：** [ENCODING_FIXED.md](ENCODING_FIXED.md)

---

## 📖 文档导航

- **ENCODING_FIXED.md** - 编译错误修复（当前问题）⭐
- **COMPILE_FIX_GUIDE.md** - 详细修复指南
- **TODO.md** - 完整操作清单
- **QUICK_START.md** - 快速开始

---

## ✨ 编译成功后

```bash
# 复制配置文件
copy config.ini x64\Release\

# 以管理员身份运行
cd x64\Release
LaunchProcessWithRestrictedToken.exe cmd.exe
```

---

**提示：** 文件编码问题已解决，现在只需在 Visual Studio 中添加文件和库依赖即可编译成功！
