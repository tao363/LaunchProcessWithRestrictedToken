#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include <sdkddkver.h>
#include <Windows.h>
#include <UserEnv.h>
#include <sddl.h>
#include <Aclapi.h>
#include <WinSafer.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cstdarg>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "Userenv.lib")
#pragma comment(lib, "Advapi32.lib")

#include "NetworkFilterPlugin.h"

#ifndef CP_UTF8
#define CP_UTF8 65001
#endif

struct EnvKV {
	std::wstring name;
	std::wstring value;
};

enum class PathAccessLevel {
	ReadOnly,
	ReadExecute,
	ReadWrite,
	FullControl
};

struct AllowedPathEntry {
	std::wstring path;
	PathAccessLevel accessLevel;
};

struct SavedSecurity {
	std::wstring path;
	PSECURITY_DESCRIPTOR sdDacl = nullptr;
	PACL dacl = nullptr;
	bool hasDacl = false;
	bool daclProtected = false;

	PSECURITY_DESCRIPTOR sdSacl = nullptr;
	PACL sacl = nullptr;
	bool hasSacl = false;

	~SavedSecurity() {
		if (sdDacl) { LocalFree(sdDacl); sdDacl = nullptr; }
		if (sdSacl) { LocalFree(sdSacl); sdSacl = nullptr; }
	}

	SavedSecurity() = default;
	SavedSecurity(const SavedSecurity&) = delete;
	SavedSecurity& operator=(const SavedSecurity&) = delete;
	SavedSecurity(SavedSecurity&& other) noexcept
		: path(std::move(other.path)), sdDacl(other.sdDacl), dacl(other.dacl),
		hasDacl(other.hasDacl), daclProtected(other.daclProtected),
		sdSacl(other.sdSacl), sacl(other.sacl), hasSacl(other.hasSacl) {
		other.sdDacl = nullptr; other.sdSacl = nullptr;
		other.dacl = nullptr; other.sacl = nullptr;
		other.hasDacl = false; other.hasSacl = false;
	}
	SavedSecurity& operator=(SavedSecurity&& other) noexcept {
		if (this != &other) {
			if (sdDacl) LocalFree(sdDacl);
			if (sdSacl) LocalFree(sdSacl);
			path = std::move(other.path);
			sdDacl = other.sdDacl; dacl = other.dacl;
			hasDacl = other.hasDacl; daclProtected = other.daclProtected;
			sdSacl = other.sdSacl; sacl = other.sacl; hasSacl = other.hasSacl;
			other.sdDacl = nullptr; other.sdSacl = nullptr;
		}
		return *this;
	}
};

#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif

static WCHAR* ExeToLaunch = nullptr;
static std::wstring PackageMoniker;
static std::wstring PackageDisplayName;

static std::vector<AllowedPathEntry> AllowedPaths;
static std::vector<EnvKV> EnvOverrides;
static std::vector<std::wstring> PathPrependEntries;
static std::vector<SavedSecurity> g_SavedSecurity;

static std::vector<wchar_t> g_CmdLineBuf;
static std::vector<wchar_t> g_ChildEnv;

static bool WaitForExit = true;
static bool PathLowIntegrity = true;
static int IntegrityLevel = 0;  // 0=Low(4096), 1=Medium(8192), 2=High(12288)
static bool CleanupAllowedSubdirs = false;
static bool g_WaitAutoEnabled = false;
static bool UseNewConsole = false;
static bool HideParentConsole = true;
static bool g_LogEnabled = false;

// Network Filter Plugin state
static bool g_NetworkFilterEnabled = false;
static int  g_NetworkFilterPort = 8080;
static std::wstring g_NetworkFilterAllowedUrls;

// Bun Virtual Drive (B:\~BUN) state
static volatile LONG g_BunCleanupDone = 0;
static bool g_BunDriveMapped = false;
static std::wstring g_BunStagingDir;
static std::wstring g_BunDriveTarget;
static std::vector<std::wstring> g_BunFallbackDirs;

static volatile LONG g_RestoreDone = 0;
static volatile LONG g_PathAclModified = 0;

// Log file state
static HANDLE g_LogFile = INVALID_HANDLE_VALUE;
static std::wstring g_LogFilePath;

// Proxy log file state
static HANDLE g_ProxyLogFile = INVALID_HANDLE_VALUE;
static std::wstring g_ProxyLogFilePath;
static volatile LONG g_ProxyAllowCount = 0;
static volatile LONG g_ProxyBlockCount = 0;

static HANDLE g_ChildProcess = nullptr;
static HANDLE g_hJob = nullptr;
static DWORD g_ChildConsoleHostPid = 0;
static DWORD g_ParentConsoleHostPid = 0;
static std::vector<DWORD> g_ConsoleHostsToCleanup;


// ========================================================================
// Logging
// ========================================================================

static void InitLogFile() {
	if (!g_LogEnabled) return;

	wchar_t exePath[MAX_PATH] = {};
	DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return;

	std::wstring dir(exePath, n);
	size_t pos = dir.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		dir = dir.substr(0, pos + 1);
	}
	else {
		dir = L".\\";
	}

	SYSTEMTIME st{};
	GetLocalTime(&st);
	wchar_t fname[128] = {};
	_snwprintf_s(fname, _countof(fname), _TRUNCATE,
		L"LaunchSandbox_%04u%02u%02u_%02u%02u%02u%03u.log",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	g_LogFilePath = dir + fname;

	g_LogFile = CreateFileW(g_LogFilePath.c_str(),
		FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (g_LogFile == INVALID_HANDLE_VALUE) {
		g_LogFilePath = std::wstring(L".\\") + fname;
		g_LogFile = CreateFileW(g_LogFilePath.c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
	}

	if (g_LogFile != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER fileSize{};
		if (GetFileSizeEx(g_LogFile, &fileSize) && fileSize.QuadPart == 0) {
			const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
			DWORD written = 0;
			WriteFile(g_LogFile, bom, sizeof(bom), &written, nullptr);
		}
	}
}

static void CloseLogFile() {
	if (g_LogFile != INVALID_HANDLE_VALUE) {
		CloseHandle(g_LogFile);
		g_LogFile = INVALID_HANDLE_VALUE;
	}
}

void LogV(PCWSTR level, PCWSTR fmt, va_list ap) {
	if (!g_LogEnabled) return;

	SYSTEMTIME st{};
	GetLocalTime(&st);

	wchar_t prefix[80] = {};
	_snwprintf_s(prefix, _countof(prefix), _TRUNCATE,
		L"[%04u-%02u-%02u %02u:%02u:%02u.%03u][%ls] ",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level);

	wchar_t body[4096] = {};
	va_list apCopy;
	va_copy(apCopy, ap);
	_vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, apCopy);
	va_end(apCopy);

	if (g_LogEnabled) wprintf(L"%ls%ls\r\n", prefix, body);

	if (g_LogFile != INVALID_HANDLE_VALUE) {
		std::wstring line = std::wstring(prefix) + body + L"\r\n";
		int utf8Len = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(),
			nullptr, 0, nullptr, nullptr);
		if (utf8Len > 0) {
			std::vector<char> utf8Buf(utf8Len);
			WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(),
				utf8Buf.data(), utf8Len, nullptr, nullptr);
			DWORD written = 0;
			WriteFile(g_LogFile, utf8Buf.data(), (DWORD)utf8Len, &written, nullptr);
		}
	}
}

void LogInfo(PCWSTR fmt, ...) {
	va_list ap; va_start(ap, fmt); LogV(L"INFO", fmt, ap); va_end(ap);
}
void LogWarn(PCWSTR fmt, ...) {
	va_list ap; va_start(ap, fmt); LogV(L"WARN", fmt, ap); va_end(ap);
}
void LogError(PCWSTR fmt, ...) {
	va_list ap; va_start(ap, fmt); LogV(L"ERROR", fmt, ap); va_end(ap);
}
void LogDebug(PCWSTR fmt, ...) {
	va_list ap; va_start(ap, fmt); LogV(L"DEBUG", fmt, ap); va_end(ap);
}

// ========================================================================
// Proxy Log
// ========================================================================

static void InitProxyLogFile() {
	if (!g_LogEnabled || !g_NetworkFilterEnabled) return;

	wchar_t exePath[MAX_PATH] = {};
	DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return;

	std::wstring dir(exePath, n);
	size_t pos = dir.find_last_of(L"\\/");
	if (pos != std::wstring::npos) {
		dir = dir.substr(0, pos + 1);
	}
	else {
		dir = L".\\";
	}

	SYSTEMTIME st{};
	GetLocalTime(&st);
	wchar_t fname[128] = {};
	_snwprintf_s(fname, _countof(fname), _TRUNCATE,
		L"NetworkFilterProxy_%04u%02u%02u_%02u%02u%02u%03u.log",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	g_ProxyLogFilePath = dir + fname;

	g_ProxyLogFile = CreateFileW(g_ProxyLogFilePath.c_str(),
		FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (g_ProxyLogFile == INVALID_HANDLE_VALUE) {
		g_ProxyLogFilePath = std::wstring(L".\\") + fname;
		g_ProxyLogFile = CreateFileW(g_ProxyLogFilePath.c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL, nullptr);
	}

	if (g_ProxyLogFile != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER fileSize{};
		if (GetFileSizeEx(g_ProxyLogFile, &fileSize) && fileSize.QuadPart == 0) {
			const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
			DWORD written = 0;
			WriteFile(g_ProxyLogFile, bom, sizeof(bom), &written, nullptr);
		}
	}
}

static void CloseProxyLogFile() {
	if (g_ProxyLogFile != INVALID_HANDLE_VALUE) {
		LONG allows = InterlockedCompareExchange(&g_ProxyAllowCount, 0, 0);
		LONG blocks = InterlockedCompareExchange(&g_ProxyBlockCount, 0, 0);

		SYSTEMTIME st{};
		GetLocalTime(&st);
		wchar_t summary[512] = {};
		_snwprintf_s(summary, _countof(summary), _TRUNCATE,
			L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ========== Session summary: %ld ALLOWED, %ld BLOCKED ==========\r\n",
			st.wYear, st.wMonth, st.wDay,
			st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
			allows, blocks);

		int len = (int)wcslen(summary);
		int utf8Len = WideCharToMultiByte(CP_UTF8, 0, summary, len, nullptr, 0, nullptr, nullptr);
		if (utf8Len > 0) {
			std::vector<char> utf8Buf(utf8Len);
			WideCharToMultiByte(CP_UTF8, 0, summary, len, utf8Buf.data(), utf8Len, nullptr, nullptr);
			DWORD written = 0;
			WriteFile(g_ProxyLogFile, utf8Buf.data(), (DWORD)utf8Buf.size(), &written, nullptr);
		}

		CloseHandle(g_ProxyLogFile);
		g_ProxyLogFile = INVALID_HANDLE_VALUE;
	}
}

void ProxyLogWriteLine(PCWSTR line, bool alsoConsole = false) {
	if (alsoConsole) {
		if (g_LogEnabled) wprintf(L"%ls\r\n", line);
	}

	if (g_ProxyLogFile != INVALID_HANDLE_VALUE) {
		std::wstring full = std::wstring(line) + L"\r\n";
		int utf8Len = WideCharToMultiByte(CP_UTF8, 0, full.c_str(), (int)full.size(),
			nullptr, 0, nullptr, nullptr);
		if (utf8Len > 0) {
			std::vector<char> utf8Buf(utf8Len);
			WideCharToMultiByte(CP_UTF8, 0, full.c_str(), (int)full.size(),
				utf8Buf.data(), utf8Len, nullptr, nullptr);
			DWORD written = 0;
			WriteFile(g_ProxyLogFile, utf8Buf.data(), (DWORD)utf8Len, &written, nullptr);
		}
	}
}

void LogProxyAllow(const wchar_t* method, const wchar_t* domain, const wchar_t* matchedPattern) {
	InterlockedIncrement(&g_ProxyAllowCount);
	SYSTEMTIME st{};
	GetLocalTime(&st);
	wchar_t line[1024] = {};
	_snwprintf_s(line, _countof(line), _TRUNCATE,
		L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] ALLOW  %-7ls %ls  (rule: %ls)",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		method, domain, matchedPattern);
	ProxyLogWriteLine(line);
}

void LogProxyBlock(const wchar_t* method, const wchar_t* domain, const wchar_t* reason) {
	InterlockedIncrement(&g_ProxyBlockCount);
	SYSTEMTIME st{};
	GetLocalTime(&st);
	wchar_t line[1024] = {};
	_snwprintf_s(line, _countof(line), _TRUNCATE,
		L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] BLOCK  %-7ls %ls  (%ls)",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		method, domain, reason);
	ProxyLogWriteLine(line, true);
}

void LogProxyV(PCWSTR level, PCWSTR fmt, va_list ap) {
	SYSTEMTIME st{};
	GetLocalTime(&st);
	wchar_t prefix[80] = {};
	_snwprintf_s(prefix, _countof(prefix), _TRUNCATE,
		L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] %-6ls ",
		st.wYear, st.wMonth, st.wDay,
		st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level);
	wchar_t body[4096] = {};
	va_list apCopy;
	va_copy(apCopy, ap);
	_vsnwprintf_s(body, _countof(body), _TRUNCATE, fmt, apCopy);
	va_end(apCopy);
	wchar_t line[4200] = {};
	_snwprintf_s(line, _countof(line), _TRUNCATE, L"%ls%ls", prefix, body);
	ProxyLogWriteLine(line);
}

void LogProxyInfo(PCWSTR fmt, ...) {
	va_list ap; va_start(ap, fmt); LogProxyV(L"INFO", fmt, ap); va_end(ap);
}
void LogProxyWarn(PCWSTR fmt, ...) {
	va_list ap; va_start(ap, fmt); LogProxyV(L"WARN", fmt, ap); va_end(ap);
}
void LogProxyError(PCWSTR fmt, ...) {
	va_list ap; va_start(ap, fmt); LogProxyV(L"ERROR", fmt, ap); va_end(ap);
}

// ========================================================================
// String / Path Utilities
// ========================================================================
static bool IsSpace(wchar_t ch) {
	return ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n';
}

static std::wstring TrimCopy(const std::wstring& s) {
	size_t b = 0;
	size_t e = s.size();
	while (b < e && IsSpace(s[b])) ++b;
	while (e > b && IsSpace(s[e - 1])) --e;
	std::wstring t = s.substr(b, e - b);
	if (t.size() >= 2) {
		if ((t.front() == L'"' && t.back() == L'"') || (t.front() == L'\'' && t.back() == L'\'')) {
			t = t.substr(1, t.size() - 2);
		}
	}
	return t;
}

static bool IEquals(const std::wstring& a, const std::wstring& b) {
	return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

static std::wstring ToLowerCopy(std::wstring s) {
	for (auto& ch : s) ch = static_cast<wchar_t>(towlower(ch));
	return s;
}

static std::wstring NormalizePathForCompare(const std::wstring& in) {
	std::wstring p = TrimCopy(in);
	std::replace(p.begin(), p.end(), L'/', L'\\');
	while (p.size() > 3 && !p.empty() && p.back() == L'\\') p.pop_back();
	if (p.size() == 2 && p[1] == L':') p.push_back(L'\\');
	return p;
}

static bool PathEqualsInsensitive(const std::wstring& a, const std::wstring& b) {
	std::wstring na = NormalizePathForCompare(a);
	std::wstring nb = NormalizePathForCompare(b);
	return _wcsicmp(na.c_str(), nb.c_str()) == 0;
}

static bool VectorHasPathInsensitive(const std::vector<AllowedPathEntry>& list, const std::wstring& path) {
	for (const auto& item : list) {
		if (PathEqualsInsensitive(item.path, path)) return true;
	}
	return false;
}

static bool VectorHasPathInsensitiveWStr(const std::vector<std::wstring>& list, const std::wstring& path) {
	for (const auto& item : list) {
		if (PathEqualsInsensitive(item, path)) return true;
	}
	return false;
}

static bool AddAllowedPathUnique(const std::wstring& path, PathAccessLevel level = PathAccessLevel::FullControl) {
	std::wstring p = TrimCopy(path);
	if (p.empty()) return false;
	if (VectorHasPathInsensitive(AllowedPaths, p)) return false;
	AllowedPaths.push_back({ p, level });
	return true;
}

static bool AddPathPrependUnique(const std::wstring& path) {
	std::wstring p = TrimCopy(path);
	if (p.empty()) return false;
	if (VectorHasPathInsensitiveWStr(PathPrependEntries, p)) return false;
	PathPrependEntries.emplace_back(p);
	return true;
}

static std::wstring GetEnvVarCopy(PCWSTR name) {
	DWORD need = GetEnvironmentVariableW(name, nullptr, 0);
	if (need == 0) return L"";
	std::vector<wchar_t> buf(need);
	DWORD got = GetEnvironmentVariableW(name, buf.data(), need);
	if (got == 0 || got >= need) return L"";
	return std::wstring(buf.data(), got);
}

static bool DirectoryExists(const std::wstring& path) {
	if (path.empty()) return false;
	DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

static bool IsDotOrDotDot(const wchar_t* name) {
	return name && (wcscmp(name, L".") == 0 || wcscmp(name, L"..") == 0);
}

static void ClearReadOnlyAttributeIfSet(const std::wstring& path) {
	DWORD attrs = GetFileAttributesW(path.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES) return;
	if (attrs & FILE_ATTRIBUTE_READONLY) {
		SetFileAttributesW(path.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
	}
}

static DWORD DeleteTreeNoFollow(const std::wstring& path) {
	std::vector<std::wstring> dirsToDelete;
	dirsToDelete.push_back(path);

	const int MAX_ITERATIONS = 100000;
	int iterations = 0;

	while (!dirsToDelete.empty()) {
		if (++iterations > MAX_ITERATIONS) {
			LogWarn(L"[Cleanup] DeleteTreeNoFollow exceeded max iterations on: %ls", path.c_str());
			return ERROR_TOO_MANY_NAMES;
		}
		std::wstring currentPath = std::move(dirsToDelete.back());
		dirsToDelete.pop_back();

		DWORD attrs = GetFileAttributesW(currentPath.c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			DWORD e = GetLastError();
			if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) continue;
			return e;
		}

		if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			ClearReadOnlyAttributeIfSet(currentPath);
			if (DeleteFileW(currentPath.c_str())) continue;
			DWORD e = GetLastError();
			if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) continue;
			return e;
		}

		if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) {
			ClearReadOnlyAttributeIfSet(currentPath);
			if (RemoveDirectoryW(currentPath.c_str())) continue;
			DWORD e = GetLastError();
			if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) continue;
			return e;
		}

		std::wstring search = currentPath;
		if (!search.empty() && search.back() != L'\\') search.push_back(L'\\');
		search.append(L"*");

		WIN32_FIND_DATAW fd{};
		HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
		if (hFind != INVALID_HANDLE_VALUE) {
			std::vector<std::wstring> subItems;
			do {
				if (IsDotOrDotDot(fd.cFileName)) continue;
				std::wstring child = currentPath;
				if (!child.empty() && child.back() != L'\\') child.push_back(L'\\');
				child.append(fd.cFileName);
				subItems.push_back(std::move(child));
			} while (FindNextFileW(hFind, &fd));

			DWORD e = GetLastError();
			FindClose(hFind);
			if (e != ERROR_NO_MORE_FILES) return e;

			if (subItems.empty()) {
				ClearReadOnlyAttributeIfSet(currentPath);
				if (!RemoveDirectoryW(currentPath.c_str())) {
					DWORD re = GetLastError();
					if (re != ERROR_FILE_NOT_FOUND && re != ERROR_PATH_NOT_FOUND) return re;
				}
			}
			else {
				dirsToDelete.push_back(currentPath);
				for (auto& item : subItems) dirsToDelete.push_back(std::move(item));
			}
		}
		else {
			DWORD e = GetLastError();
			if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) return e;
			ClearReadOnlyAttributeIfSet(currentPath);
			if (!RemoveDirectoryW(currentPath.c_str())) {
				e = GetLastError();
				if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) return e;
			}
		}
	}

	return ERROR_SUCCESS;
}

static void DeleteSubdirectoriesOfRoot(const std::wstring& root) {
	std::wstring r = NormalizePathForCompare(root);
	if (r.empty() || r.size() <= 3) {
		LogWarn(L"[Cleanup] Refuse to clean subdirs of drive root: %ls", root.c_str());
		return;
	}
	if (!DirectoryExists(r)) return;

	std::wstring search = r;
	if (!search.empty() && search.back() != L'\\') search.push_back(L'\\');
	search.append(L"*");

	WIN32_FIND_DATAW fd{};
	HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
	if (hFind == INVALID_HANDLE_VALUE) {
		DWORD e = GetLastError();
		if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) {
			LogWarn(L"[Cleanup] FindFirstFile failed on %ls (%lu)", r.c_str(), e);
		}
		return;
	}

	do {
		if (IsDotOrDotDot(fd.cFileName)) continue;
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) continue;

		std::wstring child = r;
		if (!child.empty() && child.back() != L'\\') child.push_back(L'\\');
		child.append(fd.cFileName);

		DWORD dw = DeleteTreeNoFollow(child);
		if (dw != ERROR_SUCCESS) {
			LogWarn(L"[Cleanup] Failed to delete subdir: %ls (%lu)", child.c_str(), dw);
		}
	} while (FindNextFileW(hFind, &fd));

	FindClose(hFind);
}

static void CleanupAllowedPathSubdirs() {
	for (const auto& entry : AllowedPaths) {
		DeleteSubdirectoriesOfRoot(entry.path);
	}
}

static std::wstring JoinPath(const std::wstring& base, const std::wstring& leaf) {
	if (base.empty()) return leaf;
	if (leaf.empty()) return base;
	std::wstring out = base;
	if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
	if (leaf.front() == L'\\' || leaf.front() == L'/') out.append(leaf.substr(1));
	else out.append(leaf);
	return out;
}

static std::wstring GetFileNamePart(const std::wstring& path) {
	if (path.empty()) return L"";
	size_t pos = path.find_last_of(L"\\/");
	if (pos == std::wstring::npos) return path;
	return path.substr(pos + 1);
}

static std::wstring GetParentDir(const std::wstring& path) {
	if (path.empty()) return L"";
	std::wstring p = path;
	std::replace(p.begin(), p.end(), L'/', L'\\');
	while (p.size() > 3 && !p.empty() && p.back() == L'\\') p.pop_back();
	size_t pos = p.find_last_of(L'\\');
	if (pos == std::wstring::npos) return L"";
	if (pos == 2 && p[1] == L':') return p.substr(0, 3);
	if (pos == 0) return p.substr(0, 1);
	return p.substr(0, pos);
}

static std::wstring ParseImagePathFromCommandLine(PCWSTR cmdLine) {
	if (!cmdLine) return L"";
	const wchar_t* p = cmdLine;
	while (*p && IsSpace(*p)) ++p;
	if (*p == L'\0') return L"";
	if (*p == L'"') {
		++p;
		const wchar_t* begin = p;
		while (*p && *p != L'"') ++p;
		return std::wstring(begin, p - begin);
	}
	const wchar_t* begin = p;
	while (*p && !IsSpace(*p)) ++p;
	return std::wstring(begin, p - begin);
}

static bool LooksLikeBunImage(const std::wstring& imagePath) {
	std::wstring name = ToLowerCopy(GetFileNamePart(imagePath));
	return name == L"bun" || name == L"bun.exe";
}

static bool CommandLineMentionsBun(PCWSTR cmdLine) {
	if (!cmdLine) return false;
	std::wstring lower = ToLowerCopy(std::wstring(cmdLine));
	if (lower.find(L"bun.exe") != std::wstring::npos) return true;
	if (lower == L"bun") return true;
	if (lower.rfind(L"bun ", 0) == 0) return true;
	if (lower.find(L" bun ") != std::wstring::npos) return true;
	if (lower.find(L"\\bun ") != std::wstring::npos) return true;
	if (lower.find(L"/bun ") != std::wstring::npos) return true;
	return false;
}

static bool LooksLikeOpenCodeImage(const std::wstring& imagePath) {
	std::wstring name = ToLowerCopy(GetFileNamePart(imagePath));
	return name == L"opencode" || name == L"opencode.exe";
}

static std::wstring GetFirstArgTokenLower(PCWSTR cmdLine) {
	if (!cmdLine) return L"";
	const wchar_t* p = cmdLine;
	while (*p && IsSpace(*p)) ++p;
	if (*p == L'\0') return L"";
	if (*p == L'"') {
		++p;
		while (*p && *p != L'"') ++p;
		if (*p == L'"') ++p;
	}
	else {
		while (*p && !IsSpace(*p)) ++p;
	}
	while (*p && IsSpace(*p)) ++p;
	if (*p == L'\0') return L"";
	std::wstring token;
	if (*p == L'"') {
		++p;
		const wchar_t* begin = p;
		while (*p && *p != L'"') ++p;
		token.assign(begin, p - begin);
	}
	else {
		const wchar_t* begin = p;
		while (*p && !IsSpace(*p)) ++p;
		token.assign(begin, p - begin);
	}
	return ToLowerCopy(token);
}

static bool IsOpenCodeTuiInvocation(PCWSTR cmdLine) {
	if (!cmdLine) return false;
	std::wstring image = ParseImagePathFromCommandLine(cmdLine);
	if (!LooksLikeOpenCodeImage(image)) return false;
	std::wstring sub = GetFirstArgTokenLower(cmdLine);
	if (sub.empty()) return true;
	if (sub == L"--help" || sub == L"-h" ||
		sub == L"--version" || sub == L"-v" ||
		sub == L"config" || sub == L"auth" ||
		sub == L"model" || sub == L"provider" ||
		sub == L"mcp" || sub == L"rules" ||
		sub == L"completion" || sub == L"doctor") {
		return false;
	}
	return true;
}

static bool IsConsoleInteractiveSession() {
	DWORD mode = 0;
	HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hIn == INVALID_HANDLE_VALUE || hOut == INVALID_HANDLE_VALUE) return false;
	if (!GetConsoleMode(hIn, &mode)) return false;
	if (!GetConsoleMode(hOut, &mode)) return false;
	return true;
}

static void AutoEnableWaitForTuiIfNeeded() {
	if (WaitForExit) return;
	if (!ExeToLaunch) return;
	if (!IsConsoleInteractiveSession()) return;
	if (IsOpenCodeTuiInvocation(ExeToLaunch)) {
		WaitForExit = true;
		g_WaitAutoEnabled = true;
		LogWarn(L"wait=false but OpenCode TUI detected; auto-enabled wait=true.");
	}
}

// ========================================================================
// Shell Compatibility
// ========================================================================
static void UpsertEnvOverride(const std::wstring& key, const std::wstring& value);

static bool IsMsys2OrCygwinPath(const std::wstring& path) {
	if (path.empty()) return false;
	std::wstring lower = ToLowerCopy(path);
	if (lower.find(L"\\git\\") != std::wstring::npos &&
		lower.find(L"bash") != std::wstring::npos) return true;
	if (lower.find(L"\\msys") != std::wstring::npos) return true;
	if (lower.find(L"\\cygwin") != std::wstring::npos) return true;
	return false;
}

static void FixShellForSandbox() {
	std::wstring currentShell = GetEnvVarCopy(L"SHELL");
	std::wstring comspec = GetEnvVarCopy(L"COMSPEC");
	std::wstring safeShell = comspec.empty() ? L"C:\\Windows\\System32\\cmd.exe" : comspec;

	if (!currentShell.empty() && IsMsys2OrCygwinPath(currentShell)) {
		LogWarn(L"[ShellCompat] SHELL=%ls is MSYS2/Cygwin, overriding with cmd.exe.", currentShell.c_str());
		UpsertEnvOverride(L"SHELL", safeShell);
		UpsertEnvOverride(L"COMSPEC", safeShell);
		return;
	}
	if (currentShell.empty()) {
		UpsertEnvOverride(L"SHELL", safeShell);
		return;
	}
}

static bool ResolveSubstPath(const std::wstring& inputPath, std::wstring* resolvedPath) {
	if (!resolvedPath) return false;
	std::wstring p = NormalizePathForCompare(inputPath);
	if (p.size() < 2 || p[1] != L':' || !iswalpha(p[0])) return false;

	wchar_t drive[3] = { static_cast<wchar_t>(towupper(p[0])), L':', L'\0' };
	wchar_t target[4096] = {};
	DWORD n = QueryDosDeviceW(drive, target, _countof(target));
	if (n == 0) return false;

	std::wstring firstTarget(target);
	const std::wstring dosPrefix = L"\\??\\";
	if (firstTarget.rfind(dosPrefix, 0) != 0) return false;

	std::wstring mapped = firstTarget.substr(dosPrefix.size());
	if (mapped.size() < 2 || mapped[1] != L':') return false;

	std::wstring tail = p.substr(2);
	*resolvedPath = mapped + tail;
	return true;
}

static void ResolveExePathIfSubst() {
	if (!ExeToLaunch) return;
	std::wstring image = ParseImagePathFromCommandLine(ExeToLaunch);
	if (image.empty()) return;

	std::wstring resolved;
	if (ResolveSubstPath(image, &resolved)) {
		if (!PathEqualsInsensitive(image, resolved)) {
			LogWarn(L"[SubstResolve] Executable is on SUBST drive: '%ls' -> '%ls'", image.c_str(), resolved.c_str());

			const wchar_t* p = ExeToLaunch;
			while (*p && IsSpace(*p)) ++p;

			std::wstring argsSuffix;
			bool wasQuoted = false;

			if (*p == L'"') {
				wasQuoted = true;
				++p;
				const wchar_t* end = p;
				while (*end && *end != L'"') ++end;
				if (*end == L'"') argsSuffix = (end + 1);
				else argsSuffix = end;
			}
			else {
				const wchar_t* end = p;
				while (*end && !IsSpace(*end)) ++end;
				argsSuffix = end;
			}

			std::wstring newCmd;
			bool needsQuotes = wasQuoted || (resolved.find(L' ') != std::wstring::npos);
			if (needsQuotes) newCmd = L"\"" + resolved + L"\"";
			else newCmd = resolved;
			newCmd += argsSuffix;

			g_CmdLineBuf.assign(newCmd.begin(), newCmd.end());
			g_CmdLineBuf.push_back(L'\0');
			ExeToLaunch = g_CmdLineBuf.data();
		}
	}
}

static void UpsertEnvOverride(const std::wstring& key, const std::wstring& value) {
	if (key.empty() || key.find(L'=') != std::wstring::npos) return;
	for (auto& kv : EnvOverrides) {
		if (IEquals(kv.name, key)) { kv.value = value; return; }
	}
	EnvOverrides.push_back(EnvKV{ key, value });
}

// ========================================================================
// Default OpenCode Environment Paths
// ========================================================================
static std::wstring GetExeDir();

static void SetEnvDefaultIfAbsent(const std::wstring& key, const std::wstring& value) {
	for (const auto& kv : EnvOverrides) {
		if (IEquals(kv.name, key)) return;
	}
	EnvOverrides.push_back(EnvKV{ key, value });
}

static void ApplyDefaultOpenCodeEnvPaths() {
	std::wstring exeDir = GetExeDir();
	if (exeDir.empty()) return;
	while (exeDir.size() > 3 && exeDir.back() == L'\\') exeDir.pop_back();

	std::wstring baseDir = JoinPath(exeDir, L"opencode");
	std::wstring workDir = JoinPath(baseDir, L"work");
	std::wstring configDir = JoinPath(baseDir, L"config");
	std::wstring cacheDir = JoinPath(baseDir, L"cache");
	std::wstring dataDir = JoinPath(baseDir, L"data");
	std::wstring tempDir = JoinPath(baseDir, L"temp");

	CreateDirectoryW(baseDir.c_str(), nullptr);
	CreateDirectoryW(workDir.c_str(), nullptr);
	CreateDirectoryW(configDir.c_str(), nullptr);
	CreateDirectoryW(cacheDir.c_str(), nullptr);
	CreateDirectoryW(dataDir.c_str(), nullptr);
	CreateDirectoryW(tempDir.c_str(), nullptr);

	SetEnvDefaultIfAbsent(L"HOME", workDir);
	SetEnvDefaultIfAbsent(L"USERPROFILE", workDir);
	SetEnvDefaultIfAbsent(L"APPDATA", configDir);
	SetEnvDefaultIfAbsent(L"LOCALAPPDATA", dataDir);
	SetEnvDefaultIfAbsent(L"TEMP", tempDir);
	SetEnvDefaultIfAbsent(L"TMP", tempDir);
	SetEnvDefaultIfAbsent(L"XDG_CONFIG_HOME", configDir);
	SetEnvDefaultIfAbsent(L"XDG_CACHE_HOME", cacheDir);
	SetEnvDefaultIfAbsent(L"XDG_DATA_HOME", dataDir);
	SetEnvDefaultIfAbsent(L"OPENCODE_CONFIG", JoinPath(baseDir, L"opencode.json"));

	std::wstring dirs[] = { baseDir, workDir, configDir, cacheDir, dataDir, tempDir };
	for (const auto& d : dirs) AddAllowedPathUnique(d);
}

static void ApplyBunOpenTuiCompatibility() {
	if (!ExeToLaunch) return;
	std::wstring imagePath = ParseImagePathFromCommandLine(ExeToLaunch);
	bool bunDetected = LooksLikeBunImage(imagePath) || CommandLineMentionsBun(ExeToLaunch);
	if (!bunDetected) return;

	std::wstring bunInstall = TrimCopy(GetEnvVarCopy(L"BUN_INSTALL"));
	if (bunInstall.empty() && !imagePath.empty()) {
		std::wstring imageDir = GetParentDir(imagePath);
		if (IEquals(GetFileNamePart(imageDir), L"bin")) {
			bunInstall = GetParentDir(imageDir);
		}
	}
	if (bunInstall.empty()) {
		LogWarn(L"[BunCompat] bun detected, but BUN_INSTALL is empty. Skip auto path fix.");
		return;
	}

	auto applyBase = [&](const std::wstring& base, PCWSTR sourceTag) {
		if (base.empty()) return;
		std::wstring rootPath = JoinPath(base, L"root");
		std::wstring binPath = JoinPath(base, L"bin");
		if (DirectoryExists(rootPath)) {
			AddAllowedPathUnique(rootPath);
			AddPathPrependUnique(rootPath);
		}
		if (DirectoryExists(binPath)) AddPathPrependUnique(binPath);
		};

	applyBase(bunInstall, L"[BunCompat]");

	std::wstring resolvedInstall;
	if (ResolveSubstPath(bunInstall, &resolvedInstall) &&
		!PathEqualsInsensitive(bunInstall, resolvedInstall) &&
		DirectoryExists(resolvedInstall)) {
		UpsertEnvOverride(L"BUN_INSTALL", resolvedInstall);
		applyBase(resolvedInstall, L"[BunCompat]");
	}
}

// ========================================================================
// Argument / Config Parsing
// ========================================================================

static PathAccessLevel ParseAccessLevel(const std::wstring& pathWithLevel, std::wstring& outPath) {
	outPath = pathWithLevel;
	size_t colonPos = pathWithLevel.rfind(L':');
	if (colonPos != std::wstring::npos && colonPos > 1 && colonPos + 1 < pathWithLevel.size()) {
		std::wstring levelStr = pathWithLevel.substr(colonPos + 1);
		outPath = TrimCopy(pathWithLevel.substr(0, colonPos));
		if (levelStr == L"R" || levelStr == L"r") return PathAccessLevel::ReadOnly;
		else if (levelStr == L"RX" || levelStr == L"rx") return PathAccessLevel::ReadExecute;
		else if (levelStr == L"RW" || levelStr == L"rw") return PathAccessLevel::ReadWrite;
		else if (levelStr == L"F" || levelStr == L"f") return PathAccessLevel::FullControl;
		else outPath = pathWithLevel;
	}
	return PathAccessLevel::FullControl;
}

static bool ParseAllowedPathList(WCHAR* paths) {
	if (!paths) return true;
	std::wstring pathsStr = TrimCopy(paths);
	if (pathsStr.size() >= 2 && pathsStr.front() == L'"' && pathsStr.back() == L'"') {
		pathsStr = pathsStr.substr(1, pathsStr.size() - 2);
	}
	std::vector<wchar_t> buf(pathsStr.begin(), pathsStr.end());
	buf.push_back(L'\0');
	WCHAR* ctx = nullptr;
	for (WCHAR* tok = wcstok_s(buf.data(), L";,", &ctx); tok != nullptr; tok = wcstok_s(nullptr, L";,", &ctx)) {
		std::wstring raw = TrimCopy(tok);
		if (raw.size() >= 2 && raw.front() == L'"' && raw.back() == L'"') {
			raw = raw.substr(1, raw.size() - 2);
		}
		if (!raw.empty()) {
			std::wstring path;
			PathAccessLevel level = ParseAccessLevel(raw, path);
			if (!path.empty()) AddAllowedPathUnique(path, level);
		}
	}
	return true;
}

static bool ParseEnvList(WCHAR* env) {
	if (!env) return true;
	WCHAR* ctx = nullptr;
	for (WCHAR* tok = wcstok_s(env, L";", &ctx); tok != nullptr; tok = wcstok_s(nullptr, L";", &ctx)) {
		std::wstring pair = TrimCopy(tok);
		if (pair.empty()) continue;
		size_t eq = pair.find(L'=');
		std::wstring k = TrimCopy(eq == std::wstring::npos ? pair : pair.substr(0, eq));
		std::wstring v = TrimCopy(eq == std::wstring::npos ? L"" : pair.substr(eq + 1));
		if (!k.empty() && k.find(L'=') == std::wstring::npos) {
			EnvOverrides.push_back(EnvKV{ k, v });
		}
		else {
			LogWarn(L"Ignored invalid env token: %ls", pair.c_str());
		}
	}
	return true;
}

static bool ParsePathPrependList(WCHAR* paths) {
	if (!paths) return true;
	WCHAR* ctx = nullptr;
	for (WCHAR* tok = wcstok_s(paths, L";", &ctx); tok != nullptr; tok = wcstok_s(nullptr, L";", &ctx)) {
		std::wstring p = TrimCopy(tok);
		if (!p.empty()) AddPathPrependUnique(p);
	}
	return true;
}

static bool ParseAllowedPathListFromArg(PCWSTR arg) {
	if (!arg) return true;
	std::vector<wchar_t> buf(arg, arg + wcslen(arg));
	buf.push_back(L'\0');
	return ParseAllowedPathList(buf.data());
}

static bool ParseEnvListFromArg(PCWSTR arg) {
	if (!arg) return true;
	std::vector<wchar_t> buf(arg, arg + wcslen(arg));
	buf.push_back(L'\0');
	return ParseEnvList(buf.data());
}

static bool ParsePathPrependListFromArg(PCWSTR arg) {
	if (!arg) return true;
	std::vector<wchar_t> buf(arg, arg + wcslen(arg));
	buf.push_back(L'\0');
	return ParsePathPrependList(buf.data());
}

static void PrintUsage() {
	wprintf(L"LaunchSandbox.exe Usage:\r\n");
	wprintf(L"\t-m Moniker -i ExeToLaunch -a Path1;Path2 -e K1=V1;K2=V2 -p Dir1;Dir2 -s -w -x\r\n");
	wprintf(L"\r\n");
	wprintf(L"Required:\r\n");
	wprintf(L"\t-i : Command line of exe to launch\r\n");
	wprintf(L"\t-m : Package moniker (identifier)\r\n");
	wprintf(L"\r\n");
	wprintf(L"Optional:\r\n");
	wprintf(L"\t-d : Display name\r\n");
	wprintf(L"\t-a : Semicolon-separated directories to grant (F)\r\n");
	wprintf(L"\t-e : Semicolon-separated env pairs (K=V;K2=V2)\r\n");
	wprintf(L"\t-p : Semicolon-separated path prepend list\r\n");
	wprintf(L"\t-s : Skip setting Low Integrity on -a paths\r\n");
	wprintf(L"\t-w : Wait for child exit\r\n");
	wprintf(L"\t-x : Cleanup: recursively delete subdirectories under -a paths after exit\r\n");
	wprintf(L"\t-g : Enable logging (console + file)\r\n");
	wprintf(L"\r\n");
	wprintf(L"Notes:\r\n");
	wprintf(L"\t- Uses Restricted Token sandbox with configurable integrity level.\r\n");
	wprintf(L"\t- config.ini [App] is auto-loaded when no CLI args.\r\n");
	wprintf(L"\t- For bun/bun.exe, %%BUN_INSTALL%%\\root is auto-added to allowPaths and PATH.\r\n");
	wprintf(L"\r\n");
	wprintf(L"Network Filter (config.ini only):\r\n");
	wprintf(L"\tnetworkFilterEnabled = true|false\r\n");
	wprintf(L"\tnetworkFilterPort = 8080\r\n");
	wprintf(L"\tnetworkFilterAllowedUrls = *.example.com;api.other.com\r\n");
}

static bool ParseArguments(int argc, WCHAR** argv) {
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] != L'-' && argv[i][0] != L'/') continue;

		switch (argv[i][1]) {
		case L'i':
			if (i + 1 >= argc) { PrintUsage(); return false; }
			ExeToLaunch = argv[++i];
			break;
		case L'm':
			if (i + 1 >= argc) { PrintUsage(); return false; }
			PackageMoniker = argv[++i];
			break;
		case L'd':
			if (i + 1 >= argc) { PrintUsage(); return false; }
			PackageDisplayName = argv[++i];
			break;
		case L'a':
			if (i + 1 >= argc) { PrintUsage(); return false; }
			if (!ParseAllowedPathListFromArg(argv[++i])) return false;
			break;
		case L'e':
			if (i + 1 >= argc) { PrintUsage(); return false; }
			if (!ParseEnvListFromArg(argv[++i])) return false;
			break;
		case L'p':
			if (i + 1 >= argc) { PrintUsage(); return false; }
			if (!ParsePathPrependListFromArg(argv[++i])) return false;
			break;
		case L's': PathLowIntegrity = false; break;
		case L'w': WaitForExit = true; break;
		case L'x': CleanupAllowedSubdirs = true; break;
		case L'g': g_LogEnabled = true; break;
		default:
			LogWarn(L"[Args] Unknown option: %ls", argv[i]);
			break;
		}
	}
	return true;
}

// ========================================================================
// Security / ACL Management
// ========================================================================
static bool SidAlreadyHasAccess(PCWSTR path, PSID sid, DWORD requiredMask) {
	PSECURITY_DESCRIPTOR sd = nullptr;
	PACL dacl = nullptr;
	DWORD dw = GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, &dacl, nullptr, &sd);
	if (dw != ERROR_SUCCESS || !dacl) {
		if (sd) LocalFree(sd);
		return false;
	}

	ACL_SIZE_INFORMATION aclInfo{};
	if (!GetAclInformation(dacl, &aclInfo, sizeof(aclInfo), AclSizeInformation)) {
		LocalFree(sd);
		return false;
	}

	bool found = false;
	for (DWORD i = 0; i < aclInfo.AceCount; ++i) {
		ACCESS_ALLOWED_ACE* ace = nullptr;
		if (!GetAce(dacl, i, reinterpret_cast<LPVOID*>(&ace))) continue;
		if (ace->Header.AceType != ACCESS_ALLOWED_ACE_TYPE) continue;
		if (!EqualSid(reinterpret_cast<PSID>(&ace->SidStart), sid)) continue;
		if ((ace->Mask & requiredMask) == requiredMask) {
			found = true;
			break;
		}
	}
	LocalFree(sd);
	return found;
}

static DWORD AccessMaskFromLevel(PathAccessLevel level) {
	switch (level) {
	case PathAccessLevel::ReadOnly:    return FILE_GENERIC_READ;
	case PathAccessLevel::ReadExecute: return FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
	case PathAccessLevel::ReadWrite:   return FILE_GENERIC_READ | FILE_GENERIC_WRITE;
	case PathAccessLevel::FullControl:
	default:                           return FILE_ALL_ACCESS;
	}
}

static bool PathHasLowIntegrityLabel(PCWSTR path) {
	PSECURITY_DESCRIPTOR sd = nullptr;
	PACL sacl = nullptr;
	DWORD dw = GetNamedSecurityInfoW(path, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION,
		nullptr, nullptr, nullptr, &sacl, &sd);
	if (dw != ERROR_SUCCESS || !sacl) {
		if (sd) LocalFree(sd);
		return false;
	}

	ACL_SIZE_INFORMATION aclInfo{};
	if (!GetAclInformation(sacl, &aclInfo, sizeof(aclInfo), AclSizeInformation)) {
		LocalFree(sd);
		return false;
	}

	PSID pLowSid = nullptr;
	if (!ConvertStringSidToSid(L"S-1-16-4096", &pLowSid)) {
		LocalFree(sd);
		return false;
	}

	bool found = false;
	for (DWORD i = 0; i < aclInfo.AceCount; ++i) {
		SYSTEM_MANDATORY_LABEL_ACE* ace = nullptr;
		if (!GetAce(sacl, i, reinterpret_cast<LPVOID*>(&ace))) continue;
		if (ace->Header.AceType != SYSTEM_MANDATORY_LABEL_ACE_TYPE) continue;
		if (EqualSid(reinterpret_cast<PSID>(&ace->SidStart), pLowSid)) {
			found = true;
			break;
		}
	}

	LocalFree(pLowSid);
	LocalFree(sd);
	return found;
}

static DWORD GrantAccessToSidOnPath(PCWSTR path, PSID sid, PathAccessLevel level) {
	DWORD requiredMask = AccessMaskFromLevel(level);
	if (SidAlreadyHasAccess(path, sid, requiredMask)) {
		LogInfo(L"[ACL] SID already has required access on %ls, skipping.", path);
		return ERROR_SUCCESS;
	}

	PSECURITY_DESCRIPTOR sd = nullptr;
	PACL oldDacl = nullptr;
	DWORD dw = GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, &oldDacl, nullptr, &sd);
	if (dw != ERROR_SUCCESS) {
		LogError(L"[ACL] GetNamedSecurityInfoW failed on %ls: err=%lu", path, dw);
		return dw;
	}

	EXPLICIT_ACCESSW ea{};
	ea.grfAccessPermissions = requiredMask;
	ea.grfAccessMode = GRANT_ACCESS;
	ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
	BuildTrusteeWithSidW(&ea.Trustee, sid);

	PACL newDacl = nullptr;
	dw = SetEntriesInAclW(1, &ea, oldDacl, &newDacl);
	if (dw != ERROR_SUCCESS) {
		LogError(L"[ACL] SetEntriesInAclW failed on %ls: err=%lu", path, dw);
		if (sd) LocalFree(sd);
		return dw;
	}

	dw = SetNamedSecurityInfoW(const_cast<LPWSTR>(path), SE_FILE_OBJECT,
		DACL_SECURITY_INFORMATION, nullptr, nullptr, newDacl, nullptr);
	if (dw != ERROR_SUCCESS) {
		LogError(L"[ACL] SetNamedSecurityInfoW (DACL) failed on %ls: err=%lu", path, dw);
	}

	if (newDacl) LocalFree(newDacl);
	if (sd) LocalFree(sd);
	return dw;
}

static DWORD GrantFullControlToSidOnPath(PCWSTR path, PSID sid) {
	return GrantAccessToSidOnPath(path, sid, PathAccessLevel::FullControl);
}

static DWORD SetLowIntegrityLabel(PCWSTR path) {
	if (PathHasLowIntegrityLabel(path)) {
		LogInfo(L"[ACL] Low integrity label already set on %ls, skipping.", path);
		return ERROR_SUCCESS;
	}

	PSECURITY_DESCRIPTOR sd = nullptr;
	if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
		L"S:(ML;OICI;NW;;;LW)", SDDL_REVISION_1, &sd, nullptr)) {
		DWORD err = GetLastError();
		LogError(L"[ACL] ConvertStringSecurityDescriptorToSecurityDescriptorW failed: err=%lu", err);
		return err;
	}

	PACL sacl = nullptr;
	BOOL present = FALSE, defaulted = FALSE;
	if (!GetSecurityDescriptorSacl(sd, &present, &sacl, &defaulted)) {
		DWORD err = GetLastError();
		LogError(L"[ACL] GetSecurityDescriptorSacl failed: err=%lu", err);
		LocalFree(sd);
		return err;
	}

	DWORD dw = SetNamedSecurityInfoW(const_cast<LPWSTR>(path), SE_FILE_OBJECT,
		LABEL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, sacl);
	if (dw != ERROR_SUCCESS) {
		LogWarn(L"[ACL] SetNamedSecurityInfoW (SACL/LowIntegrity) failed on %ls: err=%lu (needs elevation)", path, dw);
	}

	LocalFree(sd);
	return dw;
}

static DWORD RestoreDaclWithProtection(const SavedSecurity& ss) {
	if (!ss.hasDacl) return ERROR_INVALID_PARAMETER;
	DWORD flags = DACL_SECURITY_INFORMATION;
	flags |= ss.daclProtected ? PROTECTED_DACL_SECURITY_INFORMATION : UNPROTECTED_DACL_SECURITY_INFORMATION;
	return SetNamedSecurityInfoW(const_cast<LPWSTR>(ss.path.c_str()), SE_FILE_OBJECT,
		flags, nullptr, nullptr, ss.dacl, nullptr);
}

static DWORD SaveOriginalSecurityForPath(PCWSTR path) {
	SavedSecurity ss{};
	ss.path = path;

	DWORD daclErr = GetNamedSecurityInfoW(path, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
		nullptr, nullptr, &ss.dacl, nullptr, &ss.sdDacl);
	if (daclErr == ERROR_SUCCESS) {
		ss.hasDacl = true;
		SECURITY_DESCRIPTOR_CONTROL ctrl = 0;
		DWORD rev = 0;
		if (GetSecurityDescriptorControl(ss.sdDacl, &ctrl, &rev)) {
			ss.daclProtected = (ctrl & SE_DACL_PROTECTED) != 0;
		}
	}

	DWORD saclErr = GetNamedSecurityInfoW(path, SE_FILE_OBJECT, LABEL_SECURITY_INFORMATION,
		nullptr, nullptr, nullptr, &ss.sacl, &ss.sdSacl);
	if (saclErr == ERROR_SUCCESS) ss.hasSacl = true;

	g_SavedSecurity.push_back(std::move(ss));
	return (daclErr == ERROR_SUCCESS || saclErr == ERROR_SUCCESS) ? ERROR_SUCCESS
		: (daclErr != ERROR_SUCCESS ? daclErr : saclErr);
}

static void RestoreSavedSecurity() {
	int restoredDacl = 0, restoredSacl = 0, failedDacl = 0, failedSacl = 0;
	for (auto& ss : g_SavedSecurity) {
		if (ss.hasDacl) {
			DWORD dw = RestoreDaclWithProtection(ss);
			if (dw == ERROR_SUCCESS) ++restoredDacl;
			else { LogWarn(L"[Revert] Failed to restore DACL on %ls (err=%lu)", ss.path.c_str(), dw); ++failedDacl; }
		}
		if (ss.hasSacl) {
			DWORD dw = SetNamedSecurityInfoW(const_cast<LPWSTR>(ss.path.c_str()), SE_FILE_OBJECT,
				LABEL_SECURITY_INFORMATION, nullptr, nullptr, nullptr, ss.sacl);
			if (dw == ERROR_SUCCESS) ++restoredSacl;
			else { LogWarn(L"[Revert] Failed to restore Label SACL on %ls (err=%lu)", ss.path.c_str(), dw); ++failedSacl; }
		}
	}
	g_SavedSecurity.clear();
	LogInfo(L"[Revert] Security restore: DACL ok=%d fail=%d, SACL ok=%d fail=%d",
		restoredDacl, failedDacl, restoredSacl, failedSacl);
}

static void RestoreSavedSecurityOnce() {
	if (InterlockedCompareExchange(&g_RestoreDone, 1, 0) != 0) return;
	if (InterlockedCompareExchange(&g_PathAclModified, 0, 0) == 1) {
		RestoreSavedSecurity();
	}
}

static void GrantAccessToAllowedPaths(PSID sid) {
	int grantedCount = 0, skippedCount = 0, failedCount = 0;

	for (const auto& entry : AllowedPaths) {
		DWORD attrs = GetFileAttributesW(entry.path.c_str());
		if (attrs == INVALID_FILE_ATTRIBUTES) {
			LogWarn(L"[Permissions] Skip path (not found): %ls", entry.path.c_str());
			++skippedCount;
			continue;
		}
		if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
			LogWarn(L"[Permissions] Skip path (not directory): %ls", entry.path.c_str());
			++skippedCount;
			continue;
		}

		DWORD requiredMask = AccessMaskFromLevel(entry.accessLevel);
		bool daclNeeded = !SidAlreadyHasAccess(entry.path.c_str(), sid, requiredMask);
		bool saclNeeded = PathLowIntegrity && !PathHasLowIntegrityLabel(entry.path.c_str());
		if (!daclNeeded && !saclNeeded) {
			LogInfo(L"[Permissions] ACLs already correct on %ls, skipping.", entry.path.c_str());
			++skippedCount;
			continue;
		}

		SaveOriginalSecurityForPath(entry.path.c_str());
		SavedSecurity* backup = g_SavedSecurity.empty() ? nullptr : &g_SavedSecurity.back();
		bool modifiedThisPath = false;

		if (backup && backup->hasDacl) {
			DWORD dw = GrantAccessToSidOnPath(entry.path.c_str(), sid, entry.accessLevel);
			if (dw == ERROR_SUCCESS) {
				modifiedThisPath = true;
			}
			else {
				LogWarn(L"[Permissions] Grant failed on %ls (err=%lu)", entry.path.c_str(), dw);
				++failedCount;
			}
		}

		if (PathLowIntegrity && backup && backup->hasSacl) {
			DWORD dw = SetLowIntegrityLabel(entry.path.c_str());
			if (dw == ERROR_SUCCESS) {
				modifiedThisPath = true;
			}
			else {
				if (dw == ERROR_ACCESS_DENIED)
					LogWarn(L"[Permissions] Set Low Integrity DENIED on %ls. Run elevated or set lowIntegrityOnPaths=false.", entry.path.c_str());
				else
					LogWarn(L"[Permissions] Set Low Integrity failed on %ls (err=%lu)", entry.path.c_str(), dw);
				++failedCount;
			}

			if (entry.accessLevel == PathAccessLevel::ReadWrite || entry.accessLevel == PathAccessLevel::FullControl) {
				PSID pLowIntegritySid = nullptr;
				if (ConvertStringSidToSid(L"S-1-16-4096", &pLowIntegritySid)) {
					DWORD mask = AccessMaskFromLevel(entry.accessLevel);
					if (!SidAlreadyHasAccess(entry.path.c_str(), pLowIntegritySid, mask)) {
						DWORD dwLow = GrantAccessToSidOnPath(entry.path.c_str(), pLowIntegritySid, entry.accessLevel);
						if (dwLow == ERROR_SUCCESS)
							LogInfo(L"[Permissions] Low-IL write access granted on %ls", entry.path.c_str());
						else
							LogWarn(L"[Permissions] Failed to grant Low-IL write access on %ls (err=%lu)", entry.path.c_str(), dwLow);
					}
					LocalFree(pLowIntegritySid);
				}
			}
		}

		if (modifiedThisPath) {
			InterlockedExchange(&g_PathAclModified, 1);
			++grantedCount;
		}
		else {
			if (!g_SavedSecurity.empty()) {
				auto& ss = g_SavedSecurity.back();
				if (ss.sdDacl) { LocalFree(ss.sdDacl); ss.sdDacl = nullptr; }
				if (ss.sdSacl) { LocalFree(ss.sdSacl); ss.sdSacl = nullptr; }
				g_SavedSecurity.pop_back();
			}
		}
	}

	LogInfo(L"[Permissions] Summary: granted=%d, skipped=%d, failed=%d (total=%llu)",
		grantedCount, skippedCount, failedCount, (unsigned long long)AllowedPaths.size());
}

// ========================================================================
// Bun Standalone Virtual Drive (B:\~BUN) Support
// ========================================================================

static std::wstring FindDllNameInBinary(const BYTE* data, size_t dataSize) {
	const char* pat = "opentui-";
	size_t patLen = 8;
	for (size_t i = 0; i + patLen + 4 < dataSize; ++i) {
		if (memcmp(data + i, pat, patLen) != 0) continue;
		size_t j = i + patLen;
		while (j < dataSize && j - i < 80) {
			BYTE ch = data[j];
			if (ch == '.') break;
			if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
				(ch >= '0' && ch <= '9') || ch == '_' || ch == '-') {
				++j;
			}
			else break;
		}
		if (j + 4 <= dataSize && memcmp(data + j, ".dll", 4) == 0) {
			size_t nameLen = (j + 4) - i;
			std::string name(reinterpret_cast<const char*>(data + i), nameLen);
			return std::wstring(name.begin(), name.end());
		}
	}
	return L"";
}

static DWORD RvaToFileOffset(const BYTE* peBase, size_t peSize, DWORD rva) {
	if (peSize < 0x40 || rva == 0) return 0;
	DWORD peOff = *reinterpret_cast<const DWORD*>(peBase + 0x3C);
	if (peOff < 0x40 || peOff + 4 + 20 > peSize) return 0;
	size_t coffOff = peOff + 4;
	WORD numSections = *reinterpret_cast<const WORD*>(peBase + coffOff + 2);
	WORD optSize = *reinterpret_cast<const WORD*>(peBase + coffOff + 16);
	size_t secTableOff = coffOff + 20 + optSize;
	if (numSections > 96 || secTableOff + numSections * 40 > peSize) return 0;

	for (WORD s = 0; s < numSections; ++s) {
		size_t sh = secTableOff + s * 40;
		DWORD secVA = *reinterpret_cast<const DWORD*>(peBase + sh + 12);
		DWORD secRawSize = *reinterpret_cast<const DWORD*>(peBase + sh + 16);
		DWORD secRawPtr = *reinterpret_cast<const DWORD*>(peBase + sh + 20);
		DWORD secVSize = *reinterpret_cast<const DWORD*>(peBase + sh + 8);
		DWORD secExtent = (secRawSize > secVSize) ? secRawSize : secVSize;
		if (rva >= secVA && rva < secVA + secExtent) {
			DWORD offset = secRawPtr + (rva - secVA);
			if (offset < peSize) return offset;
		}
	}

	DWORD sizeOfHeaders = 0;
	size_t optOff = coffOff + 20;
	if (optOff + 64 <= peSize) {
		sizeOfHeaders = *reinterpret_cast<const DWORD*>(peBase + optOff + 60);
	}
	if (rva < sizeOfHeaders && rva < peSize) return rva;
	return 0;
}

static bool PEDllExportsSymbol(const BYTE* peBase, size_t peSize,
	const char* symbolName, int* outExportCount = nullptr) {
	if (outExportCount) *outExportCount = 0;
	if (!peBase || peSize < 0x200 || !symbolName) return false;
	if (peBase[0] != 'M' || peBase[1] != 'Z') return false;

	DWORD peOff = *reinterpret_cast<const DWORD*>(peBase + 0x3C);
	if (peOff + 4 + 20 > peSize) return false;
	size_t coffOff = peOff + 4;
	WORD optSize = *reinterpret_cast<const WORD*>(peBase + coffOff + 16);
	size_t optOff = coffOff + 20;
	if (optOff + optSize > peSize) return false;

	WORD magic = *reinterpret_cast<const WORD*>(peBase + optOff);
	DWORD numDataDirs = 0;
	size_t dataDirOff = 0;
	if (magic == 0x010B) {
		if (optSize < 96) return false;
		numDataDirs = *reinterpret_cast<const DWORD*>(peBase + optOff + 92);
		dataDirOff = optOff + 96;
	}
	else if (magic == 0x020B) {
		if (optSize < 112) return false;
		numDataDirs = *reinterpret_cast<const DWORD*>(peBase + optOff + 108);
		dataDirOff = optOff + 112;
	}
	else return false;
	if (numDataDirs > 16) numDataDirs = 16;
	if (numDataDirs < 1) return false;

	DWORD exportRVA = *reinterpret_cast<const DWORD*>(peBase + dataDirOff);
	DWORD exportSize = *reinterpret_cast<const DWORD*>(peBase + dataDirOff + 4);
	if (exportRVA == 0 || exportSize == 0) return false;

	DWORD exportFileOff = RvaToFileOffset(peBase, peSize, exportRVA);
	if (exportFileOff == 0 || exportFileOff + 40 > peSize) return false;

	DWORD numberOfNames = *reinterpret_cast<const DWORD*>(peBase + exportFileOff + 24);
	DWORD namesRVA = *reinterpret_cast<const DWORD*>(peBase + exportFileOff + 32);
	if (outExportCount) *outExportCount = static_cast<int>(numberOfNames);
	if (numberOfNames == 0 || namesRVA == 0) return false;
	if (numberOfNames > 100000) return false;

	DWORD namesFileOff = RvaToFileOffset(peBase, peSize, namesRVA);
	if (namesFileOff == 0 || namesFileOff + numberOfNames * 4 > peSize) return false;

	size_t symbolLen = strlen(symbolName);
	for (DWORD i = 0; i < numberOfNames; ++i) {
		DWORD nameRVA = *reinterpret_cast<const DWORD*>(peBase + namesFileOff + i * 4);
		DWORD nameOff = RvaToFileOffset(peBase, peSize, nameRVA);
		if (nameOff == 0 || nameOff >= peSize) continue;
		const char* name = reinterpret_cast<const char*>(peBase + nameOff);
		size_t maxLen = peSize - nameOff;
		size_t len = strnlen(name, maxLen > 512 ? 512 : maxLen);
		if (len == symbolLen && memcmp(name, symbolName, symbolLen) == 0) return true;
	}
	return false;
}

static size_t ValidatePEDllAndGetCompleteSize(const BYTE* base, size_t maxLen) {
	if (maxLen < 0x200) return 0;
	if (base[0] != 'M' || base[1] != 'Z') return 0;

	DWORD peOff = *reinterpret_cast<const DWORD*>(base + 0x3C);
	if (peOff < 0x40 || peOff > 0x10000 || peOff + 4 + 20 > maxLen) return 0;
	if (base[peOff] != 'P' || base[peOff + 1] != 'E' ||
		base[peOff + 2] != 0 || base[peOff + 3] != 0) return 0;

	size_t coffOff = peOff + 4;
	WORD machine = *reinterpret_cast<const WORD*>(base + coffOff + 0);
	WORD numSections = *reinterpret_cast<const WORD*>(base + coffOff + 2);
	WORD optHeaderSize = *reinterpret_cast<const WORD*>(base + coffOff + 16);
	WORD characteristics = *reinterpret_cast<const WORD*>(base + coffOff + 18);

	if (!(characteristics & 0x2000)) return 0;
	if (machine != 0x014C && machine != 0x8664) return 0;
	if (numSections == 0 || numSections > 96 || optHeaderSize < 96) return 0;

	size_t optOff = coffOff + 20;
	if (optOff + optHeaderSize > maxLen) return 0;

	WORD optMagic = *reinterpret_cast<const WORD*>(base + optOff);
	bool isPE32Plus = (optMagic == 0x020B);
	bool isPE32 = (optMagic == 0x010B);
	if (!isPE32 && !isPE32Plus) return 0;
	if (isPE32Plus && machine != 0x8664) return 0;
	if (isPE32 && machine != 0x014C) return 0;

	DWORD sizeOfHeaders = *reinterpret_cast<const DWORD*>(base + optOff + 60);
	DWORD fileAlignment = *reinterpret_cast<const DWORD*>(base + optOff + 36);
	if (fileAlignment == 0 || (fileAlignment & (fileAlignment - 1)) != 0) return 0;
	if (fileAlignment < 512 || fileAlignment > 65536) return 0;

	DWORD numDataDirs = 0;
	size_t dataDirOff = 0;
	if (isPE32) {
		if (optHeaderSize < 96) return 0;
		numDataDirs = *reinterpret_cast<const DWORD*>(base + optOff + 92);
		dataDirOff = optOff + 96;
	}
	else {
		if (optHeaderSize < 112) return 0;
		numDataDirs = *reinterpret_cast<const DWORD*>(base + optOff + 108);
		dataDirOff = optOff + 112;
	}
	if (numDataDirs > 16) numDataDirs = 16;

	size_t sectionTableOff = optOff + optHeaderSize;
	if (sectionTableOff + numSections * 40 > maxLen) return 0;

	size_t totalSize = sizeOfHeaders;
	for (WORD s = 0; s < numSections; ++s) {
		size_t shOff = sectionTableOff + s * 40;
		DWORD rawSize = *reinterpret_cast<const DWORD*>(base + shOff + 16);
		DWORD rawPtr = *reinterpret_cast<const DWORD*>(base + shOff + 20);
		if (rawSize > 0 && rawPtr > 0) {
			size_t end = static_cast<size_t>(rawPtr) + rawSize;
			if (end > maxLen) return 0;
			if (end > totalSize) totalSize = end;
		}
	}

	if (numDataDirs > 4) {
		size_t certEntryOff = dataDirOff + 4 * 8;
		if (certEntryOff + 8 <= optOff + optHeaderSize) {
			DWORD certFileOffset = *reinterpret_cast<const DWORD*>(base + certEntryOff);
			DWORD certSize = *reinterpret_cast<const DWORD*>(base + certEntryOff + 4);
			if (certFileOffset > 0 && certSize > 0) {
				size_t certEnd = static_cast<size_t>(certFileOffset) + certSize;
				if (certEnd > maxLen) return 0;
				if (certEnd > totalSize) totalSize = certEnd;
			}
		}
	}

	if (numDataDirs > 6) {
		size_t dbgEntryOff = dataDirOff + 6 * 8;
		if (dbgEntryOff + 8 <= optOff + optHeaderSize) {
			DWORD dbgRVA = *reinterpret_cast<const DWORD*>(base + dbgEntryOff);
			DWORD dbgSize = *reinterpret_cast<const DWORD*>(base + dbgEntryOff + 4);
			if (dbgRVA > 0 && dbgSize > 0) {
				DWORD dbgFileOff = RvaToFileOffset(base, maxLen, dbgRVA);
				if (dbgFileOff > 0) {
					DWORD numEntries = dbgSize / 28;
					for (DWORD d = 0; d < numEntries && dbgFileOff + d * 28 + 28 <= maxLen; ++d) {
						size_t entOff = dbgFileOff + d * 28;
						DWORD dbgDataSize = *reinterpret_cast<const DWORD*>(base + entOff + 16);
						DWORD dbgDataPtr = *reinterpret_cast<const DWORD*>(base + entOff + 24);
						if (dbgDataSize > 0 && dbgDataPtr > 0) {
							size_t dbgEnd = static_cast<size_t>(dbgDataPtr) + dbgDataSize;
							if (dbgEnd <= maxLen && dbgEnd > totalSize) totalSize = dbgEnd;
						}
					}
				}
			}
		}
	}

	if (fileAlignment > 1) {
		totalSize = (totalSize + fileAlignment - 1) & ~(static_cast<size_t>(fileAlignment) - 1);
		if (totalSize > maxLen) totalSize = maxLen;
	}
	if (totalSize < 4096) return 0;
	return totalSize;
}

static bool VerifyExtractedDll(const std::wstring& dllPath, bool checkExports = true) {
	HANDLE hFile = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) return false;

	LARGE_INTEGER li{};
	GetFileSizeEx(hFile, &li);
	size_t fileSize = static_cast<size_t>(li.QuadPart);
	if (fileSize < 4096) { CloseHandle(hFile); return false; }

	HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (!hMap) { CloseHandle(hFile); return false; }

	const BYTE* base = static_cast<const BYTE*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
	if (!base) { CloseHandle(hMap); CloseHandle(hFile); return false; }

	bool valid = true;
	if (base[0] != 'M' || base[1] != 'Z') { valid = false; goto done; }
	{
		DWORD peOff = *reinterpret_cast<const DWORD*>(base + 0x3C);
		if (peOff + 4 > fileSize) { valid = false; goto done; }
		if (base[peOff] != 'P' || base[peOff + 1] != 'E') { valid = false; goto done; }
		WORD chars = *reinterpret_cast<const WORD*>(base + peOff + 4 + 18);
		if (!(chars & 0x2000)) { valid = false; goto done; }

		WORD numSections = *reinterpret_cast<const WORD*>(base + peOff + 4 + 2);
		WORD optSize = *reinterpret_cast<const WORD*>(base + peOff + 4 + 16);
		size_t secOff = peOff + 4 + 20 + optSize;
		for (WORD s = 0; s < numSections && secOff + s * 40 + 40 <= fileSize; ++s) {
			size_t sh = secOff + s * 40;
			DWORD rawSize = *reinterpret_cast<const DWORD*>(base + sh + 16);
			DWORD rawPtr = *reinterpret_cast<const DWORD*>(base + sh + 20);
			if (rawSize > 0 && rawPtr > 0) {
				if (static_cast<size_t>(rawPtr) + rawSize > fileSize) { valid = false; goto done; }
			}
		}
		if (checkExports) {
			int exportCount = 0;
			bool hasSetLogCallback = PEDllExportsSymbol(base, fileSize, "setLogCallback", &exportCount);
			if (exportCount == 0 || !hasSetLogCallback) { valid = false; goto done; }
		}
	}
done:
	UnmapViewOfFile(base);
	CloseHandle(hMap);
	CloseHandle(hFile);
	return valid;
}

struct PECandidate {
	size_t offset;
	size_t size;
	bool hasTargetExport;
	int exportCount;
};

static bool ExtractEmbeddedDllFromExe(const std::wstring& exePath,
	const std::wstring& outDir, const std::wstring& dllName, std::wstring& outPath) {
	HANDLE hFile = CreateFileW(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) return false;

	LARGE_INTEGER li{};
	if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 4096) { CloseHandle(hFile); return false; }
	size_t fileSize = static_cast<size_t>(li.QuadPart);

	HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
	if (!hMap) { CloseHandle(hFile); return false; }

	const BYTE* base = static_cast<const BYTE*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
	if (!base) { CloseHandle(hMap); CloseHandle(hFile); return false; }

	std::vector<PECandidate> candidates;
	size_t scanStart = 0x10000;
	if (scanStart >= fileSize) scanStart = 4096;

	for (size_t off = scanStart; off + 0x200 < fileSize; ++off) {
		if (base[off] != 'M' || base[off + 1] != 'Z') continue;
		size_t remaining = fileSize - off;
		size_t peSize = ValidatePEDllAndGetCompleteSize(base + off, remaining);
		if (peSize == 0) continue;
		int exportCount = 0;
		bool hasExport = PEDllExportsSymbol(base + off, peSize, "setLogCallback", &exportCount);
		candidates.push_back({ off, peSize, hasExport, exportCount });
		if (peSize > 0x200) off += peSize - 1;
	}

	if (candidates.empty()) {
		UnmapViewOfFile(base); CloseHandle(hMap); CloseHandle(hFile);
		return false;
	}

	const PECandidate* best = nullptr;
	for (const auto& c : candidates) {
		if (!best) { best = &c; continue; }
		if (c.hasTargetExport && !best->hasTargetExport) best = &c;
		else if (c.hasTargetExport == best->hasTargetExport && c.exportCount > best->exportCount) best = &c;
	}
	if (!best) { UnmapViewOfFile(base); CloseHandle(hMap); CloseHandle(hFile); return false; }

	std::wstring name = dllName.empty() ? L"opentui.dll" : dllName;
	outPath = outDir;
	if (!outPath.empty() && outPath.back() != L'\\') outPath += L'\\';
	outPath += name;

	bool found = false;
	HANDLE hOut = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0,
		nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hOut != INVALID_HANDLE_VALUE) {
		size_t written_total = 0;
		const size_t chunkSize = 4 * 1024 * 1024;
		bool writeOk = true;
		while (written_total < best->size) {
			DWORD toWrite = static_cast<DWORD>(min(chunkSize, best->size - written_total));
			DWORD written = 0;
			if (!WriteFile(hOut, base + best->offset + written_total, toWrite, &written, nullptr) || written != toWrite) {
				CloseHandle(hOut); DeleteFileW(outPath.c_str()); writeOk = false; break;
			}
			written_total += written;
		}
		if (writeOk) {
			CloseHandle(hOut);
			if (VerifyExtractedDll(outPath, true)) found = true;
			else DeleteFileW(outPath.c_str());
		}
	}

	UnmapViewOfFile(base); CloseHandle(hMap); CloseHandle(hFile);
	return found;
}

static std::wstring ResolveExeFullPath(PCWSTR exeName) {
	if (!exeName) return L"";
	std::wstring image = ParseImagePathFromCommandLine(exeName);
	if (image.empty()) return L"";

	if (image.find(L'\\') != std::wstring::npos || image.find(L'/') != std::wstring::npos) {
		DWORD attrs = GetFileAttributesW(image.c_str());
		if (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY)) return image;
		return L"";
	}

	std::wstring exeDir = GetExeDir();
	if (!exeDir.empty()) {
		std::wstring c = JoinPath(exeDir, image);
		DWORD a = GetFileAttributesW(c.c_str());
		if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) return c;
	}
	for (const auto& entry : AllowedPaths) {
		std::wstring c = JoinPath(entry.path, image);
		DWORD a = GetFileAttributesW(c.c_str());
		if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) return c;
	}

	wchar_t fullPath[MAX_PATH * 2] = {};
	DWORD n = SearchPathW(nullptr, image.c_str(), L".exe", _countof(fullPath), fullPath, nullptr);
	if (n > 0 && n < _countof(fullPath)) return std::wstring(fullPath, n);
	return L"";
}

static bool PrepareBunVirtualDrive() {
	if (!ExeToLaunch) return false;
	std::wstring image = ParseImagePathFromCommandLine(ExeToLaunch);
	std::wstring nameLower = ToLowerCopy(GetFileNamePart(image));
	if (nameLower.find(L"opencode") == std::wstring::npos) return false;

	UINT driveType = GetDriveTypeW(L"B:\\");
	bool bDriveAvailable = (driveType == DRIVE_NO_ROOT_DIR || driveType == 0);

	std::wstring exeFullPath = ResolveExeFullPath(ExeToLaunch);
	if (exeFullPath.empty()) return false;

	std::wstring stagingBase = AllowedPaths.empty() ? GetExeDir() : AllowedPaths[0].path;
	if (stagingBase.empty()) return false;

	g_BunStagingDir = JoinPath(stagingBase, L".bun-vfs");
	std::wstring bunDir = JoinPath(g_BunStagingDir, L"~BUN");
	std::wstring dllDir = JoinPath(bunDir, L"root");

	CreateDirectoryW(g_BunStagingDir.c_str(), nullptr);
	CreateDirectoryW(bunDir.c_str(), nullptr);
	CreateDirectoryW(dllDir.c_str(), nullptr);
	if (!DirectoryExists(dllDir)) return false;

	std::wstring dllName;
	{
		HANDLE hF = CreateFileW(exeFullPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
			nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (hF != INVALID_HANDLE_VALUE) {
			LARGE_INTEGER sz{}; GetFileSizeEx(hF, &sz);
			HANDLE hM = CreateFileMappingW(hF, nullptr, PAGE_READONLY, 0, 0, nullptr);
			if (hM) {
				const BYTE* d = static_cast<const BYTE*>(MapViewOfFile(hM, FILE_MAP_READ, 0, 0, 0));
				if (d) { dllName = FindDllNameInBinary(d, static_cast<size_t>(sz.QuadPart)); UnmapViewOfFile(d); }
				CloseHandle(hM);
			}
			CloseHandle(hF);
		}
	}

	std::wstring dllPath;
	bool alreadyExtracted = false;
	if (!dllName.empty()) {
		dllPath = JoinPath(dllDir, dllName);
		DWORD a = GetFileAttributesW(dllPath.c_str());
		if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) {
			if (VerifyExtractedDll(dllPath, true)) alreadyExtracted = true;
			else DeleteFileW(dllPath.c_str());
		}
	}

	if (!alreadyExtracted) {
		std::wstring extractedPath;
		if (ExtractEmbeddedDllFromExe(exeFullPath, dllDir, dllName, extractedPath)) dllPath = extractedPath;
	}

	if (!dllDir.empty() && DirectoryExists(dllDir)) AddPathPrependUnique(dllDir);

	if (bDriveAvailable) {
		if (DefineDosDeviceW(0, L"B:", g_BunStagingDir.c_str())) {
			g_BunDriveMapped = true;
			g_BunDriveTarget = g_BunStagingDir;
		}
	}

	if (!dllPath.empty()) {
		std::wstring dllFileName = GetFileNamePart(dllPath);
		std::wstring exeDir = GetExeDir();

		std::wstring candidateBases[3];
		int numCandidates = 0;
		if (!exeDir.empty()) candidateBases[numCandidates++] = exeDir;
		{
			std::wstring localAppData;
			for (const auto& ov : EnvOverrides) {
				if (IEquals(ov.name, L"LOCALAPPDATA")) { localAppData = ov.value; break; }
			}
			if (localAppData.empty()) localAppData = GetEnvVarCopy(L"LOCALAPPDATA");
			if (!localAppData.empty() && !IEquals(localAppData, exeDir))
				candidateBases[numCandidates++] = localAppData;
		}
		if (!AllowedPaths.empty() && !AllowedPaths[0].path.empty()) {
			bool dup = false;
			for (int i = 0; i < numCandidates; ++i) {
				if (IEquals(AllowedPaths[0].path, candidateBases[i])) { dup = true; break; }
			}
			if (!dup) candidateBases[numCandidates++] = AllowedPaths[0].path;
		}

		for (int i = 0; i < numCandidates; ++i) {
			std::wstring bunFallbackBase = JoinPath(candidateBases[i], L".bun");
			std::wstring bunFallbackBun = JoinPath(bunFallbackBase, L"~BUN");
			std::wstring bunFallbackRoot = JoinPath(bunFallbackBun, L"root");
			std::wstring bunFallbackDll = JoinPath(bunFallbackRoot, dllFileName);

			CreateDirectoryW(bunFallbackBase.c_str(), nullptr);
			CreateDirectoryW(bunFallbackBun.c_str(), nullptr);
			CreateDirectoryW(bunFallbackRoot.c_str(), nullptr);

			if (DirectoryExists(bunFallbackRoot)) {
				if (CopyFileW(dllPath.c_str(), bunFallbackDll.c_str(), FALSE)) {
					g_BunFallbackDirs.push_back(bunFallbackBase);
					AddPathPrependUnique(bunFallbackRoot);
					AddAllowedPathUnique(bunFallbackBase);
				}
			}
		}
	}
	return true;
}

static void CleanupBunVirtualDrive() {
	if (InterlockedCompareExchange(&g_BunCleanupDone, 1, 0) != 0) return;

	if (g_BunDriveMapped) {
		DefineDosDeviceW(DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE,
			L"B:", g_BunDriveTarget.c_str());
		g_BunDriveMapped = false;
	}
	if (!g_BunStagingDir.empty() && DirectoryExists(g_BunStagingDir)) {
		DeleteTreeNoFollow(g_BunStagingDir);
	}
	for (const auto& fbDir : g_BunFallbackDirs) {
		if (!fbDir.empty() && DirectoryExists(fbDir)) DeleteTreeNoFollow(fbDir);
	}
	g_BunFallbackDirs.clear();
}

static void AtExitCleanupBunDrive() { CleanupBunVirtualDrive(); }

static LONG WINAPI BunDriveCrashHandler(EXCEPTION_POINTERS*) {
	CleanupBunVirtualDrive();
	return EXCEPTION_CONTINUE_SEARCH;
}

// ========================================================================
// Environment Block Builder
// ========================================================================
static bool BuildChildEnvBlockFromOverrides() {
	if (EnvOverrides.empty() && PathPrependEntries.empty()) return false;

	LPWCH env = GetEnvironmentStringsW();
	std::vector<std::wstring> rawSpecial;
	std::vector<EnvKV> vars;

	if (env) {
		for (LPWCH p = env; *p;) {
			size_t len = wcslen(p);
			std::wstring entry(p, p + len);
			if (!entry.empty() && entry[0] == L'=') rawSpecial.push_back(entry);
			else {
				size_t eq = entry.find(L'=');
				if (eq != std::wstring::npos && eq > 0)
					vars.push_back(EnvKV{ entry.substr(0, eq), entry.substr(eq + 1) });
			}
			p += len + 1;
		}
		FreeEnvironmentStringsW(env);
	}

	for (const auto& ov : EnvOverrides) {
		if (ov.name.empty() || ov.name.find(L'=') != std::wstring::npos) continue;
		bool replaced = false;
		for (auto& cur : vars) {
			if (IEquals(cur.name, ov.name)) { cur.value = ov.value; replaced = true; break; }
		}
		if (!replaced) vars.push_back(ov);
	}

	if (!PathPrependEntries.empty()) {
		std::wstring prependJoined;
		for (const auto& p : PathPrependEntries) {
			if (p.empty()) continue;
			if (!prependJoined.empty()) prependJoined.push_back(L';');
			prependJoined.append(p);
		}
		if (!prependJoined.empty()) {
			auto it = std::find_if(vars.begin(), vars.end(),
				[](const EnvKV& kv) { return _wcsicmp(kv.name.c_str(), L"PATH") == 0; });
			if (it == vars.end()) vars.push_back(EnvKV{ L"PATH", prependJoined });
			else if (it->value.empty()) it->value = prependJoined;
			else it->value = prependJoined + L";" + it->value;
		}
	}

	std::sort(vars.begin(), vars.end(), [](const EnvKV& a, const EnvKV& b) {
		return _wcsicmp(a.name.c_str(), b.name.c_str()) < 0;
		});

	g_ChildEnv.clear();
	for (const auto& e : rawSpecial) {
		g_ChildEnv.insert(g_ChildEnv.end(), e.begin(), e.end());
		g_ChildEnv.push_back(L'\0');
	}
	for (const auto& e : vars) {
		std::wstring line = e.name + L"=" + e.value;
		g_ChildEnv.insert(g_ChildEnv.end(), line.begin(), line.end());
		g_ChildEnv.push_back(L'\0');
	}
	g_ChildEnv.push_back(L'\0');
	return true;
}

// ========================================================================
// Job Object & Console Host Management
// ========================================================================

static HANDLE CreateKillOnCloseJob() {
	HANDLE hJob = CreateJobObjectW(nullptr, nullptr);
	if (!hJob) { LogWarn(L"[Job] CreateJobObjectW failed: err=%lu", GetLastError()); return nullptr; }
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
	jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
		LogWarn(L"[Job] SetInformationJobObject failed: err=%lu", GetLastError());
		CloseHandle(hJob);
		return nullptr;
	}
	return hJob;
}

static std::vector<DWORD> SnapshotConsoleHostPids() {
	std::vector<DWORD> pids;
	HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnap == INVALID_HANDLE_VALUE) return pids;
	PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
	if (Process32FirstW(hSnap, &pe)) {
		do {
			std::wstring name = ToLowerCopy(pe.szExeFile);
			if (name == L"openconsole.exe" || name == L"conhost.exe") pids.push_back(pe.th32ProcessID);
		} while (Process32NextW(hSnap, &pe));
	}
	CloseHandle(hSnap);
	return pids;
}

static DWORD FindNewConsoleHostPid(const std::vector<DWORD>& before) {
	g_ConsoleHostsToCleanup.clear();
	int stableRoundsAfterFound = 0;
	for (int attempt = 0; attempt < 10; attempt++) {
		Sleep(200);
		auto after = SnapshotConsoleHostPids();
		bool addedThisRound = false;
		for (DWORD pid : after) {
			bool isNew = true;
			for (DWORD oldPid : before) { if (pid == oldPid) { isNew = false; break; } }
			if (!isNew) continue;
			if (std::find(g_ConsoleHostsToCleanup.begin(), g_ConsoleHostsToCleanup.end(), pid) != g_ConsoleHostsToCleanup.end()) continue;
			g_ConsoleHostsToCleanup.push_back(pid);
			addedThisRound = true;
		}
		if (!g_ConsoleHostsToCleanup.empty()) {
			stableRoundsAfterFound = addedThisRound ? 0 : (stableRoundsAfterFound + 1);
			if (stableRoundsAfterFound >= 2) break;
			continue;
		}
	}
	if (g_ConsoleHostsToCleanup.empty()) return 0;
	return g_ConsoleHostsToCleanup.front();
}

static void TerminateConsoleHostPid(DWORD pid, PCWSTR tag, DWORD gracefulWaitMs) {
	if (pid == 0) return;
	HANDLE hQuery = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
	if (!hQuery) return;
	DWORD wr = WaitForSingleObject(hQuery, gracefulWaitMs);
	CloseHandle(hQuery);
	if (wr == WAIT_OBJECT_0) return;

	HANDLE hTerm = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
	if (!hTerm) return;
	TerminateProcess(hTerm, 0);
	CloseHandle(hTerm);
}

static void TerminateConsoleHostIfAlive() {
	if (!g_ConsoleHostsToCleanup.empty()) {
		for (DWORD pid : g_ConsoleHostsToCleanup) TerminateConsoleHostPid(pid, L"Child", 5000);
		g_ConsoleHostsToCleanup.clear();
	}
	else {
		TerminateConsoleHostPid(g_ChildConsoleHostPid, L"Child", 5000);
	}
	g_ChildConsoleHostPid = 0;
}

static void TerminateParentConsoleHostIfAlive() {
	TerminateConsoleHostPid(g_ParentConsoleHostPid, L"Parent", 2000);
	g_ParentConsoleHostPid = 0;
}

static void AssignProcessToJob(HANDLE hJob, HANDLE hProcess) {
	if (!hJob || !hProcess) return;
	if (!AssignProcessToJobObject(hJob, hProcess))
		LogWarn(L"[Job] AssignProcessToJobObject failed: err=%lu", GetLastError());
}

// ========================================================================
// Process Launcher (Restricted Token mode)
// ========================================================================

static DWORD LaunchProcessWithRestrictedToken() {
	DWORD result = ERROR_SUCCESS;
	LogInfo(L"[Launch] Starting with Restricted Token: %ls (wait=%ls, newConsole=%ls)",
		ExeToLaunch, WaitForExit ? L"yes" : L"no", UseNewConsole ? L"yes" : L"no");

	SAFER_LEVEL_HANDLE hLevel = NULL;
	HANDLE hRestrictedToken = NULL;

	do {
		if (!g_hJob) g_hJob = CreateKillOnCloseJob();

		if (!SaferCreateLevel(SAFER_SCOPEID_USER, SAFER_LEVELID_NORMALUSER, SAFER_LEVEL_OPEN, &hLevel, NULL)) {
			result = GetLastError();
			LogError(L"[Launch] SaferCreateLevel failed: err=%lu", result);
			break;
		}
		if (!SaferComputeTokenFromLevel(hLevel, NULL, &hRestrictedToken, 0, NULL)) {
			result = GetLastError();
			LogError(L"[Launch] SaferComputeTokenFromLevel failed: err=%lu", result);
			SaferCloseLevel(hLevel);
			break;
		}
		SaferCloseLevel(hLevel); hLevel = NULL;

		TOKEN_MANDATORY_LABEL tml = { 0 };
		tml.Label.Attributes = SE_GROUP_INTEGRITY;

		const wchar_t* integritySid = L"S-1-16-4096";
		const wchar_t* integrityName = L"Low";
		if (IntegrityLevel == 1) { integritySid = L"S-1-16-8192"; integrityName = L"Medium"; }
		else if (IntegrityLevel == 2) { integritySid = L"S-1-16-12288"; integrityName = L"High"; }

		if (!ConvertStringSidToSid((LPWSTR)integritySid, &tml.Label.Sid)) {
			result = GetLastError(); CloseHandle(hRestrictedToken); break;
		}

		DWORD tmlSize = sizeof(tml) + GetLengthSid(tml.Label.Sid);
		if (!SetTokenInformation(hRestrictedToken, TokenIntegrityLevel, &tml, tmlSize)) {
			result = GetLastError();
			LocalFree(tml.Label.Sid); CloseHandle(hRestrictedToken); break;
		}
		LocalFree(tml.Label.Sid);
		LogInfo(L"[Launch] Restricted token created with %ls integrity level", integrityName);

		STARTUPINFOEXW si = { 0 };
		PROCESS_INFORMATION pi = { 0 };
		si.StartupInfo.cb = sizeof(si);

		DWORD createFlags = 0;
		LPVOID envBlock = nullptr;
		if (!g_ChildEnv.empty()) { envBlock = g_ChildEnv.data(); createFlags |= CREATE_UNICODE_ENVIRONMENT; }
		if (UseNewConsole) createFlags |= CREATE_NEW_CONSOLE;

		std::vector<DWORD> consoleHostsBefore;
		if (UseNewConsole) consoleHostsBefore = SnapshotConsoleHostPids();

		createFlags |= CREATE_SUSPENDED;
		if (!CreateProcessAsUserW(hRestrictedToken, NULL, ExeToLaunch, NULL, NULL, FALSE,
			createFlags, envBlock, NULL, &si.StartupInfo, &pi)) {
			result = GetLastError();
			LogError(L"[Launch] CreateProcessAsUserW FAILED: err=%lu", result);
			CloseHandle(hRestrictedToken);
			break;
		}

		AssignProcessToJob(g_hJob, pi.hProcess);
		g_ChildProcess = pi.hProcess;
		CloseHandle(hRestrictedToken);
		ResumeThread(pi.hThread);
		LogInfo(L"[Launch] Process created: PID=%lu, TID=%lu", pi.dwProcessId, pi.dwThreadId);

		if (HideParentConsole && UseNewConsole && WaitForExit) {
			DWORD pids[16] = {};
			DWORD procCount = GetConsoleProcessList(pids, 16);
			if (procCount <= 1) {
				HWND hwnd = GetConsoleWindow();
				if (hwnd) {
					DWORD consolePid = 0;
					GetWindowThreadProcessId(hwnd, &consolePid);
					HWND hideHwnd = hwnd;
					SetForegroundWindow(hwnd);
					HWND fg = GetForegroundWindow();
					if (fg) {
						DWORD fgPid = 0;
						GetWindowThreadProcessId(fg, &fgPid);
						if (fgPid == consolePid) hideHwnd = fg;
					}
					ShowWindow(hideHwnd, SW_HIDE);
					if (consolePid != 0 && consolePid != GetCurrentProcessId()) {
						bool isKnownHost = false;
						for (DWORD pid : consoleHostsBefore) { if (pid == consolePid) { isKnownHost = true; break; } }
						if (isKnownHost) g_ParentConsoleHostPid = consolePid;
					}
				}
			}
		}

		if (UseNewConsole && g_hJob) {
			g_ChildConsoleHostPid = FindNewConsoleHostPid(consoleHostsBefore);
		}

		if (WaitForExit) {
			WaitForSingleObject(pi.hProcess, INFINITE);
			if (HideParentConsole && UseNewConsole) {
				FreeConsole();
				TerminateParentConsoleHostIfAlive();
			}
			DWORD exitCode = 0;
			if (GetExitCodeProcess(pi.hProcess, &exitCode)) {
				LogInfo(L"[Launch] Child exited: code=%lu (0x%08lX)", exitCode, exitCode);
				result = exitCode;
			}
			else {
				result = GetLastError();
			}
		}

		CloseHandle(pi.hThread);
		g_ChildProcess = nullptr;
		CloseHandle(pi.hProcess);

	} while (false);

	if (g_hJob) {
		LogInfo(L"[Launch] Closing Job Object -> KILL_ON_JOB_CLOSE will terminate child tree");
		CloseHandle(g_hJob); g_hJob = nullptr;
	}
	TerminateConsoleHostIfAlive();
	return result;
}

// ========================================================================
// Network Filter
// ========================================================================
static std::vector<std::wstring> ParseNetworkFilterUrls(const std::wstring& raw) {
	std::vector<std::wstring> result;
	if (raw.empty()) return result;
	std::wstring current;
	for (size_t i = 0; i < raw.size(); ++i) {
		wchar_t ch = raw[i];
		if (ch == L';' || ch == L',') {
			std::wstring trimmed = TrimCopy(current);
			if (!trimmed.empty()) result.push_back(trimmed);
			current.clear();
		}
		else current += ch;
	}
	std::wstring trimmed = TrimCopy(current);
	if (!trimmed.empty()) result.push_back(trimmed);
	return result;
}

// ========================================================================
// Exe Dir / INI Config
// ========================================================================
static std::wstring GetExeDir() {
	wchar_t path[MAX_PATH] = {};
	DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return L"";
	for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
		if (path[i] == L'\\' || path[i] == L'/') { path[i + 1] = L'\0'; break; }
	}
	return std::wstring(path);
}

static std::wstring ReadIniString(const std::wstring& iniPath, const wchar_t* key) {
	const DWORD kMax = 32767;
	std::vector<wchar_t> buf(kMax + 1);
	DWORD got = GetPrivateProfileStringW(L"App", key, L"", buf.data(), kMax, iniPath.c_str());
	return std::wstring(buf.data(), got);
}

static bool IniBoolFromString(const std::wstring& s, bool defVal = false) {
	if (s.empty()) return defVal;
	std::wstring t = TrimCopy(s);
	for (auto& ch : t) ch = static_cast<wchar_t>(towlower(ch));
	if (t == L"1" || t == L"true" || t == L"yes" || t == L"on") return true;
	if (t == L"0" || t == L"false" || t == L"no" || t == L"off") return false;
	return defVal;
}

static bool LoadConfigFromIniIfNoArgs(int argc) {
	if (argc > 1) return false;
	std::wstring dir = GetExeDir();
	if (dir.empty()) return false;
	std::wstring ini = dir + L"config.ini";
	DWORD attrs = GetFileAttributesW(ini.c_str());
	if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY)) return false;

	LogInfo(L"[Config] Loading config from: %ls", ini.c_str());
	AllowedPaths.clear(); EnvOverrides.clear(); PathPrependEntries.clear(); g_ChildEnv.clear();

	auto moniker = TrimCopy(ReadIniString(ini, L"moniker"));
	if (!moniker.empty()) PackageMoniker = moniker;
	auto exe = TrimCopy(ReadIniString(ini, L"exe"));
	if (!exe.empty()) {
		g_CmdLineBuf.assign(exe.begin(), exe.end());
		g_CmdLineBuf.push_back(L'\0');
		ExeToLaunch = g_CmdLineBuf.data();
	}
	auto disp = TrimCopy(ReadIniString(ini, L"displayName"));
	if (!disp.empty()) PackageDisplayName = disp;
	auto allowPaths = ReadIniString(ini, L"allowPaths");
	if (!allowPaths.empty()) ParseAllowedPathListFromArg(allowPaths.c_str());
	auto env = ReadIniString(ini, L"env");
	if (!env.empty()) ParseEnvListFromArg(env.c_str());
	auto prepend = ReadIniString(ini, L"pathPrepend");
	if (!prepend.empty()) ParsePathPrependListFromArg(prepend.c_str());

	auto lowIntegrity = ReadIniString(ini, L"lowIntegrityOnPaths");
	if (!lowIntegrity.empty()) PathLowIntegrity = IniBoolFromString(lowIntegrity, PathLowIntegrity);
	auto cleanup = ReadIniString(ini, L"cleanupSubdirs");
	if (!cleanup.empty()) CleanupAllowedSubdirs = IniBoolFromString(cleanup, CleanupAllowedSubdirs);
	WaitForExit = IniBoolFromString(ReadIniString(ini, L"wait"), WaitForExit);

	auto integrityStr = ReadIniString(ini, L"integrityLevel");
	if (!integrityStr.empty()) {
		if (integrityStr == L"low" || integrityStr == L"0") IntegrityLevel = 0;
		else if (integrityStr == L"medium" || integrityStr == L"1") IntegrityLevel = 1;
		else if (integrityStr == L"high" || integrityStr == L"2") IntegrityLevel = 2;
	}

	UseNewConsole = IniBoolFromString(ReadIniString(ini, L"newConsole"), UseNewConsole);
	HideParentConsole = IniBoolFromString(ReadIniString(ini, L"hideParentConsole"), HideParentConsole);
	g_LogEnabled = IniBoolFromString(ReadIniString(ini, L"log"), g_LogEnabled);

	g_NetworkFilterEnabled = IniBoolFromString(
		ReadIniString(ini, L"networkFilterEnabled"), g_NetworkFilterEnabled);
	auto nfPort = TrimCopy(ReadIniString(ini, L"networkFilterPort"));
	if (!nfPort.empty()) {
		int p = _wtoi(nfPort.c_str());
		if (p > 0 && p <= 65535) g_NetworkFilterPort = p;
	}
	g_NetworkFilterAllowedUrls = TrimCopy(ReadIniString(ini, L"networkFilterAllowedUrls"));

	LogInfo(L"[Config] Final: wait=%d, lowIntegrity=%d, cleanup=%d, newConsole=%d, "
		L"hideParent=%d, log=%d, integrityLevel=%d, networkFilter=%d",
		WaitForExit ? 1 : 0, PathLowIntegrity ? 1 : 0, CleanupAllowedSubdirs ? 1 : 0,
		UseNewConsole ? 1 : 0, HideParentConsole ? 1 : 0, g_LogEnabled ? 1 : 0,
		IntegrityLevel, g_NetworkFilterEnabled ? 1 : 0);
	return true;
}

// ========================================================================
// Network Filter Plugin Integration
// ========================================================================
static bool InitializeNetworkFilter() {
	if (!g_NetworkFilterEnabled) return true;
	auto domains = ParseNetworkFilterUrls(g_NetworkFilterAllowedUrls);
	if (domains.empty()) LogWarn(L"[NetworkFilter] Enabled but no allowed URLs. All requests BLOCKED.");
	NetworkFilterPlugin::SetAllowedDomains(domains);
	if (!NetworkFilterPlugin::Initialize(g_NetworkFilterPort)) {
		LogError(L"[NetworkFilter] Failed to initialize proxy on port %d", g_NetworkFilterPort);
		return false;
	}
	LogInfo(L"[NetworkFilter] Proxy started on %ls", NetworkFilterPlugin::GetProxyUrl().c_str());
	return true;
}

static void ShutdownNetworkFilter() {
	if (g_NetworkFilterEnabled && NetworkFilterPlugin::IsRunning()) {
		NetworkFilterPlugin::Shutdown();
	}
}

static void InjectProxyEnvVars() {
	if (!g_NetworkFilterEnabled || !NetworkFilterPlugin::IsRunning()) return;
	std::wstring proxyUrl = NetworkFilterPlugin::GetProxyUrl();
	UpsertEnvOverride(L"HTTP_PROXY", proxyUrl);
	UpsertEnvOverride(L"HTTPS_PROXY", proxyUrl);
	UpsertEnvOverride(L"http_proxy", proxyUrl);
	UpsertEnvOverride(L"https_proxy", proxyUrl);
	UpsertEnvOverride(L"NO_PROXY", L"localhost,127.0.0.1");
	UpsertEnvOverride(L"no_proxy", L"localhost,127.0.0.1");
}

// ========================================================================
// Console Control Handler
// ========================================================================
static BOOL WINAPI OnConsoleCtrl(DWORD ctrlType) {
	switch (ctrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
		LogWarn(L"[Console] Ctrl+C/Break received. Terminating child...");
		if (g_ChildProcess) TerminateProcess(g_ChildProcess, 0xC000013A);
		return TRUE;
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		LogWarn(L"[Console] Close/logoff/shutdown (type=%lu). Cleaning up...", ctrlType);
		if (g_ChildProcess) TerminateProcess(g_ChildProcess, 0xC000013A);
		if (g_hJob) { CloseHandle(g_hJob); g_hJob = nullptr; }
		TerminateConsoleHostIfAlive();
		TerminateParentConsoleHostIfAlive();
		ShutdownNetworkFilter();
		CleanupBunVirtualDrive();
		RestoreSavedSecurityOnce();
		return TRUE;
	default:
		return FALSE;
	}
}

// ========================================================================
// Entry Point
// ========================================================================
int wmain(int argc, WCHAR** argv) {
	DWORD result = ERROR_SUCCESS;

	if (!SetConsoleCtrlHandler(OnConsoleCtrl, TRUE))
		if (g_LogEnabled) wprintf(L"[Warn] SetConsoleCtrlHandler failed: err=%lu\r\n", GetLastError());

	LoadConfigFromIniIfNoArgs(argc);
	if (!ParseArguments(argc, argv)) { SetConsoleCtrlHandler(OnConsoleCtrl, FALSE); return 0; }
	if (PackageMoniker.empty() || ExeToLaunch == nullptr) {
		if (g_LogEnabled) wprintf(L"[Error] Missing required parameters: moniker or exe\r\n");
		PrintUsage();
		SetConsoleCtrlHandler(OnConsoleCtrl, FALSE);
		return 0;
	}

	InitLogFile();

	LogInfo(L"=== LaunchSandbox started (PID=%lu) === Moniker=%ls, Exe=%ls, AllowedPaths=%llu",
		GetCurrentProcessId(), PackageMoniker.c_str(), ExeToLaunch,
		(unsigned long long)AllowedPaths.size());

	if (AllowedPaths.empty()) {
		// Try to use the directory of the executable to launch
		std::wstring exePath = ResolveExeFullPath(ExeToLaunch);
		std::wstring exeDir;
		
		if (!exePath.empty()) {
			exeDir = GetParentDir(exePath);
			if (!exeDir.empty() && DirectoryExists(exeDir)) {
				AddAllowedPathUnique(exeDir, PathAccessLevel::FullControl);
				LogInfo(L"[Config] No allowPaths configured, using exe directory: %ls", exeDir.c_str());
			}
		}
		
		// Fallback to current working directory if exe directory not found
		if (AllowedPaths.empty()) {
			wchar_t cwd[MAX_PATH] = {};
			DWORD cwdLen = GetCurrentDirectoryW(MAX_PATH, cwd);
			if (cwdLen > 0 && cwdLen < MAX_PATH) {
				AddAllowedPathUnique(std::wstring(cwd), PathAccessLevel::FullControl);
				LogInfo(L"[Config] No allowPaths configured, defaulting to CWD: %ls", cwd);
			}
		}
	}

	ResolveExePathIfSubst();
	ApplyDefaultOpenCodeEnvPaths();
	ApplyBunOpenTuiCompatibility();
	AutoEnableWaitForTuiIfNeeded();
	FixShellForSandbox();

	if (WaitForExit) {
		UpsertEnvOverride(L"TERM", L"xterm-256color");
		UpsertEnvOverride(L"COLORTERM", L"truecolor");
	}

	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);

	// Network Filter
	if (g_NetworkFilterEnabled) {
		if (!InitializeNetworkFilter()) {
			SetConsoleCtrlHandler(OnConsoleCtrl, FALSE);
			CloseLogFile();
			return 1;
		}
		InitProxyLogFile();
		InjectProxyEnvVars();
	}

	// Grant ACLs using current user SID
	HANDLE hToken = nullptr;
	if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
		DWORD tokenUserLen = 0;
		GetTokenInformation(hToken, TokenUser, nullptr, 0, &tokenUserLen);
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER && tokenUserLen > 0) {
			PTOKEN_USER pTokenUser = (PTOKEN_USER)LocalAlloc(LPTR, tokenUserLen);
			if (pTokenUser && GetTokenInformation(hToken, TokenUser, pTokenUser, tokenUserLen, &tokenUserLen)) {
				if (!AllowedPaths.empty()) GrantAccessToAllowedPaths(pTokenUser->User.Sid);
			}
			if (pTokenUser) LocalFree(pTokenUser);
		}
		CloseHandle(hToken);
	}

	// Bun virtual drive
	PrepareBunVirtualDrive();
	atexit(AtExitCleanupBunDrive);
	SetUnhandledExceptionFilter(BunDriveCrashHandler);

	// Build child environment
	if (!EnvOverrides.empty() || !PathPrependEntries.empty()) {
		BuildChildEnvBlockFromOverrides();
	}

	// Launch
	LogInfo(L"[Phase] Launching child process...");
	result = LaunchProcessWithRestrictedToken();

	if (CleanupAllowedSubdirs && (WaitForExit || result != ERROR_SUCCESS)) {
		CleanupAllowedPathSubdirs();
	}

	// Cleanup
	HWND consoleHwnd = GetConsoleWindow();
	FreeConsole();

	{
		HANDLE hCleanup = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
			ShutdownNetworkFilter();
			CleanupBunVirtualDrive();
			RestoreSavedSecurityOnce();
			return 0;
			}, nullptr, 0, nullptr);
		if (hCleanup) {
			if (WaitForSingleObject(hCleanup, 8000) != WAIT_OBJECT_0) {
				LogWarn(L"[Cleanup] Timeout after 8s, forcing exit");
			}
			CloseHandle(hCleanup);
		}
	}

	SetConsoleCtrlHandler(OnConsoleCtrl, FALSE);
	LogInfo(L"=== LaunchSandbox finished: result=%lu (0x%08lX) ===", result, result);
	CloseProxyLogFile();
	CloseLogFile();

	if (consoleHwnd && IsWindow(consoleHwnd)) PostMessageW(consoleHwnd, WM_CLOSE, 0, 0);

	UINT exitCode = static_cast<UINT>(result);
	if (exitCode == 0xC000013A) exitCode = 0;
	ExitProcess(exitCode);
	return static_cast<int>(exitCode);
}