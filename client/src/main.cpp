#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "minidrive/version.hpp"

#include <signal.h>

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
}

enum class Mode {
    Local,
    Remote
};

void main_loop(const int fd, Mode mode) {
    std::string input_buffer;
    char temp[256];

    std::cout << "> " << std::flush;

    while (true) {
        // read up to 256 bytes from stdin
        ssize_t read_bytes = ::read(STDIN_FILENO, temp, sizeof(temp));
        if (read_bytes < 0) {
            perror("read");
            break;
        }
        if (read_bytes == 0) {
            std::cout << "stdin closed\n";
            break;
        }
        input_buffer.append(temp, static_cast<size_t>(read_bytes));

        // process complete lines
        size_t pos;
        while ((pos = input_buffer.find('\n')) != std::string::npos) {
            std::string cmd = input_buffer.substr(0, pos);
            input_buffer.erase(0, pos + 1);

            if (cmd == "HELP") {
                print_help();
            } else if (cmd == "EXIT") {
                std::cout << "Exiting...\n";
                return;
            } else {
                // not a local command -> send to server if in remote mode
                if (mode == Mode::Remote) {
                    // check connection
                    char tmp;
                    ssize_t r = recv(fd, &tmp, 1, MSG_PEEK | MSG_DONTWAIT);
                    if (r == 0) {
                        std::cout << "ERROR closed_connection : connection closed by server. Exitting...\n";
                        return;
                    }

                    cmd.append("\n"); // append newline
                    ssize_t sent = ::send(fd, cmd.c_str(), cmd.size(), 0);
                    if (sent < 0) {
                        std::perror("send");
                    } else {
                        std::cout << "Sent command to server (" << sent << " bytes)" << std::endl;
                        // wait for response
                        std::string response;
                        while (true) {
                            ssize_t recvd = ::recv(fd, temp, sizeof(temp) - 1, 0);
                            if (recvd < 0) {
                                if (errno == EINTR) continue;
                                std::perror("ERROR recv : failed to receive response from server");
                                break;
                            }
                            if (recvd == 0) {
                                std::perror("ERROR closed_connection : connection closed by server");
                                return;
                            }
                            response.append(temp, static_cast<size_t>(recvd));

                            size_t pos = response.find('\n');
                            if (pos != std::string::npos) {
                                break;
                            }

                        }
                        std::cout << "OK\n" << response << std::flush;
                    }
                } else {
                    std::cout << "Unknown command: " << cmd << std::endl;
                }
            }
            
            std::cout << "> " << std::flush;
        }
    }
}

int main(int argc, char* argv[]) {
    ::signal(SIGPIPE, SIG_IGN);
    
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
    
    HostPort hp;
    if (!parse_host_port(argv[1], hp)) {
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

    main_loop(fd, Mode::Remote);

    ::close(fd);
    return 0;
}
