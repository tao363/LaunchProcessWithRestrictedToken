#include "ConfigReader.h"
#include "LogFunctions.h"
#include <fstream>
#include <sstream>
#include <algorithm>

ConfigReader::ConfigReader() : loaded_(false) {
}

ConfigReader::~ConfigReader() {
}

bool ConfigReader::Load(const std::wstring& configPath) {
    configPath_ = configPath;
    loaded_ = true;

    // 检查文件是否存在
    DWORD attrs = GetFileAttributesW(configPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        LogWarn(L"[ConfigReader] Config file not found: %s", configPath.c_str());
        return false;
    }

    LogInfo(L"[ConfigReader] Loaded config from: %s", configPath.c_str());
    return true;
}

std::wstring ConfigReader::GetString(const std::wstring& section, const std::wstring& key, const std::wstring& defaultValue) const {
    if (!loaded_) {
        return defaultValue;
    }

    wchar_t buffer[1024] = { 0 };
    DWORD result = GetPrivateProfileStringW(
        section.c_str(),
        key.c_str(),
        defaultValue.c_str(),
        buffer,
        1024,
        configPath_.c_str()
    );

    return std::wstring(buffer);
}

int ConfigReader::GetInt(const std::wstring& section, const std::wstring& key, int defaultValue) const {
    if (!loaded_) {
        return defaultValue;
    }

    return GetPrivateProfileIntW(
        section.c_str(),
        key.c_str(),
        defaultValue,
        configPath_.c_str()
    );
}

bool ConfigReader::GetBool(const std::wstring& section, const std::wstring& key, bool defaultValue) const {
    std::wstring value = GetString(section, key, defaultValue ? L"true" : L"false");
    value = Trim(value);

    // 转换为小写
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);

    return (value == L"true" || value == L"1" || value == L"yes" || value == L"on");
}

std::vector<std::wstring> ConfigReader::GetStringList(const std::wstring& section, const std::wstring& key) const {
    std::wstring value = GetString(section, key, L"");
    if (value.empty()) {
        return std::vector<std::wstring>();
    }

    return Split(value, L',');
}

std::wstring ConfigReader::Trim(const std::wstring& str) {
    size_t start = 0;
    size_t end = str.length();

    while (start < end && iswspace(str[start])) {
        start++;
    }

    while (end > start && iswspace(str[end - 1])) {
        end--;
    }

    return str.substr(start, end - start);
}

std::vector<std::wstring> ConfigReader::Split(const std::wstring& str, wchar_t delimiter) {
    std::vector<std::wstring> result;
    std::wstring current;

    for (wchar_t ch : str) {
        if (ch == delimiter) {
            std::wstring trimmed = Trim(current);
            if (!trimmed.empty()) {
                result.push_back(trimmed);
            }
            current.clear();
        }
        else {
            current += ch;
        }
    }

    std::wstring trimmed = Trim(current);
    if (!trimmed.empty()) {
        result.push_back(trimmed);
    }

    return result;
}
