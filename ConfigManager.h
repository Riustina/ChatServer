// ConfigManager.h
#pragma once

#include <map>
#include <string>
#include "Singleton.h"

// 存储 Section 内部键值对的结构体
struct SectionInfo {
    SectionInfo();
    ~SectionInfo();

    std::map<std::string, std::string> _section_datas;

    // 边界检查：找不到返回空字符串
    const std::string& operator[](const std::string& key) const;
};

// 配置管理单例类
class ConfigManager : public Singleton<ConfigManager>
{
    friend class Singleton<ConfigManager>;

public:
    ~ConfigManager();

    // 在首次 getInstance() 之前调用，用于指定配置文件路径
    static void SetConfigPath(const std::string& config_path);
    static const std::string& GetConfigPath();

    // 边界检查：找不到返回空的 SectionInfo
    const SectionInfo& operator[](const std::string& section) const;

private:
    ConfigManager();
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    std::map<std::string, SectionInfo> _config_map;
    static std::string _config_path;
};
