#pragma once

#include <Windows.h>

// 日志函数声明
extern void LogInfo(const wchar_t* fmt, ...);
extern void LogWarn(const wchar_t* fmt, ...);
extern void LogError(const wchar_t* fmt, ...);
extern void LogDebug(const wchar_t* fmt, ...);

// 代理日志函数声明
extern void LogProxyAllow(const wchar_t* method, const wchar_t* domain, const wchar_t* matchedPattern);
extern void LogProxyBlock(const wchar_t* method, const wchar_t* domain, const wchar_t* reason);
extern void LogProxyInfo(const wchar_t* fmt, ...);
extern void LogProxyWarn(const wchar_t* fmt, ...);
extern void LogProxyError(const wchar_t* fmt, ...);
