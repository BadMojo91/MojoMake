#include "DeusExBuilder.h"
#include "IniFile.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>
#include <windows.h>

namespace fs = std::filesystem;

void DeusExBuilder::performInitialPackageScan() {
    if (!fs::exists(system_dir + "/Default.ini") || !fs::exists(project_system_dir + "/" + project_name + ".ini")) {
        std::cout << "Skipping initial scan - required ini files not found" << std::endl;
        return;
    }

    IniFile default_ini, project_ini, config;

    if (!default_ini.load(system_dir + "/Default.ini") || 
        !project_ini.load(project_system_dir + "/" + project_name + ".ini") ||
        !config.load(config_path)) {
        std::cout << "Failed to load ini files for initial scan" << std::endl;
        return;
    }

    // Get default edit packages
    auto default_packages = default_ini.getValues("Editor.EditorEngine", "EditPackages");
    std::set<std::string> default_set(default_packages.begin(), default_packages.end());

    // Get project edit packages (excluding defaults)
    auto all_project_packages = project_ini.getValues("Editor.EditorEngine", "EditPackages");
    std::vector<std::string> project_only_packages;
    std::vector<std::string> ue2_compatible_packages;

    for (const auto& pkg : all_project_packages) {
        if (default_set.find(pkg) == default_set.end()) {
            project_only_packages.push_back(pkg);

            // Check for UE2 compatibility
            std::string package_path = game_path + "/" + pkg;
            std::string package_classes = package_path + "/Classes";

            if (fs::exists(package_classes)) {
                bool has_ue_blocks = false;

                try {
                    for (const auto& entry : fs::directory_iterator(package_classes)) {
                        if (entry.path().extension() == ".uc") {
                            std::ifstream file(entry.path());
                            if (file.is_open()) {
                                std::string content((std::istreambuf_iterator<char>(file)),
                                                   std::istreambuf_iterator<char>());
                                file.close();

                                if (content.find("BEGIN UE1") != std::string::npos || 
                                    content.find("BEGIN UE2") != std::string::npos) {
                                    has_ue_blocks = true;
                                    break;
                                }
                            }
                        }
                    }
                } catch (const std::exception&) {
                    // Continue with next package if error occurs
                }

                if (has_ue_blocks) {
                    ue2_compatible_packages.push_back(pkg);
                    std::cout << "Found UE2 compatible package: " << pkg << std::endl;
                }
            }
        }
    }

    // Add packages to MojoMake.ini in the correct section order
    bool config_updated = false;

    // First add Editor.EditorEngine packages (after Game.Info which is already there)
    for (const auto& pkg : project_only_packages) {
        config.addValue("Editor.EditorEngine", "EditPackages", pkg);
        config_updated = true;
    }

    // Then add UE2.Editor packages (last section)
    for (const auto& pkg : ue2_compatible_packages) {
        config.addValue("UE2.Editor", "EditPackages", pkg);
        config_updated = true;
    }

    if (config_updated) {
        if (config.save(config_path)) {
            std::cout << "Added " << project_only_packages.size() << " UE1 package(s) and " 
                     << ue2_compatible_packages.size() << " UE2 compatible package(s) to MojoMake.ini" << std::endl;
        } else {
            std::cerr << "Failed to save updated MojoMake.ini" << std::endl;
        }
    } else {
        std::cout << "No additional packages found to add" << std::endl;
    }
}

void DeusExBuilder::scanForUE2Compatibility() {
    std::cout << std::endl << "Scanning packages for UE2 compatibility..." << std::endl;

    IniFile config;
    if (!config.load(config_path)) {
        std::cerr << "Failed to load MojoMake.ini" << std::endl;
        return;
    }

    std::vector<std::string> packages_to_add;

    for (const auto& package : project_edit_packages) {
        std::cout << "Checking " << package << "...";

        // Check if package already in UE2 list
        if (std::find(ue2_edit_packages.begin(), ue2_edit_packages.end(), package) != ue2_edit_packages.end()) {
            std::cout << " already in UE2 list" << std::endl;
            continue;
        }

        // Look for package directory in game path
        std::string package_path = game_path + "/" + package;
        std::string package_classes = package_path + "/Classes";

        if (!fs::exists(package_classes)) {
            std::cout << " no Classes directory found" << std::endl;
            continue;
        }

        bool has_ue_blocks = false;

        // Scan all .uc files in the package's Classes directory
        try {
            for (const auto& entry : fs::directory_iterator(package_classes)) {
                if (entry.path().extension() == ".uc") {
                    std::ifstream file(entry.path());
                    if (file.is_open()) {
                        std::string content((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
                        file.close();

                        // Check for UE1 or UE2 comment blocks
                        if (content.find("BEGIN UE1") != std::string::npos || 
                            content.find("BEGIN UE2") != std::string::npos) {
                            has_ue_blocks = true;
                            break;
                        }
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cout << " error scanning: " << e.what() << std::endl;
            continue;
        }

        if (has_ue_blocks) {
            std::cout << " UE1/UE2 blocks found - adding to UE2 compatibility" << std::endl;
            packages_to_add.push_back(package);
        } else {
            std::cout << " no UE1/UE2 blocks found" << std::endl;
        }
    }

    // Add found packages to MojoMake.ini
    if (!packages_to_add.empty()) {
        for (const auto& package : packages_to_add) {
            config.addValue("UE2.Editor", "EditPackages", package);
            ue2_edit_packages.push_back(package);
        }

        if (config.save(config_path)) {
            std::cout << std::endl << "Added " << packages_to_add.size() 
                     << " package(s) to UE2 compatibility list." << std::endl;
        } else {
            std::cerr << "Failed to save MojoMake.ini" << std::endl;
        }
    } else {
        std::cout << std::endl << "No new UE2-compatible packages found." << std::endl;
    }

    std::cout << "Scan complete." << std::endl;
}

void DeusExBuilder::process_exclusive_code(int version, bool is_enabled, const std::string& package) {
    std::string version_str = std::to_string(version);

    // If package is specified, check if it needs version processing
    if (!package.empty()) {
        bool in_ue1 = std::find(project_edit_packages.begin(), project_edit_packages.end(), package) != project_edit_packages.end();
        bool in_ue2 = std::find(ue2_edit_packages.begin(), ue2_edit_packages.end(), package) != ue2_edit_packages.end();

        // Only process version-specific code if package is in both lists
        if (!(in_ue1 && in_ue2)) {
            return;
        }
    }

    std::string target_dir = classes_dir;
    if (!package.empty()) {
        // Use package-specific directory
        target_dir = game_path + "/" + package + "/Classes";
    }

    if (!fs::exists(target_dir)) {
        return;
    }

    for (const auto& entry : fs::directory_iterator(target_dir)) {
        if (entry.path().extension() == ".uc") {

            std::ifstream file(entry.path());
            std::string content((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
            file.close();

            std::string original_content = content;

            if (is_enabled) {
                // Enable code: /* BEGIN UE? -> // BEGIN UE?  and  END UE? */ -> // END UE?
                {
                    const std::string from = "/* BEGIN UE" + version_str;
                    const std::string to   = "// BEGIN UE" + version_str;
                    size_t pos = 0;
                    while ((pos = content.find(from, pos)) != std::string::npos) {
                        content.replace(pos, from.size(), to);
                        pos += to.size();
                    }
                }
                {
                    const std::string from = "END UE" + version_str + " */";
                    const std::string to   = "// END UE" + version_str;
                    size_t pos = 0;
                    while ((pos = content.find(from, pos)) != std::string::npos) {
                        content.replace(pos, from.size(), to);
                        pos += to.size();
                    }
                }
            } else {
                // Disable code: // BEGIN UE? -> /* BEGIN UE?  and  // END UE? -> END UE? */
                {
                    const std::string from = "// BEGIN UE" + version_str;
                    const std::string to   = "/* BEGIN UE" + version_str;
                    size_t pos = 0;
                    while ((pos = content.find(from, pos)) != std::string::npos) {
                        content.replace(pos, from.size(), to);
                        pos += to.size();
                    }
                }
                {
                    const std::string from = "// END UE" + version_str;
                    const std::string to   = "END UE" + version_str + " */";
                    size_t pos = 0;
                    while ((pos = content.find(from, pos)) != std::string::npos) {
                        content.replace(pos, from.size(), to);
                        pos += to.size();
                    }
                }
            }

            if (content != original_content) {
                std::ofstream out_file(entry.path());
                if (out_file.is_open()) {
                    out_file << content;
                    out_file.close();
                }
            }
        }
    }
}

