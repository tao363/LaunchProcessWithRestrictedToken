@echo off
chcp 65001 >nul
echo ========================================
echo 强制重新加载项目
echo ========================================
echo.

echo [步骤 1] 关闭 Visual Studio
echo.
echo 请确保 Visual Studio 已完全关闭
echo 按任意键继续...
pause >nul

echo.
echo [步骤 2] 清理临时文件...
if exist ".vs" (
    echo 删除 .vs 文件夹...
    rmdir /s /q .vs
    echo [OK] .vs 已删除
)

if exist "x64" (
    echo 清理 x64 输出...
    rmdir /s /q x64
    echo [OK] x64 已清理
)

if exist "LaunchProcessWithRestrictedToken\x64" (
    echo 清理项目 x64 输出...
    rmdir /s /q LaunchProcessWithRestrictedToken\x64
    echo [OK] 项目 x64 已清理
)

echo.
echo [步骤 3] 验证项目文件...
findstr /C:"NetworkControlManager.cpp" LaunchProcessWithRestrictedToken\LaunchProcessWithRestrictedToken.vcxproj >nul
if %errorlevel%==0 (
    echo [OK] 项目文件包含 NetworkControlManager.cpp
) else (
    echo [错误] 项目文件不包含 NetworkControlManager.cpp
    pause
    exit /b 1
)

findstr /C:"Fwpuclnt.lib" LaunchProcessWithRestrictedToken\LaunchProcessWithRestrictedToken.vcxproj >nul
if %errorlevel%==0 (
    echo [OK] 项目文件包含库依赖
) else (
    echo [错误] 项目文件不包含库依赖
    pause
    exit /b 1
)

echo.
echo [步骤 4] 验证源文件存在...
for %%f in (ConfigReader.cpp FirewallControl.cpp NetworkControlManager.cpp WFPNetworkFilter.cpp) do (
    if exist "LaunchProcessWithRestrictedToken\%%f" (
        echo [OK] %%f
    ) else (
        echo [错误] 缺少 %%f
    )
)

echo.
echo ========================================
echo 准备完成
echo ========================================
echo.
echo 现在请执行以下步骤：
echo.
echo 1. 打开 Visual Studio
echo 2. 打开解决方案：LaunchProcessWithRestrictedToken.sln
echo 3. 在解决方案资源管理器中，展开项目
echo 4. 确认看到以下文件：
echo    - ConfigReader.cpp
echo    - FirewallControl.cpp
echo    - NetworkControlManager.cpp
echo    - WFPNetworkFilter.cpp
echo.
echo 5. 生成 -^> 清理解决方案
echo 6. 生成 -^> 重新生成解决方案
echo.
echo 如果仍然看不到这些文件：
echo    右键项目 -^> 卸载项目
echo    右键项目 -^> 重新加载项目
echo.

pause
