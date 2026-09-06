#pragma once

#include <string>
#include <vector>

#define MOJO_MAKE_VERSION "1.2.3"

class DeusExBuilder {
private:
    std::string project_name;
    std::string game_path;
    std::string project_path;
    std::vector<std::string> project_edit_packages;
    std::vector<std::string> ue2_edit_packages;
    std::vector<std::string> blacklist_packages;
    bool ue2_support;
    bool ue22x_support;
    bool ucc_exists;
    bool lcc_exists;
    std::string compiler;

    std::string system_dir;
    std::string ued22_dir;
    std::string ued22x_dir;
    std::string classes_dir;
    std::string project_system_dir;
    std::string config_path;

    // Private methods
    bool loadOrCreateConfig();
    bool validateGamePath();
    bool validateProjectPath();
    bool setupProjectIni();
    bool syncEditPackages();
    void scanForUE2Compatibility();
    std::vector<std::string> findGameRootsFromCwd();
    void updateUnrealTournamentIniManual();
    void cleanUnrealTournamentIni();
    void performInitialPackageScan();
    void configureConsoleWindow();
    void process_exclusive_code(int version, bool is_enabled, const std::string& package = "");
    bool updateDeusExIni(bool add_packages, bool ue2 = false, const std::vector<std::string>* packages_override = nullptr);
    bool updateUnrealTournamentIni(bool add_packages, const std::vector<std::string>* packages_override = nullptr);
    void backupAndRemoveUFiles(const std::vector<std::string>& packages, bool ue2 = false);
    void moveCompiledFiles(const std::vector<std::string>& packages, bool ue2 = false);
    int runCompiler(const std::string& ucc_path, const std::string& args = "");
    void compileSinglePackage(const std::string& package, bool ue2);
    void compileAllPackages(bool ue2);
    void toggleBlacklistPackages();
    bool ensureBlacklistedUFilesInGameSystem(const std::vector<std::string>& packages, bool ue2 = false);
    bool setupCompiler();
    void toggleCompiler();

public:
    DeusExBuilder();
    bool initialize();
    void showMenu();
};