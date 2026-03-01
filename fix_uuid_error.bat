@echo off
chcp 65001 >nul
echo ========================================
echo 修复 UuidCreate 链接错误
echo ========================================
echo.

echo [问题] 无法解析的外部符号 __imp_UuidCreate
echo [原因] Rpcrt4.lib 没有被正确链接
echo.

echo [解决方案 1] 手动在 Visual Studio 中添加
echo.
echo 1. 右键项目 -^> 属性
echo 2. 链接器 -^> 输入 -^> 附加依赖项
echo 3. 确保包含：Fwpuclnt.lib;ole32.lib;oleaut32.lib;Rpcrt4.lib;
echo 4. 应用 -^> 确定
echo 5. 清理并重新生成
echo.

echo [解决方案 2] 强制重新加载项目
echo.
echo 按任意键执行清理并强制重新加载...
pause >nul

echo.
echo [执行] 关闭 Visual Studio（如果已打开）
echo 请确保 Visual Studio 已完全关闭
pause

echo.
echo [执行] 清理缓存和输出文件...
if exist ".vs" (
    rmdir /s /q .vs
    echo [OK] 已删除 .vs
)

if exist "x64" (
    rmdir /s /q x64
    echo [OK] 已删除 x64
)

if exist "LaunchProcessWithRestrictedToken\x64" (
    rmdir /s /q LaunchProcessWithRestrictedToken\x64
    echo [OK] 已删除项目 x64
)

echo.
echo [验证] 检查项目文件...
findstr /C:"Rpcrt4.lib" LaunchProcessWithRestrictedToken\LaunchProcessWithRestrictedToken.vcxproj >nul
if %errorlevel%==0 (
    echo [OK] 项目文件包含 Rpcrt4.lib
) else (
    echo [错误] 项目文件不包含 Rpcrt4.lib
    echo 需要手动添加！
    pause
    exit /b 1
)

echo.
echo ========================================
echo 清理完成
echo ========================================
echo.
echo 现在请执行以下步骤：
echo.
echo 1. 打开 Visual Studio
echo 2. 打开解决方案
echo 3. 右键项目 -^> 属性
echo 4. 链接器 -^> 输入 -^> 附加依赖项
echo 5. 确认包含：Rpcrt4.lib
echo 6. 生成 -^> 清理解决方案
echo 7. 生成 -^> 重新生成解决方案
echo.
echo 或者使用命令行编译：
echo.
echo   打开 "x64 Native Tools Command Prompt for VS 2022"
echo   cd %CD%
echo   msbuild LaunchProcessWithRestrictedToken.sln /t:Rebuild /p:Configuration=Release /p:Platform=x64
echo.

pause
