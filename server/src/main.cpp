#include <iostream>
#include <string>
#include <cstdint>

#include "minidrive/version.hpp"
#include "simple_server.hpp"

int main(int argc, char* argv[]) {
    // Echo full command line once for diagnostics
    std::cout << "[cmd]";
    for (int i = 0; i < argc; ++i) {
        std::cout << " \"" << argv[i] << '"';
    }
    std::cout << std::endl;
    std::uint16_t port = 9000; // default
    std::string root = "";
    std::string log_file = "log.txt";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        } else if (arg.starts_with("--root")) {
            root = std::string(argv[++i]);
        } else if (arg.starts_with("--log")) {
            log_file = std::string(argv[++i]);
        }
    }

    if (root.empty()) {
        std::cerr << "Error: --root <path> argument is required" << std::endl;
        return 1;
    }

    std::cout << "Starting simple server (version " << minidrive::version() << ") on port " << port << std::endl;
    start_simple_server(port, root, log_file);
    std::cout << "Server exited." << std::endl;
    return 0;
}
