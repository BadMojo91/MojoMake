#pragma once

#include <string>
#include <vector>
#include <map>

class IniFile {
private:
    std::map<std::string, std::map<std::string, std::vector<std::string>>> data;

public:
    bool load(const std::string& filepath);
    bool save(const std::string& filepath);
    std::vector<std::string> getValues(const std::string& section, const std::string& key);
    std::string getValue(const std::string& section, const std::string& key);
    void setValue(const std::string& section, const std::string& key, const std::string& value);
    void addValue(const std::string& section, const std::string& key, const std::string& value);
    void removeValue(const std::string& section, const std::string& key, const std::string& value);
    bool hasSection(const std::string& section);
};