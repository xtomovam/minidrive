#include "simple_server.hpp"
#include "../../shared/include/minidrive/helpers.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include <filesystem>
#include <sstream>
#include <fstream>

namespace {

int create_listen_socket(std::uint16_t port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::perror("socket");
        return -1;
    }

    int enable = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) < 0) {
        std::perror("setsockopt");
        ::close(fd);
        return -1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(fd);
        return -1;
    }

    // Allow a small backlog so multiple clients can queue while one is being processed.
    if (::listen(fd, 8) < 0) {
        std::perror("listen");
        ::close(fd);
        return -1;
    }
    return fd;
}

}

std::string list(const std::string &cmd) {
    namespace fs = std::filesystem;

    std::string path = ".";
    if (cmd.size() > 5) {
        path = cmd.substr(5);
    }

    std::ostringstream out;

    try {
        for (const auto &entry : fs::directory_iterator(path)) {
            if (fs::is_directory(entry.status())) {
                out << "[DIR]  ";
            } else {
                out << "       ";
            }

            out << entry.path().filename().string() << "\n";
        }
    } catch (const std::exception &e) {
        return std::string("ERROR: ") + e.what() + "\n";
    }

    return std::string("OK ") + out.str();
}

std::string cd(const std::string &cmd) {
    namespace fs = std::filesystem;

    std::string path = ".";
    if (cmd.size() > 3) {
        path = cmd.substr(3);
    } else {
        return "ERROR no_path: CD command requires a path argument\n";
    }

    try {
        fs::current_path(path);
    } catch (const std::exception &e) {
        return std::string("ERROR: ") + e.what() + "\n";
    }

    return "OK Changed directory to " + path + "\n";
}

const std::string upload(const int &client_fd, const std::string &cmd) {
    // parse command
    if (cmd.size() <= 7) {
        return "ERROR no_path: UPLOAD command requires a path argument\n";
    }
    std::istringstream iss(cmd.substr(7));
    std::string client_path;
    std::string server_path;
    if (!(iss >> client_path)) {
        return "ERROR no_path: UPLOAD command requires a client path argument\n";
    }
    if (!(iss >> server_path)) {
        server_path = client_path[client_path.find_last_of("/\\") + 1];
    }

    // send request for file size
    std::string request = "FILESIZE " + client_path + "\n";
    ssize_t sent = ::send(client_fd, request.c_str(), request.size(), 0);
    if (sent < 0) {
        return "ERROR send_failed: failed to send FILESIZE request\n";
    }

    // receive file size
    uint64_t file_size;
    std::string response = receive(client_fd);
    file_size = std::stoull(response);


    // send request for file data
    request = "FILEDATA " + client_path + " " + std::to_string(file_size) + "\n";
    sent = ::send(client_fd, request.c_str(), request.size(), 0);
    if (sent < 0) {
        return "ERROR send_failed: failed to send FILEDATA request\n";
    }

    // receive and save file data
    std::ofstream outfile(server_path, std::ios::binary);
    if (!outfile) {
        return "ERROR file_open_failed: failed to open file for writing on server\n";
    }
    uint64_t remaining = file_size;
    char temp[256];
    while (remaining > 0) {
        ssize_t recvd = ::recv(client_fd, temp, (remaining < sizeof(temp)) ? static_cast<size_t>(remaining) : sizeof(temp), 0);
        if (recvd < 0) {
            if (errno == EINTR) continue;
            return "ERROR recv_failed: failed to receive file data\n";
        }
        if (recvd == 0) {
            return "ERROR connection_closed: connection closed by client\n";
        }
        outfile.write(temp, static_cast<std::streamsize>(recvd));
        if (!outfile) {
            return "ERROR file_write_failed: failed to write to file on server\n";
        }
        remaining -= static_cast<uint64_t>(recvd);
    }

    return "OK uploaded " + client_path + " to " + server_path + "\n";
}

const std::string process_command(const int client_fd, const std::string &cmd) {
    if (cmd.starts_with("LIST")) {
        return list(cmd);
    } else if (cmd.starts_with("CD")) {
        return cd(cmd);
    } else if (cmd.starts_with("UPLOAD")) {
        try {
            return upload(client_fd,cmd);
        } catch (const std::exception &e) {
            return std::string("ERROR: ") + e.what() + "\n";
        }
    }
    return "OK \n";
}

void start_simple_server(std::uint16_t port) {
    // set server root directory
    try {
        std::filesystem::current_path("server/root");
    } catch (const std::exception &e) {
        std::cerr << "Failed to set server root directory: " << e.what() << std::endl;
        return;
    }

    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        std::cerr << "Failed to set up listen socket on port " << port << std::endl;
        return;
    }
    std::cout << "Simple server listening on port " << port << std::endl;

    // Accept loop: handle clients sequentially. For each client, accumulate full message then print once.
    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (errno == EINTR) continue; // retry accept
            std::perror("accept");
            break; // fatal accept error -> exit server
        }

        char ipbuf[INET_ADDRSTRLEN];
        const char* ipstr = ::inet_ntop(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
        if (ipstr) {
            std::cout << "Client connected from " << ipstr << ":" << ntohs(client_addr.sin_port) << std::endl;
        }

        constexpr std::size_t BUF_SIZE = 4096;
        char buffer[BUF_SIZE];
        std::string message;
        while (true) {
            ssize_t n = ::recv(client_fd, buffer, BUF_SIZE, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                std::perror("recv");
                break;
            }
            if (n == 0) { // EOF
                std::cout << "Client disconnected" << std::endl;
                break;
            }

            message.append(buffer, static_cast<std::size_t>(n));

            // if newline in message, send response for each line
            std::size_t pos;
            while ((pos = message.find('\n')) != std::string::npos) {
                std::string line = message.substr(0, pos);
                message.erase(0, pos + 1);
                std::cout << "Received line (" << line.size() << " bytes): " << line << std::endl;
                std::string response = process_command(client_fd, line);

                ssize_t sent = ::send(client_fd, response.c_str(), response.size(), 0);
                if (sent < 0) {
                    std::perror("send");
                    break;
                }
            }
        }
        ::close(client_fd);
    }

    ::close(listen_fd);
}
