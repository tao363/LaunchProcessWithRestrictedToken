@echo off
REM 修复编译错误脚本
echo ========================================
echo 修复编译错误
echo ========================================
echo.

REM 检查文件是否存在
if not exist "LaunchProcessWithRestrictedToken\ConfigReader.h" (
    echo [错误] 找不到 ConfigReader.h
    pause
    exit /b 1
)

echo [步骤 1] 检测到的问题：
echo   - 文件编码问题（代码页 936）
echo   - 可能的头文件包含问题
echo.

echo [步骤 2] 解决方案：
echo   1. 将所有 .h 和 .cpp 文件保存为 UTF-8 with BOM 编码
echo   2. 确保头文件包含顺序正确
echo   3. 添加必要的前置声明
echo.

echo [步骤 3] 在 Visual Studio 中手动修复：
echo.
echo 对于每个新添加的文件（ConfigReader, FirewallControl, WFPNetworkFilter, NetworkControlManager）：
echo.
echo   1. 在 Visual Studio 中打开文件
echo   2. 文件 -^> 高级保存选项
echo   3. 编码：Unicode (UTF-8 带签名) - 代码页 65001
echo   4. 点击确定
echo   5. 保存文件 (Ctrl+S)
echo.

echo [步骤 4] 或者使用以下 PowerShell 命令自动转换：
echo.
echo PowerShell -Command "$files = @('ConfigReader.h', 'ConfigReader.cpp', 'FirewallControl.h', 'FirewallControl.cpp', 'WFPNetworkFilter.h', 'WFPNetworkFilter.cpp', 'NetworkControlManager.h', 'NetworkControlManager.cpp'); foreach($f in $files) { $content = Get-Content \"LaunchProcessWithRestrictedToken\$f\" -Raw; [System.IO.File]::WriteAllText(\"LaunchProcessWithRestrictedToken\$f\", $content, [System.Text.UTF8Encoding]::new($true)) }"
echo.

pause
echo.
echo 是否自动转换文件编码？(Y/N)
set /p choice=
if /i "%choice%"=="Y" (
    echo.
    echo [执行] 转换文件编码为 UTF-8 with BOM...
    PowerShell -Command "$files = @('ConfigReader.h', 'ConfigReader.cpp', 'FirewallControl.h', 'FirewallControl.cpp', 'WFPNetworkFilter.h', 'WFPNetworkFilter.cpp', 'NetworkControlManager.h', 'NetworkControlManager.cpp'); foreach($f in $files) { $path = 'LaunchProcessWithRestrictedToken\' + $f; if (Test-Path $path) { $content = Get-Content $path -Raw; [System.IO.File]::WriteAllText($path, $content, [System.Text.UTF8Encoding]::new($true)); Write-Host \"[OK] $f\" } else { Write-Host \"[SKIP] $f not found\" } }"
    echo.
    echo [完成] 文件编码已转换
    echo.
    echo 现在请在 Visual Studio 中重新编译项目
) else (
    echo.
    echo [跳过] 请手动在 Visual Studio 中转换文件编码
)

echo.
pause
