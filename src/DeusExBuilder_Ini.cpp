#include "DeusExBuilder.h"
#include "IniFile.h"
#include <iostream>
#include <filesystem>
#include <set>
#include <algorithm>

namespace fs = std::filesystem;

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
