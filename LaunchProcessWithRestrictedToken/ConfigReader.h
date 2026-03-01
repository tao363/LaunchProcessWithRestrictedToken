#pragma once

#include <string>
#include <vector>
#include <map>
#include <Windows.h>

// 配置文件读取类
class ConfigReader {
public:
    ConfigReader();
    ~ConfigReader();

    // 加载配置文件
    bool Load(const std::wstring& configPath);

    // 获取字符串值
    std::wstring GetString(const std::wstring& section, const std::wstring& key, const std::wstring& defaultValue = L"") const;

    // 获取整数值
    int GetInt(const std::wstring& section, const std::wstring& key, int defaultValue = 0) const;

    // 获取布尔值
    bool GetBool(const std::wstring& section, const std::wstring& key, bool defaultValue = false) const;

    // 获取字符串列表（逗号分隔）
    std::vector<std::wstring> GetStringList(const std::wstring& section, const std::wstring& key) const;

    // 检查配置是否已加载
    bool IsLoaded() const { return loaded_; }

private:
    std::wstring configPath_;
    bool loaded_;

    // 辅助函数：去除字符串首尾空白
    static std::wstring Trim(const std::wstring& str);

    // 辅助函数：分割字符串
    static std::vector<std::wstring> Split(const std::wstring& str, wchar_t delimiter);
};
