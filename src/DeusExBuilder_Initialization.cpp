#include "DeusExBuilder.h"
#include "IniFile.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;

bool DeusExBuilder::loadOrCreateConfig() {
    IniFile config;
    // Store absolute path to MojoMake.ini from the initial working directory
    config_path = fs::current_path().string() + "/MojoMake.ini";

    if (!fs::exists(config_path)) {
        std::cout << "MojoMake.ini not found. Creating MojoMake.ini..." << std::endl;

        // Offer candidate game paths found by scanning current and parent directories
        auto candidates = findGameRootsFromCwd();
        if (!candidates.empty()) {
            std::cout << "Found potential game directories:" << std::endl;
            for (size_t i = 0; i < candidates.size(); ++i) {
                std::cout << "  " << (i + 1) << ": " << candidates[i] << std::endl;
            }
            std::cout << "Enter a number to select a candidate, or type an absolute path: ";
            std::string choice;
            std::getline(std::cin, choice);
            if (!choice.empty()) {
                try {
                    int idx = std::stoi(choice) - 1;
                    if (idx >= 0 && idx < (int)candidates.size()) {
                        game_path = candidates[idx];
                    } else {
                        game_path = choice; // treat as path
                    }
                } catch (const std::exception&) {
                    game_path = choice; // treat non-numeric as path
                }
            }
        } else {
            std::cout << "Please enter the game path: ";
            std::getline(std::cin, game_path);
        }

        // Validate provided game path
        if (game_path.empty()) {
            std::cout << "Game path cannot be empty. Cancelled." << std::endl;
            return false;
        }
        std::string test_system = game_path + "/System";
        if (!fs::exists(game_path) || !fs::exists(test_system) || !fs::exists(test_system + "/UCC.exe")) {
            std::cout << "Specified game path is invalid or missing System/UCC.exe." << std::endl;
            return false;
        }

        std::cout << "Please enter the project name: ";
        std::getline(std::cin, project_name);
        if (project_name.empty()) {
            std::cout << "Project name cannot be empty. Cancelled." << std::endl;
            return false;
        }

        config.setValue("Game.Info", "ProjectName", project_name);
        config.setValue("Game.Info", "GamePath", game_path);

        if (!config.save(config_path)) {
            std::cerr << "Failed to create MojoMake.ini" << std::endl;
            return false;
        }

        // Set up directory paths for scanning
        project_path = game_path + "/" + project_name;
        system_dir = game_path + "/System";
        ued22_dir = game_path + "/UED22";
		ued22x_dir = game_path + "/UED22x";
        project_system_dir = project_path + "/System";
        classes_dir = project_path + "/Classes";

        // Check UE2 support for scanning
        ue2_support = fs::exists(ued22_dir + "/UCC.exe") && fs::exists(ued22_dir + "/UnrealTournament.ini");

        // Check UE2x support for scanning
        ue22x_support = fs::exists(ued22x_dir + "/UCC.exe") && fs::exists(ued22x_dir + "/UnrealTournament.ini");

        // Perform initial package scanning to populate MojoMake.ini
        std::cout << "Performing initial package scan..." << std::endl;
        performInitialPackageScan();

        // Reload config to get the packages that were just added
        if (!config.load(config_path)) {
            std::cerr << "Failed to reload MojoMake.ini after scan" << std::endl;
            return false;
        }
        // Automatically add [Build] blacklist containing Default.ini EditPackages
        try {
            IniFile default_ini;
            if (fs::exists(system_dir + "/Default.ini") && default_ini.load(system_dir + "/Default.ini")) {
                auto default_edit_packages = default_ini.getValues("Editor.EditorEngine", "EditPackages");
                // Avoid duplicates in the config
                auto existing_black = config.getValues("Build", "Blacklist");
                std::set<std::string> existing_set(existing_black.begin(), existing_black.end());
                bool added = false;
                for (const auto& pkg : default_edit_packages) {
                    if (existing_set.find(pkg) == existing_set.end()) {
                        config.addValue("Build", "Blacklist", pkg);
                        added = true;
                    }
                }
                if (added) {
                    config.save(config_path);
                }
            }
        } catch (const std::exception&) {
            // Non-fatal; continue
        }
    } else {
        if (!config.load(config_path)) {
            std::cerr << "Failed to load MojoMake.ini" << std::endl;
            return false;
        }

        project_name = config.getValue("Game.Info", "ProjectName");
        game_path = config.getValue("Game.Info", "GamePath");

        if (project_name.empty() || game_path.empty()) {
            std::cerr << "ProjectName or GamePath not set in MojoMake.ini" << std::endl;
            return false;
        }
    }

    project_path = game_path + "/" + project_name;
    system_dir = game_path + "/System";
    ued22_dir = game_path + "/UED22";
    project_system_dir = project_path + "/System";
    classes_dir = project_path + "/Classes";

    return true;
}

bool DeusExBuilder::validateGamePath() {
    if (!fs::exists(game_path)) {
        std::cerr << "Game path does not exist: " << game_path << std::endl;
        return false;
    }

    if (!fs::exists(system_dir + "/UCC.exe")) {
        std::cerr << "UCC.exe not found in System directory" << std::endl;
        return false;
    }

    if (!fs::exists(system_dir + "/DeusEx.ini")) {
        std::cerr << "DeusEx.ini not found in System directory" << std::endl;
        return false;
    }

    if (!fs::exists(system_dir + "/Default.ini")) {
        std::cerr << "Default.ini not found in System directory" << std::endl;
        return false;
    }

    // Check UE2 support
    ue2_support = fs::exists(ued22_dir + "/UCC.exe") && fs::exists(ued22_dir + "/UnrealTournament.ini");

    return true;
}

bool DeusExBuilder::validateProjectPath() {
    if (!fs::exists(project_path)) {
        std::cerr << "Project path does not exist: " << project_path << std::endl;
        return false;
    }

    if (!fs::exists(classes_dir)) {
        std::cerr << "Classes directory does not exist: " << classes_dir << std::endl;
        return false;
    }

    // Check for at least one .uc file
    bool has_uc_files = false;
    for (const auto& entry : fs::directory_iterator(classes_dir)) {
        if (entry.path().extension() == ".uc") {
            has_uc_files = true;
            break;
        }
    }

    if (!has_uc_files) {
        std::cerr << "No .uc files found in Classes directory" << std::endl;
        return false;
    }

    return true;
}

bool DeusExBuilder::setupProjectIni() {
    fs::create_directories(project_system_dir);

    std::string project_ini = project_system_dir + "/" + project_name + ".ini";

    if (!fs::exists(project_ini)) {
        std::cout << project_name << ".ini not found in project System directory." << std::endl;
        std::cout << "Create a default one by copying DeusEx.ini? (y/n): ";

        std::string response;
        std::getline(std::cin, response);

        if (response == "y" || response == "Y") {
            try {
                fs::copy_file(system_dir + "/DeusEx.ini", project_ini);
                std::cout << "Created " << project_ini << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Failed to copy DeusEx.ini: " << e.what() << std::endl;
                return false;
            }
        } else {
            std::cerr << "Project ini file is required" << std::endl;
            return false;
        }
    }

    return true;
}

bool DeusExBuilder::syncEditPackages() {
    IniFile default_ini, project_ini, config;

    if (!default_ini.load(system_dir + "/Default.ini")) {
        std::cerr << "Failed to load Default.ini" << std::endl;
        return false;
    }

    if (!project_ini.load(project_system_dir + "/" + project_name + ".ini")) {
        std::cerr << "Failed to load project ini" << std::endl;
        return false;
    }

    if (!config.load(config_path)) {
        std::cerr << "Failed to load MojoMake.ini" << std::endl;
        return false;
    }

    // Get default edit packages
    auto default_packages = default_ini.getValues("Editor.EditorEngine", "EditPackages");
    std::set<std::string> default_set(default_packages.begin(), default_packages.end());

    // Get project edit packages (excluding defaults)
    auto all_project_packages = project_ini.getValues("Editor.EditorEngine", "EditPackages");
    std::vector<std::string> project_only_packages;

    for (const auto& pkg : all_project_packages) {
        if (default_set.find(pkg) == default_set.end()) {
            project_only_packages.push_back(pkg);
        }
    }

    // Update MojoMake.ini with project packages
    auto existing_packages = config.getValues("Editor.EditorEngine", "EditPackages");
    std::set<std::string> existing_set(existing_packages.begin(), existing_packages.end());

    bool config_updated = false;
    for (const auto& pkg : project_only_packages) {
        if (existing_set.find(pkg) == existing_set.end()) {
            config.addValue("Editor.EditorEngine", "EditPackages", pkg);
            config_updated = true;
        }
    }

    if (config_updated) {
        config.save(config_path);
    }

    // Store project packages for menu
    project_edit_packages = config.getValues("Editor.EditorEngine", "EditPackages");
    ue2_edit_packages = config.getValues("UE2.Editor", "EditPackages");
    // Load optional blacklist for builds
    blacklist_packages = config.getValues("Build", "Blacklist");

    return true;
}

std::vector<std::string> DeusExBuilder::findGameRootsFromCwd() {
    std::vector<std::string> results;
    try {
        fs::path cur = fs::current_path();
        std::set<std::string> seen;
        const std::vector<std::string> executables = {"deusex.exe", "unreal.exe", "unrealtournament.exe", "UCC.exe"};

        while (true) {
            fs::path system_dir = cur / "System";
            if (fs::exists(system_dir) && fs::is_directory(system_dir)) {
                for (const auto &exe : executables) {
                    if (fs::exists(system_dir / exe)) {
                        std::string root = cur.string();
                        if (seen.insert(root).second) {
                            results.push_back(root);
                        }
                        break;
                    }
                }
            }

            if (cur == cur.root_path()) break;
            cur = cur.parent_path();
        }
    } catch (const std::exception&) {
        // ignore errors and return whatever found
    }
    return results;
}
