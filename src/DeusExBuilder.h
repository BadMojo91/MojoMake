#pragma once

#include <string>
#include <vector>

class DeusExBuilder {
private:
    std::string project_name;
    std::string game_path;
    std::string project_path;
    std::vector<std::string> project_edit_packages;
    std::vector<std::string> ue2_edit_packages;
    bool ue2_support;

    std::string system_dir;
    std::string ued22_dir;
    std::string classes_dir;
    std::string project_system_dir;

    // Private methods
    bool loadOrCreateConfig();
    bool validateGamePath();
    bool validateProjectPath();
    bool setupProjectIni();
    bool syncEditPackages();
    void scanForUE2Compatibility();
    void updateUnrealTournamentIniManual();
    void cleanUnrealTournamentIni();
    void performInitialPackageScan();
    void process_exclusive_code(int version, bool is_enabled, const std::string& package = "");
    bool updateDeusExIni(bool add_packages, bool ue2 = false);
    bool updateUnrealTournamentIni(bool add_packages);
    void backupAndRemoveUFiles(const std::vector<std::string>& packages, bool ue2 = false);
    void moveCompiledFiles(const std::vector<std::string>& packages, bool ue2 = false);
    int runCompiler(const std::string& ucc_path, const std::string& args = "");
    void compileSinglePackage(const std::string& package, bool ue2);
    void compileAllPackages(bool ue2);

public:
    DeusExBuilder();
    bool initialize();
    void showMenu();
};