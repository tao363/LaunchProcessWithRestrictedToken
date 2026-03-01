@echo off
REM Windows 批处理构建脚本
REM 用法: build.bat [Release|Debug] [clean]

setlocal enabledelayedexpansion

set BUILD_TYPE=Release
set CLEAN_BUILD=0

REM 解析参数
if not "%1"=="" (
    if /i "%1"=="clean" (
        set CLEAN_BUILD=1
    ) else (
        set BUILD_TYPE=%1
    )
)

if not "%2"=="" (
    if /i "%2"=="clean" (
        set CLEAN_BUILD=1
    )
)

set PROJECT_ROOT=%~dp0
set BUILD_DIR=%PROJECT_ROOT%build

REM 清理构建
if %CLEAN_BUILD%==1 (
    echo 清理构建目录...
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%"
        echo 构建目录已清理
    )
    goto :end
)

REM 创建构建目录
if not exist "%BUILD_DIR%" (
    echo 创建构建目录: %BUILD_DIR%
    mkdir "%BUILD_DIR%"
)

cd /d "%BUILD_DIR%"

REM 配置 CMake
echo 配置 CMake...
cmake .. -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo CMake 配置失败
    cd /d "%PROJECT_ROOT%"
    exit /b 1
)

REM 编译
echo.
echo 开始编译 %BUILD_TYPE% 配置...
cmake --build . --config %BUILD_TYPE% --parallel
if errorlevel 1 (
    echo 编译失败
    cd /d "%PROJECT_ROOT%"
    exit /b 1
)

echo.
echo 编译成功!

REM 显示输出文件
if "%BUILD_TYPE%"=="Debug" (
    set OUTPUT_EXE=LaunchSandbox_d.exe
) else (
    set OUTPUT_EXE=LaunchSandbox.exe
)

set OUTPUT_PATH=%BUILD_DIR%\bin\%OUTPUT_EXE%
if exist "%OUTPUT_PATH%" (
    echo.
    echo 输出文件: %OUTPUT_PATH%
)

cd /d "%PROJECT_ROOT%"

:end
echo.
echo 构建完成!
endlocal
