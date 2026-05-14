#include "IniFile.h"
#include <fstream>
#include <algorithm>

bool IniFile::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return false;

    std::string line, current_section;
    while (std::getline(file, line)) {
        // Remove whitespace
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
            current_section = line.substr(1, line.length() - 2);
        } else if (!current_section.empty()) {
            size_t eq_pos = line.find('=');
            if (eq_pos != std::string::npos) {
                std::string key = line.substr(0, eq_pos);
                std::string value = line.substr(eq_pos + 1);
                data[current_section][key].push_back(value);
            }
        }
    }
    return true;
}

bool IniFile::save(const std::string& filepath) {
    std::ofstream file(filepath);
    if (!file.is_open()) return false;

    for (const auto& section : data) {
        file << "[" << section.first << "]" << std::endl;
        for (const auto& key_values : section.second) {
            for (const auto& value : key_values.second) {
                file << key_values.first << "=" << value << std::endl;
            }
        }
        file << std::endl;
    }
    return true;
}

std::vector<std::string> IniFile::getValues(const std::string& section, const std::string& key) {
    if (data.count(section) && data[section].count(key)) {
        return data[section][key];
    }
    return {};
}

std::string IniFile::getValue(const std::string& section, const std::string& key) {
    auto values = getValues(section, key);
    return values.empty() ? "" : values[0];
}

void IniFile::setValue(const std::string& section, const std::string& key, const std::string& value) {
    data[section][key] = {value};
}

void IniFile::addValue(const std::string& section, const std::string& key, const std::string& value) {
    data[section][key].push_back(value);
}

void IniFile::removeValue(const std::string& section, const std::string& key, const std::string& value) {
    if (data.count(section) && data[section].count(key)) {
        auto& values = data[section][key];
        values.erase(std::remove(values.begin(), values.end(), value), values.end());
    }
}

bool IniFile::hasSection(const std::string& section) {
    return data.count(section) > 0;
}