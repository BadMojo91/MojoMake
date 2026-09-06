#include "DeusExBuilder.h"
#include <iostream>
#include <filesystem>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;

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
			// For UE2: copy from UED22 to UED22x so both versions have the compiled .u
            std::string ued22_file = ued22_dir + "/" + pkg + ".u";
            if (fs::exists(ued22_file)) {
                std::cout << "Compiled " << pkg << ".u for UE2 (in UED22 folder)" << std::endl;

                if (ue22x_support) {
					std::cout << "Copying " << pkg << ".u to UED22x folder..." << std::endl;
                    fs::copy_file(ued22_file, ued22x_dir + "/" + pkg + ".u", fs::copy_options::overwrite_existing);
                }
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
