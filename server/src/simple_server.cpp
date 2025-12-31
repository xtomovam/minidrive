#include "simple_server.hpp"
#include "session.hpp"

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


const std::string download(const int &client_fd, const std::string &cmd) {
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

void start_simple_server(const std::uint16_t &port, const std::string &root) {
    // verify root directory exists
    if (!std::filesystem::exists("./" +root)) {
        std::cout << "Root directory does not exist: " << root << std::endl;
        return;
    }

    // if no public directory, create it
    if (!std::filesystem::exists(root + "/public")) {
        std::filesystem::create_directory(root + "/public");
    }

    // create listen socket
    int listen_fd = create_listen_socket(port);
    if (listen_fd < 0) {
        std::cerr << "Failed to set up listen socket on port " << port << std::endl;
        return;
    }
    std::cout << "Simple server listening on port " << port << std::endl;

    std::unordered_map<int, std::unique_ptr<Session>> sessions;

    // main server loop
    std::vector<int> toClose;
    while (true) {
        toClose.clear();
        
        // add all client fds to sets
        fd_set readfds;
        fd_set writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        FD_SET(listen_fd, &readfds);
        int maxfd = listen_fd;
        for (auto &p : sessions) {
            FD_SET(p.first, &readfds);
            if (p.second->getState() == Session::State::DownloadingFile) {
                FD_SET(p.first, &writefds);
            }
            if (p.first > maxfd) maxfd = p.first;
        }

        // wait for event
        int activity = select(maxfd + 1, &readfds, &writefds, nullptr, nullptr);
        if (activity < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // new client -> accept connection and create session
        if (FD_ISSET(listen_fd, &readfds)) {
            // accept new connection
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = ::accept(listen_fd,
                                     reinterpret_cast<sockaddr*>(&client_addr),
                                     &client_len);
            if (client_fd < 0) {
                perror("accept");
                continue;
            }
            char ipbuf[INET_ADDRSTRLEN];
            const char* ipstr = ::inet_ntop(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
            if (ipstr) {
                std::cout << "Client connected from " << ipstr << ":" << ntohs(client_addr.sin_port) << std::endl;
            }

            // create session
            sessions.emplace(client_fd,
                std::make_unique<Session>(client_fd, root, [&](int fd){
                    toClose.push_back(fd);
                })
            );
        }

        // handle existing clients
        for (auto &p : sessions) {
            int fd = p.first;

            // handle active downloads when socket is writable
            if (p.second->getState() == Session::State::DownloadingFile && FD_ISSET(fd, &writefds)) {
                try {
                    p.second->downloadFileChunk();
                } catch (const std::exception &e) {
                    std::cerr << "Error sending file to client " << fd << ": " << e.what() << "\n";
                    toClose.push_back(fd);
                }
                continue;
            }

            // existing client sent message -> read and process
            if (FD_ISSET(fd, &readfds)) {
                std::string msg;

                // session waits for file -> delegate to flow
                if (sessions[fd]->getState() == Session::State::AwaitingFile) {
                    try {
                        sessions[fd]->onMessage("");
                    } catch (const std::exception &e) {
                        std::cerr << "Error processing file for client " << fd << ": " << e.what() << "\n";
                        toClose.push_back(fd);
                    }
                    continue;
                }

                try {
                    msg = recv_msg(fd);
                } catch (const std::exception &e) {
                    if (std::string(e.what()).find("connection_closed") != std::string::npos) {
                        std::cout << "Client " << fd << " disconnected\n";
                        toClose.push_back(fd);
                        continue;
                    } else {
                        std::cerr << "Error receiving message from client " << fd << ": " << e.what() << "\n";
                        continue;
                    }
                }

                if (msg.empty()) {
                    // client disconnected
                    std::cout << "Client " << fd << " disconnected\n";
                    toClose.push_back(fd);
                    continue;
                }

                std::cout << "Received (" << msg.size() << " bytes) from fd=" << fd << ": " << msg << "\n";

                // delegate session logic
                p.second->onMessage(msg);
            }
        }

        // close disconnected sessions
        for (int fd : toClose) {
            ::close(fd);
            sessions.erase(fd);
        }
    }

    ::close(listen_fd);
}
