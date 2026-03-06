// ConfigManager.cpp

#include "ConfigManager.h"
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <iostream>

// --- SectionInfo 实现 ---

SectionInfo::SectionInfo() {}

SectionInfo::~SectionInfo() {
    _section_datas.clear();
}

const std::string& SectionInfo::operator[](const std::string& key) const {
    auto it = _section_datas.find(key);
    if (it == _section_datas.end()) {
        std::cerr << "[ConfigManager.h] 函数 [SectionInfo::operator[]] key ["
            << key << "] not found" << std::endl;
        static const std::string empty_str = "";
        return empty_str;
    }
    return it->second;
}

// --- ConfigManager 实现 ---

ConfigManager::ConfigManager() {
    try {
        // 获取当前路径并拼接配置文件路径
        boost::filesystem::path config_path = boost::filesystem::current_path() / "config.ini";
        std::cout << "[ConfigManager.cpp] 函数 [ConfigManager()] config path: " << config_path.string() << std::endl;

        if (!boost::filesystem::exists(config_path)) {
            std::cerr << "[ConfigManager.cpp] 函数 [ConfigManager()] " << config_path.string() << " 文件不存在!" << std::endl;
            return;
        }

        // 读取配置文件
        boost::property_tree::ptree pt;
        boost::property_tree::ini_parser::read_ini(config_path.string(), pt);

        // 填充 _config_map
        for (const auto& sectionPair : pt) {
            const std::string& sectionName = sectionPair.first;
            const boost::property_tree::ptree& sectionTree = sectionPair.second;

            SectionInfo sectionInfo;
            for (const auto& keyValuePair : sectionTree) {
                sectionInfo._section_datas[keyValuePair.first] = keyValuePair.second.get_value<std::string>();
            }
            this->_config_map[sectionName] = sectionInfo;
        }

        // 调试打印
        /*std::cout << "[ConfigManager.cpp] 函数 [ConfigManager()] 遍历配置项:" << std::endl;
        for (const auto& section : _config_map) {
            std::cout << "section: " << section.first << std::endl;
            for (const auto& keyValue : section.second._section_datas) {
                std::cout << "    key: " << keyValue.first << ", value: " << keyValue.second << std::endl;
            }
        }*/
    }
    catch (const std::exception& e) {
        std::cout << "[ConfigManager.cpp] 函数 [ConfigManager()] Exception: " << e.what() << std::endl;
    }
}

ConfigManager::~ConfigManager() {
    _config_map.clear();
}

const SectionInfo& ConfigManager::operator[](const std::string& section) const {
    auto it = _config_map.find(section);
    if (it == _config_map.end()) {
        std::cerr << "[ConfigManager.h] 函数 [ConfigManager::operator[]] section ["
            << section << "] not found" << std::endl;
        static const SectionInfo empty_section;
        return empty_section;
    }
    return it->second;
}