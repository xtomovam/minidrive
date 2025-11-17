#include "minidrive/version.hpp"
#include "minidrive/helpers.hpp"

#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <signal.h>
#include <sstream>
#include <fstream>
#include <filesystem>

struct HostPort {
    std::string host;
    uint16_t port{};
};

static bool parse_host_port(const std::string& input, HostPort& out) {
    auto colon = input.rfind(':');
    if (colon == std::string::npos) return false;
    std::string host = input.substr(0, colon);
    std::string port_str = input.substr(colon + 1);
    if (host.empty() || port_str.empty()) return false;
    char* end = nullptr;
    long p = std::strtol(port_str.c_str(), &end, 10);
    if (*end != '\0' || p < 0 || p > 65535) return false;
    out.host = std::move(host);
    out.port = static_cast<uint16_t>(p);
    return true;
}

void print_help() {
    std::cout << "Available commands:\n";
    std::cout << "HELP - Show this help message\n";
    std::cout << "EXIT - Exit the client\n";
    std::cout << "LIST [path] - List files in the specified directory (default: current directory)\n";
    std::cout << "CD <path> - Change the current directory to the specified path\n";
    std::cout << "UPLOAD <local_path> [remote_path] - Upload a file from the client to the server\n";
    std::cout << "DOWNLOAD <remote_path> [local_path] - Download a file from the server to the client\n";
    std::cout << "DELETE <path> - Delete a file on the server\n";
}

enum class Mode {
    Local,
    Remote
};

void process_response(const std::string &response) {
    // hadnle OK and ERROR responses
    if (response.starts_with("OK")) {
        std::cout << "OK\n" << response.substr(3) << std::flush;
    } else if (response.starts_with("ERROR")) {
        std::cout << response << std::flush;
    }
}

void send_cmd(const int &fd, const std::string &cmd) {
    send_msg(fd, cmd);
    std::string response;

    // send command
    if (cmd.starts_with("UPLOAD ")) { // upload -> send file
        if (cmd.size() <= 7) {
            throw std::runtime_error("no_path: UPLOAD command requires a path argument");
        }
        send_file(fd, cmd.substr(7, cmd.find(' ', 7) - 7));
    } else if (cmd.starts_with("DOWNLOAD ")) {
        std::istringstream iss(cmd.substr(9));
        std::string filepath;
        iss >> filepath;
        recv_file(fd, filepath);
    }
    std::cout << "Sent command to server (" << cmd.size() << " bytes)" << std::endl;
}

void authenticate(const int &fd, const std::string &user) {
    send_msg(fd, "AUTH " + user);
    if (!user.empty()) {
        std::string response = recv_msg(fd);
        std::cout << response << std::endl;

        // server asks for password -> send answer and wait for result
        if (response.starts_with("Password")) {
            std::string answer;
            std::getline(std::cin, answer);
            send_msg(fd, answer);
            std::cout << recv_msg(fd) << std::endl;
        
        /// server promts for registration -> send answers and wait for result
        } else if (response.starts_with("User " + user + " not found")) {
            std::string answer;
            std::getline(std::cin, answer);
            send_msg(fd, answer);
            std::cout << recv_msg(fd) << std::endl;
            std::getline(std::cin, answer);
            send_msg(fd, answer);
            std::cout << recv_msg(fd) << std::endl;
        
        } else {
            throw std::runtime_error("unknown_response: Unknown authentication response: " + response);
        }
    } else {
        std::cout << recv_msg(fd) << std::endl;
    }
}

void main_loop(const int &fd, const Mode &mode) {
    std::string input_buffer;
    char temp[TMP_BUFF_SIZE];
    
    std::cout << "> " << std::flush;
    while (true) {
        // read up to TMP_BUFF_SIZE bytes from stdin
        ssize_t read_bytes = ::read(STDIN_FILENO, temp, sizeof(temp));
        if (read_bytes < 0) {
            throw std::runtime_error("read_stdin_failed: Failed to read from stdin");
        }
        if (read_bytes == 0) {
            throw std::runtime_error("stdin_closed: Stdin closed");
        }
        input_buffer.append(temp, static_cast<size_t>(read_bytes));

        // process complete lines
        size_t pos;
        while ((pos = input_buffer.find('\n')) != std::string::npos) {
            std::string cmd = input_buffer.substr(0, pos);
            input_buffer.erase(0, pos + 1);

            // process local commands
            if (cmd == "HELP") {
                print_help();
            } else if (cmd == "EXIT") {
                std::cout << "Exiting...\n";
                return;

            // not a local command -> send to server if in remote mode
            } else {
                if (mode == Mode::Remote) {
                    // send command
                    send_cmd(fd, cmd);

                    // wait for response
                    std::string response = recv_msg(fd);
                    process_response(response);
                } else {
                    std::cout << "Unknown command: " << cmd << std::endl;
                }
            }
            
            std::cout << "\n> " << std::flush;
        }
    }
}

int main(int argc, char* argv[]) {

    // set client root directory
    try {
        std::filesystem::current_path("client/root");
    } catch (const std::exception &e) {
        std::cerr << "Failed to set client root directory: " << e.what() << std::endl;
        return 1;
    }
    
    // Echo full command line once for diagnostics
    std::cout << "[cmd]";
    for (int i = 0; i < argc; ++i) {
        std::cout << " \"" << argv[i] << '"';
    }
    std::cout << std::endl;
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <host>:<port>" << std::endl;
        return 1;
    }
    
    // parse user
    std::string host = argv[1];
    std::string user = "";
    size_t pos = std::string::npos;
    if ((pos = host.find("@")) != std::string::npos) {
        user = host.substr(0, pos);
        host = host.substr(pos + 1);
    }
    
    HostPort hp;
    if (!parse_host_port(host, hp)) {
        std::cerr << "Invalid endpoint format: " << argv[1] << std::endl;
        return 1;
    }
    
    std::cout << "MiniDrive client (version " << minidrive::version() << ")" << std::endl;
    std::cout << "Connecting to " << hp.host << ':' << hp.port << std::endl;
    
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return 2;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(hp.port);
    if (::inet_pton(AF_INET, hp.host.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address: " << hp.host << std::endl;
        ::close(fd);
        return 2;
    }
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("connect");
        ::close(fd);
        main_loop(fd, Mode::Local);
        return 2;
    }
    std::cout << "Connected to server." << std::endl;

    try {
        authenticate(fd, user);
        main_loop(fd, Mode::Remote);
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        ::close(fd);
        return 1;
    }

    ::close(fd);
    return 0;
}
