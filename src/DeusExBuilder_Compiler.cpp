#include "DeusExBuilder.h"
#include "IniFile.h"
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

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

int DeusExBuilder::runCompiler(const std::string& ucc_path, const std::string& args) {
    std::string command = "\"" + ucc_path + "\" make " + args;
    return system(command.c_str());
}
