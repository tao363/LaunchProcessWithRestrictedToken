@echo off
chcp 65001 >nul
REM 编译错误快速修复脚本
echo ========================================
echo 编译错误快速修复
echo ========================================
echo.

echo [检查 1] 检查文件是否存在...
set missing=0
for %%f in (ConfigReader.h ConfigReader.cpp FirewallControl.h FirewallControl.cpp WFPNetworkFilter.h WFPNetworkFilter.cpp NetworkControlManager.h NetworkControlManager.cpp) do (
    if not exist "LaunchProcessWithRestrictedToken\%%f" (
        echo [错误] 缺少文件: %%f
        set missing=1
    ) else (
        echo [OK] %%f
    )
)

if %missing%==1 (
    echo.
    echo [错误] 有文件缺失，请确保所有文件都已创建
    pause
    exit /b 1
)

echo.
echo [检查 2] 转换文件编码为 UTF-8 with BOM...
PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& {
    $files = @(
        'LaunchProcessWithRestrictedToken\ConfigReader.h',
        'LaunchProcessWithRestrictedToken\ConfigReader.cpp',
        'LaunchProcessWithRestrictedToken\FirewallControl.h',
        'LaunchProcessWithRestrictedToken\FirewallControl.cpp',
        'LaunchProcessWithRestrictedToken\WFPNetworkFilter.h',
        'LaunchProcessWithRestrictedToken\WFPNetworkFilter.cpp',
        'LaunchProcessWithRestrictedToken\NetworkControlManager.h',
        'LaunchProcessWithRestrictedToken\NetworkControlManager.cpp'
    )

    foreach($f in $files) {
        if (Test-Path $f) {
            try {
                $content = Get-Content $f -Raw -Encoding UTF8
                $utf8BOM = New-Object System.Text.UTF8Encoding $true
                [System.IO.File]::WriteAllText($f, $content, $utf8BOM)
                Write-Host '[OK]' $f
            } catch {
                Write-Host '[错误]' $f ':' $_.Exception.Message
            }
        }
    }
}"

echo.
echo [检查 3] 验证文件编码...
PowerShell -NoProfile -ExecutionPolicy Bypass -Command "& {
    $files = @(
        'LaunchProcessWithRestrictedToken\ConfigReader.h',
        'LaunchProcessWithRestrictedToken\NetworkControlManager.h'
    )

    foreach($f in $files) {
        if (Test-Path $f) {
            $bytes = [System.IO.File]::ReadAllBytes($f)
            if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
                Write-Host '[OK]' $f '- UTF-8 with BOM'
            } else {
                Write-Host '[警告]' $f '- 不是 UTF-8 with BOM'
            }
        }
    }
}"

echo.
echo [检查 4] 检查项目文件...
if not exist "LaunchProcessWithRestrictedToken.sln" (
    echo [错误] 找不到解决方案文件
    pause
    exit /b 1
)
echo [OK] 解决方案文件存在

echo.
echo ========================================
echo 修复完成
echo ========================================
echo.
echo 下一步操作：
echo.
echo 1. 在 Visual Studio 中打开项目
echo 2. 确认所有新文件已添加到项目中
echo 3. 添加库依赖：Fwpuclnt.lib;ole32.lib;oleaut32.lib
echo 4. 生成 -^> 清理解决方案
echo 5. 生成 -^> 重新生成解决方案
echo.
echo 详细步骤请参考：COMPILE_FIX_GUIDE.md
echo.

pause
