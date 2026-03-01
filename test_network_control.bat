@echo off
REM 网络控制测试脚本
REM 此脚本用于测试不同的网络控制方法

setlocal enabledelayedexpansion

echo ========================================
echo 网络控制测试脚本
echo ========================================
echo.

REM 检查管理员权限
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo [错误] 此脚本需要管理员权限！
    echo 请右键点击脚本，选择"以管理员身份运行"
    pause
    exit /b 1
)

echo [OK] 管理员权限检查通过
echo.

REM 检查可执行文件
if not exist "x64\Release\LaunchProcessWithRestrictedToken.exe" (
    echo [错误] 找不到可执行文件：x64\Release\LaunchProcessWithRestrictedToken.exe
    echo 请先编译项目
    pause
    exit /b 1
)

echo [OK] 可执行文件存在
echo.

REM 备份原始配置
if exist "config.ini" (
    copy /Y config.ini config.ini.backup >nul
    echo [OK] 已备份原始配置到 config.ini.backup
)

REM 创建测试配置
echo.
echo ========================================
echo 测试 1: Windows Firewall 方法
echo ========================================
echo.

(
echo [NetworkControl]
echo Method=WindowsFirewall
echo ProxyPort=8080
echo AllowLocalhost=true
echo AllowedDomains=*.example.com,*.github.com
echo.
echo [App]
echo networkFilterEnabled=true
) > config.ini

echo [配置] 已创建测试配置（Windows Firewall）
echo.

REM 复制配置到输出目录
copy /Y config.ini x64\Release\ >nul

echo [测试] 启动沙箱程序...
echo 请在打开的命令提示符中测试网络访问：
echo   - curl http://www.example.com  （应该允许）
echo   - curl http://www.google.com   （应该被阻止）
echo.
echo 按任意键继续...
pause >nul

cd x64\Release
LaunchProcessWithRestrictedToken.exe cmd.exe
cd ..\..

echo.
echo [测试] Windows Firewall 测试完成
echo.

REM 测试 WFP 方法
echo ========================================
echo 测试 2: WFP 方法
echo ========================================
echo.

(
echo [NetworkControl]
echo Method=WFP
echo ProxyPort=8080
echo AllowLocalhost=true
echo AllowedDomains=*.example.com,*.github.com
echo.
echo [App]
echo networkFilterEnabled=true
) > config.ini

echo [配置] 已创建测试配置（WFP）
echo.

copy /Y config.ini x64\Release\ >nul

echo [测试] 启动沙箱程序...
echo 请在打开的命令提示符中测试网络访问
echo.
echo 按任意键继续...
pause >nul

cd x64\Release
LaunchProcessWithRestrictedToken.exe cmd.exe
cd ..\..

echo.
echo [测试] WFP 测试完成
echo.

REM 测试 None 方法（对比）
echo ========================================
echo 测试 3: None 方法（无强制控制）
echo ========================================
echo.

(
echo [NetworkControl]
echo Method=None
echo ProxyPort=8080
echo AllowLocalhost=true
echo AllowedDomains=*.example.com
echo.
echo [App]
echo networkFilterEnabled=true
) > config.ini

echo [配置] 已创建测试配置（None - 可被绕过）
echo.

copy /Y config.ini x64\Release\ >nul

echo [测试] 启动沙箱程序...
echo 注意：此模式下网络控制可以被绕过！
echo 请测试直接连接是否成功
echo.
echo 按任意键继续...
pause >nul

cd x64\Release
LaunchProcessWithRestrictedToken.exe cmd.exe
cd ..\..

echo.
echo [测试] None 方法测试完成
echo.

REM 恢复原始配置
if exist "config.ini.backup" (
    copy /Y config.ini.backup config.ini >nul
    del config.ini.backup
    echo [OK] 已恢复原始配置
)

echo.
echo ========================================
echo 测试完成
echo ========================================
echo.
echo 测试总结：
echo 1. Windows Firewall - 推荐使用，简单有效
echo 2. WFP - 最强控制，内核级拦截
echo 3. None - 仅代理，可被绕过（不推荐）
echo.
echo 请查看日志文件了解详细信息
echo.

pause
