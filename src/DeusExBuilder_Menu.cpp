#include "DeusExBuilder.h"
#include "IniFile.h"
#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

// Console color helpers
static HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
static void setColor(int color) {
    SetConsoleTextAttribute(hConsole, color);
}
static void resetColor() {
    SetConsoleTextAttribute(hConsole, 7); // White on black
}

static void clearConsole() {
    // \033[H moves the cursor to the top-left corner
    // \033[2J clears the entire screen
    std::cout << "\033[H\033[2J" << std::flush;
}

void DeusExBuilder::toggleBlacklistPackages() {
    // Toggle blacklist for project packages
    IniFile config;
    if (!config.load(config_path)) {
        std::cerr << "Failed to load MojoMake.ini" << std::endl;
        return;
    }
    else {
    START:
        clearConsole();
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
            return;
        }

        try {
            int idx = std::stoi(sel) - 1;
            if (idx < 0 || idx >= (int)project_edit_packages.size()) {
                std::cout << "Invalid selection" << std::endl;
                goto START;
            }

            std::string target = project_edit_packages[idx];
            // Toggle in config Build:Blacklist
            bool currently_black = std::find(blacklist_packages.begin(), blacklist_packages.end(), target) != blacklist_packages.end();
            if (currently_black) {
                config.removeValue("Build", "Blacklist", target);
                std::cout << "Removed " << target << " from blacklist" << std::endl;
            }
            else {
                config.addValue("Build", "Blacklist", target);
                std::cout << "Added " << target << " to blacklist" << std::endl;
            }

            if (!config.save(config_path)) {
                std::cerr << "Failed to save MojoMake.ini" << std::endl;
            }
            else {
                // Reload blacklist in memory
                blacklist_packages = config.getValues("Build", "Blacklist");
            }
        }
        catch (const std::exception&) {
            std::cout << "Invalid input" << std::endl;
        }
        goto START;
    }
}

void DeusExBuilder::configureConsoleWindow() {
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO console_info;
    if (console == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(console, &console_info)) {
        return;
    }

    const int column_width = 32;
    const size_t ue2_rows = (ue22_support && !ue2_edit_packages.empty()) ? ue2_edit_packages.size() + 1 : 0;
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
        std::cout << "p: Sync packages" << std::endl;

        print_title_value("UCC: ", ucc_exists ? "Yes" : "No");
        setColor(14); // Yellow
        std::cout << std::left << std::setw(column_width) << "b: Toggle blacklist";
        std::cout << "q: Quit" << std::endl;

        print_title_value("LCC: ", lcc_exists ? "Yes" : "No");
        setColor(14); // Yellow
        std::cout << std::left << std::setw(column_width)
                  << (ue22_support ? "u: Update UnrealTournament.ini" : "") << std::endl;


        print_title_value("UE22 support: ", ue22_support ? "Yes" : "No");
        setColor(14); // Yellow
        std::cout << std::left << std::setw(column_width)
                  << (ue22_support ? "c: Clean UE2 packages" : "") << std::endl;

        print_title_value("UE22x support: ", ue22x_support ? "Yes" : "No");
 

        setColor(11); // Cyan
        std::cout << std::endl;
        std::cout << std::string(column_width * 3, '=') << std::endl;
        resetColor();
        std::cout << std::endl;

        const bool show_ue2_packages = ue22_support && !ue2_edit_packages.empty();
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

        if (input == "p" || input == "P") {
            syncEditPackages();
            continue;
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

        if ((input == "u" || input == "U") && ue22_support) {
            updateUnrealTournamentIniManual();
            continue;
        }

        if ((input == "c" || input == "C") && ue22_support) {
            cleanUnrealTournamentIni();
            continue;
        }

        if (input == "b" || input == "B") {
            toggleBlacklistPackages();
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
                } else if (ue22_support && !ue2_edit_packages.empty()) {
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
