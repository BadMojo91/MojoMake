#include "DeusExBuilder.h"
#include "DeusExBuilder.h"

DeusExBuilder::DeusExBuilder() : ue22_support(false), ucc_exists(false), lcc_exists(false), compiler("UCC") {}

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
