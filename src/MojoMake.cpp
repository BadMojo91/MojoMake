#include "DeusExBuilder.h"
#include <iostream>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << std::endl;
}

int main(int argc, char* argv[]) {
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    std::cout << std::endl;

    DeusExBuilder builder;
    
    if (!builder.initialize()) {
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    builder.showMenu();
    return 0;
}