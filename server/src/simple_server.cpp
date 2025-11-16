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
        std::istringstream iss(cmd.substr(5));
        iss >> path;
    }

    std::ostringstream out;
    size_t n = 0;
    for (const auto &entry : fs::directory_iterator(path)) {
        if (fs::is_directory(entry.status())) {
            out << "[DIR]  ";
        } else {
            out << "       ";
        }

        out << entry.path().filename().string() << "\n";
        n++;
    }

    if (n == 0) {
        out << "\n";
    }

    return out.str();
}

std::string cd(const std::string &cmd) {
    namespace fs = std::filesystem;
    if (cmd.size() <= 3) {
        throw std::runtime_error("no_path: CD command requires a path argument");
    }
    std::string path = cmd.substr(3);
    std::cout << "Changing directory to: " << path << std::endl;
    fs::current_path(path);

    return "Changed directory to " + path;
}

const std::string upload(const int &client_fd, const std::string &cmd) {
    // parse command
    std::string client_path;
    std::string server_path;
    if (cmd.size() <= 7) {
        throw std::runtime_error("no_path: UPLOAD command requires a path argument");
    }
    std::istringstream iss(cmd.substr(7));
    if (!(iss >> client_path)) {
        throw std::runtime_error("no_path: UPLOAD command requires a path argument");
    }
    if (!(iss >> server_path)) {
        server_path = client_path.substr(client_path.find_last_of("/\\") + 1);
    }

    // receive and save file data
    recv_file(client_fd, server_path);

    return "Uploaded " + client_path + " to " + server_path;
}

std::string download(const int &client_fd, const std::string &cmd) {
    // parse command
    std::string server_path;
    std::string client_path;
    if (cmd.size() <= 9) {
        throw std::runtime_error("no_path: DOWNLOAD command requires a path argument");
    }
    std::istringstream iss(cmd.substr(9));
    if (!(iss >> server_path)) {
        throw std::runtime_error("no_path: DOWNLOAD command requires a path argument");
    }
    if (!(iss >> client_path)) {
        client_path = server_path.substr(server_path.find_last_of("/\\") + 1);
    }

    // send file data
    send_file(client_fd, server_path);

    return "Downloaded " + server_path + " to " + client_path;
}

const std::string process_command(const int client_fd, const std::string &cmd) {
    try {
        if (cmd.starts_with("LIST")) {
            return "OK " + list(cmd);
        } 
        if (cmd.starts_with("CD")) {
            return "OK " + cd(cmd);
        } 
        if (cmd.starts_with("UPLOAD")) {
            return "OK " + upload(client_fd, cmd);
        }
        if (cmd.starts_with("DOWNLOAD")) {
            return "OK " + download(client_fd, cmd);
        }
        return "ERROR unknown_command: Unknown command: " + cmd;
    } catch (const std::exception &e) {
        return std::string("ERROR ") + e.what();
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

        while (true) {
            std::string message = recv_msg(client_fd);
            std::cout << "Received line (" << message.size() << " bytes): " << message << std::endl;
            std::string response = process_command(client_fd, message);

            send_msg(client_fd, response);
        }
        ::close(client_fd);
    }

    ::close(listen_fd);
}
