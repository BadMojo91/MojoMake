#include "DeusExBuilder.h"
#include "IniFile.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <regex>
#include <cstdlib>
#include <set>
#include <algorithm>
#include <sstream>
#include <cctype>
#include <iomanip>
#include <windows.h>

namespace fs = std::filesystem;

// Console color helpers
static HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
static void setColor(int color) {
    SetConsoleTextAttribute(hConsole, color);
}
static void resetColor() {
    SetConsoleTextAttribute(hConsole, 7); // White on black
}

DeusExBuilder::DeusExBuilder() : ue2_support(false), ucc_exists(false), lcc_exists(false), compiler("UCC") {}

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

bool DeusExBuilder::initialize() {
    if (!loadOrCreateConfig()) return false;
    if (!validateGamePath()) return false;
    if (!setupCompiler()) return false;
    if (!validateProjectPath()) return false;
    if (!setupProjectIni()) return false;
    if (!syncEditPackages()) return false;
    configureConsoleWindow();
    return true;
}

void DeusExBuilder::configureConsoleWindow() {
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    if (console == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(console, &console_info)) {
        return;
    }

    const int column_width = 32;
    const size_t ue2_rows = (ue2_support && !ue2_edit_packages.empty()) ? ue2_edit_packages.size() + 1 : 0;
    size_t package_rows = project_edit_packages.size() + 1;
    if (ue2_rows > package_rows) package_rows = ue2_rows;
    if (blacklist_packages.size() > package_rows) package_rows = blacklist_packages.size();

    const SHORT minimum_width = static_cast<SHORT>(column_width * 3 + 4);
    const SHORT minimum_height = static_cast<SHORT>(package_rows + 16);
    const COORD largest_size = GetLargestConsoleWindowSize(console);
    if (largest_size.X == 0 || largest_size.Y == 0) {
        return;
    }

    SHORT target_width = console_info.dwSize.X;
    SHORT target_height = console_info.dwSize.Y;
    if (target_width < minimum_width) target_width = minimum_width;
    if (target_height < minimum_height) target_height = minimum_height;
    if (target_width > largest_size.X) target_width = largest_size.X;
    if (target_height > largest_size.Y) target_height = largest_size.Y;

    COORD target_buffer_size = console_info.dwSize;
    if (target_buffer_size.X < target_width) target_buffer_size.X = target_width;
    if (target_buffer_size.Y < target_height) target_buffer_size.Y = target_height;
    SetConsoleScreenBufferSize(console, target_buffer_size);

    CONSOLE_SCREEN_BUFFER_INFO resized_info;
    if (!GetConsoleScreenBufferInfo(console, &resized_info)) {
        return;
    }

    SMALL_RECT target_window = resized_info.srWindow;
    const SHORT current_window_width = target_window.Right - target_window.Left + 1;
    const SHORT current_window_height = target_window.Bottom - target_window.Top + 1;
    if (current_window_width < target_width) target_window.Right = target_window.Left + target_width - 1;
    if (current_window_height < target_height) target_window.Bottom = target_window.Top + target_height - 1;
    SetConsoleWindowInfo(console, TRUE, &target_window);
}

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
        project_system_dir = project_path + "/System";
        classes_dir = project_path + "/Classes";
        
        // Check UE2 support for scanning
        ue2_support = fs::exists(ued22_dir + "/UCC.exe") && fs::exists(ued22_dir + "/UnrealTournament.ini");
        
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

bool DeusExBuilder::setupCompiler() {
    ucc_exists = fs::exists(system_dir + "/UCC.exe");
    lcc_exists = fs::exists(system_dir + "/LCC.exe");

    if (!ucc_exists && !lcc_exists) {
        std::cerr << "Neither UCC.exe nor LCC.exe was found in the System directory" << std::endl;
        return false;
    }

    IniFile config;
    if (!config.load(config_path)) {
        std::cerr << "Failed to load MojoMake.ini" << std::endl;
        return false;
    }

    std::string configured_compiler = config.getValue("Build", "Compiler");
    std::transform(configured_compiler.begin(), configured_compiler.end(), configured_compiler.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (configured_compiler == "UCC" && ucc_exists) {
        compiler = "UCC";
    } else if (configured_compiler == "LCC" && lcc_exists) {
        compiler = "LCC";
    } else if (ucc_exists) {
        compiler = "UCC";
    } else {
        compiler = "LCC";
    }

    if (configured_compiler != compiler) {
        config.setValue("Build", "Compiler", compiler);
        if (!config.save(config_path)) {
            std::cerr << "Failed to save compiler selection to MojoMake.ini" << std::endl;
            return false;
        }
    }

    return true;
}

void DeusExBuilder::toggleCompiler() {
    std::string next_compiler = compiler == "UCC" ? "LCC" : "UCC";
    bool next_exists = next_compiler == "UCC" ? ucc_exists : lcc_exists;

    if (!next_exists) {
        std::cout << next_compiler << ".exe is not available in the System directory." << std::endl;
        return;
    }

    IniFile config;
    if (!config.load(config_path)) {
        std::cerr << "Failed to load MojoMake.ini" << std::endl;
        return;
    }

    compiler = next_compiler;
    config.setValue("Build", "Compiler", compiler);
    if (config.save(config_path)) {
        std::cout << "Compiler changed to " << compiler << "." << std::endl;
    } else {
        std::cerr << "Failed to save compiler selection to MojoMake.ini" << std::endl;
    }
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
        config.save("MojoMake.ini");
    }

    // Store project packages for menu
    project_edit_packages = config.getValues("Editor.EditorEngine", "EditPackages");
    ue2_edit_packages = config.getValues("UE2.Editor", "EditPackages");
    // Load optional blacklist for builds
    blacklist_packages = config.getValues("Build", "Blacklist");

    return true;
}

void DeusExBuilder::showMenu() {
    while (true) {
        std::cout << std::endl;
        const int column_width = 32;
        const auto print_title_value = [column_width](const std::string& label, const std::string& value) {
            setColor(11); // Cyan
            std::cout << label;
            setColor(12); // Red
            std::cout << value;
            resetColor();
            const size_t content_width = label.size() + value.size();
            if (content_width < static_cast<size_t>(column_width)) {
                std::cout << std::string(column_width - content_width, ' ');
            }
        };

        setColor(11); // Cyan
        std::cout << std::string(column_width * 3, '=') << std::endl;
        std::cout << std::left << std::setw(column_width) << "[ UnrealScript Compiler ]";
        resetColor();
        setColor(14); // Yellow
        std::cout << std::left << std::setw(column_width) << "[ Options ]";
        std::cout << std::endl;

        print_title_value("Version ", MOJO_MAKE_VERSION);
        setColor(14); // Yellow
        std::cout << std::left << std::setw(column_width) << "s: Scan UE2 compatibility";
        std::cout << "r: Reset Config (MojoMake.ini)" << std::endl;

        print_title_value("Project: ", project_name);
        setColor(14); // Yellow
        std::cout << std::left << std::setw(column_width) << ("t: Toggle Compiler (" + compiler + ")");
        std::cout << "g: Set Game Directory" << std::endl;

        print_title_value("UCC: ", ucc_exists ? "Yes" : "No");
        setColor(14); // Yellow
        std::cout << std::left << std::setw(column_width) << "b: Toggle blacklist";
        std::cout << "j: Set Project Name" << std::endl;

        print_title_value("LCC: ", lcc_exists ? "Yes" : "No");
        setColor(14); // Yellow
        std::cout << std::left << std::setw(column_width)
                  << (ue2_support ? "u: Update UnrealTournament.ini" : "");
        std::cout << "p: Rescan packages" << std::endl;

        print_title_value("UE2 support: ", ue2_support ? "Yes" : "No");
        setColor(14); // Yellow
        std::cout << std::left << std::setw(column_width)
                  << (ue2_support ? "c: Clean UE2 packages" : "");
        std::cout << "q: Quit" << std::endl;

        setColor(11); // Cyan
        std::cout << std::endl;
        std::cout << std::string(column_width * 3, '=') << std::endl;
        resetColor();
        std::cout << std::endl;

        const bool show_ue2_packages = ue2_support && !ue2_edit_packages.empty();
        const size_t ue1_row_count = project_edit_packages.size() + 1;
        const size_t ue2_row_count = show_ue2_packages ? ue2_edit_packages.size() + 1 : 0;
        size_t row_count = ue1_row_count;
        if (ue2_row_count > row_count) row_count = ue2_row_count;
        if (blacklist_packages.size() > row_count) row_count = blacklist_packages.size();

        std::cout << std::endl;
        setColor(10); // Green
        std::cout << std::left << std::setw(column_width) << "[ UE1 Packages ]";
        resetColor();
        if (show_ue2_packages) {
            setColor(13); // Magenta
            std::cout << std::left << std::setw(column_width) << "[ UE2 Packages ]";
            resetColor();
        } else {
            std::cout << std::left << std::setw(column_width) << "";
        }
        setColor(8); // Grey
        std::cout << "[ Blacklisted ]" << std::endl;
        resetColor();

        int choice_index = 2;
        for (size_t row = 0; row < row_count; ++row) {
            if (row == 0) {
                setColor(10); // Green
                std::cout << std::left << std::setw(column_width) << "1: All UE1 packages";
                resetColor();
            } else if (row - 1 < project_edit_packages.size()) {
                const auto& package = project_edit_packages[row - 1];
                const bool is_blacklisted = std::find(blacklist_packages.begin(), blacklist_packages.end(), package) != blacklist_packages.end();
                setColor(is_blacklisted ? 6 : 10); // Orange/Brown or green
                std::cout << std::left << std::setw(column_width)
                          << (std::to_string(choice_index++) + ": " + package);
                resetColor();
            } else {
                std::cout << std::left << std::setw(column_width) << "";
            }

            if (show_ue2_packages) {
                const size_t ue2_row = row;
                if (ue2_row == 0) {
                    setColor(13); // Magenta
                    std::cout << std::left << std::setw(column_width)
                              << (std::to_string(static_cast<int>(project_edit_packages.size()) + 2) + ": All UE2 packages");
                    resetColor();
                } else if (ue2_row - 1 < ue2_edit_packages.size()) {
                    const auto& package = ue2_edit_packages[ue2_row - 1];
                    const bool is_blacklisted = std::find(blacklist_packages.begin(), blacklist_packages.end(), package) != blacklist_packages.end();
                    setColor(is_blacklisted ? 6 : 13); // Orange/Brown or magenta
                    std::cout << std::left << std::setw(column_width)
                              << (std::to_string(static_cast<int>(project_edit_packages.size()) + 2 + ue2_row) + ": " + package);
                    resetColor();
                } else {
                    std::cout << std::left << std::setw(column_width) << "";
                }
            } else {
                std::cout << std::left << std::setw(column_width) << "";
            }

            if (row < blacklist_packages.size()) {
                setColor(8); // Grey
                std::cout << "- " << blacklist_packages[row];
                resetColor();
            }
            std::cout << std::endl;
        }

        std::string input;
        std::cout << std::endl << "Please select a package to compile: ";
        std::getline(std::cin, input);
        
        if (input.empty()) {
            std::cout << "No choice selected. Exiting" << std::endl;
            break;
        }
        
        if (input == "q" || input == "Q") {
            std::cout << "Quitting..." << std::endl;
            break;
        }
        
        if (input == "s" || input == "S") {
            scanForUE2Compatibility();
            continue;
        }

        if (input == "t" || input == "T") {
            toggleCompiler();
            continue;
        }

        if (input == "r" || input == "R") {
            std::cout << std::endl << "This will remove and recreate MojoMake.ini. Continue? (y/n): ";
            std::string resp;
            std::getline(std::cin, resp);
            if (resp != "y" && resp != "Y") {
                std::cout << "Cancelled." << std::endl;
                continue;
            }

            try {
                if (fs::exists(config_path)) {
                    fs::remove(config_path);
                    std::cout << "Removed existing MojoMake.ini" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Failed to remove existing MojoMake.ini: " << e.what() << std::endl;
                continue;
            }

            initialize();
            continue;
        }
        
        if ((input == "u" || input == "U") && ue2_support) {
            updateUnrealTournamentIniManual();
            continue;
        }
        
        if ((input == "c" || input == "C") && ue2_support) {
            cleanUnrealTournamentIni();
            continue;
        }

        if (input == "b" || input == "B") {
            // Toggle blacklist for project packages
            IniFile config;
            if (!config.load(config_path)) {
                std::cerr << "Failed to load MojoMake.ini" << std::endl;
            } else {
                std::cout << std::endl << "Project packages:" << std::endl;
                for (size_t i = 0; i < project_edit_packages.size(); ++i) {
                    const auto& pkg = project_edit_packages[i];
                    bool is_black = std::find(blacklist_packages.begin(), blacklist_packages.end(), pkg) != blacklist_packages.end();
                    std::cout << (i + 1) << ": " << pkg;
                    if (is_black) {
                        setColor(8); // Grey
                        std::cout << " (blacklisted)";
                        resetColor();
                    }
                    std::cout << std::endl;
                }

                std::cout << std::endl << "Enter package number to toggle blacklist (or q to cancel): ";
                std::string sel;
                std::getline(std::cin, sel);
                if (sel.empty() || sel == "q" || sel == "Q") {
                    continue;
                }

                try {
                    int idx = std::stoi(sel) - 1;
                    if (idx < 0 || idx >= (int)project_edit_packages.size()) {
                        std::cout << "Invalid selection" << std::endl;
                        continue;
                    }

                    std::string target = project_edit_packages[idx];
                    // Toggle in config Build:Blacklist
                    bool currently_black = std::find(blacklist_packages.begin(), blacklist_packages.end(), target) != blacklist_packages.end();
                    if (currently_black) {
                        config.removeValue("Build", "Blacklist", target);
                        std::cout << "Removed " << target << " from blacklist" << std::endl;
                    } else {
                        config.addValue("Build", "Blacklist", target);
                        std::cout << "Added " << target << " to blacklist" << std::endl;
                    }

                    if (!config.save(config_path)) {
                        std::cerr << "Failed to save MojoMake.ini" << std::endl;
                    } else {
                        // Reload blacklist in memory
                        blacklist_packages = config.getValues("Build", "Blacklist");
                    }
                } catch (const std::exception&) {
                    std::cout << "Invalid input" << std::endl;
                }
            }

            continue;
        }
        
        try {
            int choice = std::stoi(input);
            
            if (choice == 1) {
                compileAllPackages(false); // All UE1 packages
            } else {
                int index = choice - 2;
                if (index >= 0 && index < (int)project_edit_packages.size()) {
                    std::string package = project_edit_packages[index];
                    compileSinglePackage(package, false);
                } else if (ue2_support && !ue2_edit_packages.empty()) {
                    int ue2_all_index = static_cast<int>(project_edit_packages.size());
                    if (index == ue2_all_index) {
                        compileAllPackages(true); // All UE2 packages
                    } else {
                        int ue2_index = index - ue2_all_index - 1;
                        if (ue2_index >= 0 && ue2_index < (int)ue2_edit_packages.size()) {
                            std::string package = ue2_edit_packages[ue2_index];
                            compileSinglePackage(package, true);
                        } else {
                            std::cout << "Invalid choice" << std::endl;
                        }
                    }
                } else {
                    std::cout << "Invalid choice" << std::endl;
                }
            }
        } catch (const std::exception&) {
            std::cout << "Invalid input" << std::endl;
        }
    }
}

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
        if (config.save("MojoMake.ini")) {
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
        
        if (config.save("MojoMake.ini")) {
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

void DeusExBuilder::updateUnrealTournamentIniManual() {
    std::cout << std::endl << "Updating UnrealTournament.ini with UE2 packages..." << std::endl;
    
    if (ue2_edit_packages.empty()) {
        std::cout << "No UE2 packages configured. Use 's' to scan for UE2 compatibility first." << std::endl;
        return;
    }
    
    if (updateUnrealTournamentIni(true)) {
        std::cout << "Successfully added " << ue2_edit_packages.size() 
                  << " UE2 package(s) to UnrealTournament.ini" << std::endl;
    } else {
        std::cerr << "Failed to update UnrealTournament.ini" << std::endl;
    }
}

void DeusExBuilder::cleanUnrealTournamentIni() {
    std::cout << std::endl << "Cleaning UE2 packages from UnrealTournament.ini..." << std::endl;
    
    if (updateUnrealTournamentIni(false)) {
        std::cout << "Successfully removed UE2 packages from UnrealTournament.ini" << std::endl;
    } else {
        std::cerr << "Failed to clean UnrealTournament.ini" << std::endl;
    }
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

bool DeusExBuilder::updateDeusExIni(bool add_packages, bool ue2, const std::vector<std::string>* packages_override) {
    IniFile deusex_ini;
    if (!deusex_ini.load(system_dir + "/DeusEx.ini")) {
        std::cerr << "Failed to load DeusEx.ini" << std::endl;
        return false;
    }

    const std::vector<std::string>* packages_ptr = nullptr;
    std::vector<std::string> temp_packages;

    if (packages_override) {
        packages_ptr = packages_override;
    } else {
        packages_ptr = &((ue2) ? ue2_edit_packages : project_edit_packages);
    }

    const auto& packages = *packages_ptr;

    if (add_packages) {
        for (const auto& pkg : packages) {
            deusex_ini.addValue("Editor.EditorEngine", "EditPackages", pkg);
        }
    } else {
        for (const auto& pkg : packages) {
            deusex_ini.removeValue("Editor.EditorEngine", "EditPackages", pkg);
        }
    }

    return deusex_ini.save(system_dir + "/DeusEx.ini");
}

bool DeusExBuilder::updateUnrealTournamentIni(bool add_packages, const std::vector<std::string>* packages_override) {
    std::string ut_ini_path = ued22_dir + "/UnrealTournament.ini";

    if (!fs::exists(ut_ini_path)) {
        std::cerr << "UnrealTournament.ini not found" << std::endl;
        return false;
    }

    IniFile ut_ini;
    if (!ut_ini.load(ut_ini_path)) {
        std::cerr << "Failed to load UnrealTournament.ini" << std::endl;
        return false;
    }

    const std::vector<std::string>* packages_ptr = packages_override ? packages_override : &ue2_edit_packages;

    if (add_packages) {
        // Add packages that aren't already present
        auto existing_packages = ut_ini.getValues("Editor.EditorEngine", "EditPackages");
        std::set<std::string> existing_set(existing_packages.begin(), existing_packages.end());

        bool added_any = false;
        for (const auto& pkg : *packages_ptr) {
            if (existing_set.find(pkg) == existing_set.end()) {
                ut_ini.addValue("Editor.EditorEngine", "EditPackages", pkg);
                added_any = true;
            }
        }

        if (added_any) {
            return ut_ini.save(ut_ini_path);
        }
        return true; // Nothing to add

    } else {
        // Remove packages that match our UE2 packages (or override list)
        for (const auto& pkg : *packages_ptr) {
            ut_ini.removeValue("Editor.EditorEngine", "EditPackages", pkg);
        }
        return ut_ini.save(ut_ini_path);
    }
}

void DeusExBuilder::backupAndRemoveUFiles(const std::vector<std::string>& packages, bool ue2) {
    for (const auto& pkg : packages) {
        if (ue2) {
            // For UE2: only remove from UED22 directory
            std::string ued22_u_file = ued22_dir + "/" + pkg + ".u";
            if (fs::exists(ued22_u_file)) {
                std::cout << "Backing up and removing " << pkg << ".u from UED22..." << std::endl;
                fs::copy_file(ued22_u_file, ued22_u_file + ".bak", fs::copy_options::overwrite_existing);
                fs::remove(ued22_u_file);
            }
        } else {
            // For UE1: remove from game system and project system directories
            std::string game_u_file = system_dir + "/" + pkg + ".u";
            if (fs::exists(game_u_file)) {
                std::cout << "Backing up and removing " << pkg << ".u from game system..." << std::endl;
                fs::copy_file(game_u_file, game_u_file + ".bak", fs::copy_options::overwrite_existing);
                fs::remove(game_u_file);
            }
            
            std::string project_u_file = project_system_dir + "/" + pkg + ".u";
            if (fs::exists(project_u_file)) {
                std::cout << "Backing up and removing " << pkg << ".u from project system..." << std::endl;
                fs::copy_file(project_u_file, project_u_file + ".bak", fs::copy_options::overwrite_existing);
                fs::remove(project_u_file);
            }
        }
    }
}

void DeusExBuilder::moveCompiledFiles(const std::vector<std::string>& packages, bool ue2) {
    for (const auto& pkg : packages) {
        if (ue2) {
            // For UE2: move from UED22 to UED22 (they compile in place)
            std::string ued22_file = ued22_dir + "/" + pkg + ".u";
            if (fs::exists(ued22_file)) {
                std::cout << "Compiled " << pkg << ".u for UE2 (in UED22 folder)" << std::endl;
            }
        } else {
            // For UE1: move from game system to project system
            std::string src_file = system_dir + "/" + pkg + ".u";
            std::string dest_file = project_system_dir + "/" + pkg + ".u";
            
            if (fs::exists(src_file)) {
                std::cout << "Moving " << pkg << ".u to project system..." << std::endl;
                fs::rename(src_file, dest_file);
            }
        }
    }
}

int DeusExBuilder::runCompiler(const std::string& ucc_path, const std::string& args) {
    std::string command = "\"" + ucc_path + "\" make " + args;
    return system(command.c_str());
}

void DeusExBuilder::compileSinglePackage(const std::string& package, bool ue2) {
    std::cout << std::endl << "Compiling " << package << (ue2 ? " for UE2" : " for UE1") << "..." << std::endl;

    std::vector<std::string> packages = {package};
    bool is_blacklisted = std::find(
        blacklist_packages.begin(),
        blacklist_packages.end(),
        package) != blacklist_packages.end();

    if (is_blacklisted) {
        if (!ensureBlacklistedUFilesInGameSystem(packages, ue2)) {
            return;
        }

        std::cout << "Preserving existing .u for blacklisted package: "
                  << package << std::endl;
    } else {
        backupAndRemoveUFiles(packages, ue2);
    }

    if (!updateDeusExIni(true, ue2, &packages)) return;

    if (ue2) {
        process_exclusive_code(1, false, package);
        process_exclusive_code(2, true, package);

        if (!updateUnrealTournamentIni(true, &packages)) return;

        fs::current_path(ued22_dir);
        std::string args = "ini=" + ued22_dir + "/UnrealTournament.ini -package=" + package;
        runCompiler(ued22_dir + "/UCC.exe", args);

        process_exclusive_code(1, true, package);
        process_exclusive_code(2, true, package);
    } else {
        bool in_ue2 = std::find(
            ue2_edit_packages.begin(),
            ue2_edit_packages.end(),
            package) != ue2_edit_packages.end();

        if (in_ue2) {
            process_exclusive_code(2, false, package);
            process_exclusive_code(1, true, package);
        }

        fs::current_path(system_dir);
        std::string args = "ini=" + project_system_dir + "/" + project_name + ".ini -package=" + package;
        runCompiler(system_dir + "/" + compiler + ".exe", args);

        if (in_ue2) {
            process_exclusive_code(1, true, package);
            process_exclusive_code(2, true, package);
        }
    }

    updateDeusExIni(false, ue2, &packages);

    if (!is_blacklisted) {
        moveCompiledFiles(packages, ue2);
    }

    std::cout << "Compilation complete." << std::endl;
}

void DeusExBuilder::compileAllPackages(bool ue2) {
    const auto& all_packages = ue2 ? ue2_edit_packages : project_edit_packages;
    std::cout << std::endl << "Compiling all " << (ue2 ? "UE2" : "UE1") << " packages..." << std::endl;

    // Build full package list (include blacklisted packages so they are added to DeusEx.ini)
    std::set<std::string> blacklist_set(blacklist_packages.begin(), blacklist_packages.end());
    std::vector<std::string> packages(all_packages.begin(), all_packages.end());
    std::vector<std::string> non_blacklisted_packages;
    std::vector<std::string> blacklisted_packages_to_prepare;

    for (const auto& pkg : packages) {
        if (blacklist_set.find(pkg) == blacklist_set.end()) {
            non_blacklisted_packages.push_back(pkg);
        } else {
            blacklisted_packages_to_prepare.push_back(pkg);
            std::cout << "Preserving existing .u for blacklisted package: "
                      << pkg << std::endl;
        }
    }

    if (!ensureBlacklistedUFilesInGameSystem(blacklisted_packages_to_prepare, ue2)) {
        return;
    }

    if (!non_blacklisted_packages.empty()) {
        backupAndRemoveUFiles(non_blacklisted_packages, ue2);
    }

    // Update DeusEx.ini with the full package list (including blacklisted)
    if (!updateDeusExIni(true, ue2, &packages)) return;

    if (ue2) {
        // Process version-specific code for packages that exist in both lists
        for (const auto& pkg : packages) {
            process_exclusive_code(1, false, pkg); // Hide UE1 code
            process_exclusive_code(2, true, pkg);  // Show UE2 code
        }

        if (!updateUnrealTournamentIni(true, &packages)) return;

        // Change to UED22 directory for UE2 compilation
        fs::current_path(ued22_dir);
        runCompiler(ued22_dir + "/UCC.exe");

        // Restore markers after compilation
        for (const auto& pkg : packages) {
            process_exclusive_code(1, true, pkg);
            process_exclusive_code(2, true, pkg);
        }
    } else {
        // Process version-specific code for packages that exist in both lists
        for (const auto& pkg : packages) {
            process_exclusive_code(2, false, pkg); // Hide UE2 code
        }

        fs::current_path(system_dir);
        runCompiler(system_dir + "/" + compiler + ".exe");

        // Restore markers after compilation
        for (const auto& pkg : packages) {
            process_exclusive_code(1, true, pkg);
            process_exclusive_code(2, true, pkg);
        }
    }
        
    // Remove packages from DeusEx.ini after compile (full list)
    updateDeusExIni(false, ue2, &packages);

    // Move only non-blacklisted compiled files into the project system folder.
    // Blacklisted packages remain in the game system folder.
    moveCompiledFiles(non_blacklisted_packages, ue2);

    std::cout << "Compilation complete." << std::endl;
}   

bool DeusExBuilder::ensureBlacklistedUFilesInGameSystem(const std::vector<std::string>& packages, bool ue2) {
    if (ue2) {
        return true;
    }

    for (const auto& pkg : packages) {
        std::string project_u_file = project_system_dir + "/" + pkg + ".u";
        std::string game_u_file = system_dir + "/" + pkg + ".u";

        if (fs::exists(project_u_file) && !fs::exists(game_u_file)) {
            try {
                std::cout << "Copying existing blacklisted " << pkg
                          << ".u from project system to game system..." << std::endl;
                fs::copy_file(project_u_file, game_u_file);
            } catch (const std::exception& e) {
                std::cerr << "Failed to copy blacklisted " << pkg
                          << ".u to game system: " << e.what() << std::endl;
                return false;
            }
        }
    }

    return true;
}