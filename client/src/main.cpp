#include "minidrive/version.hpp"
#include "minidrive/helpers.hpp"
#include "minidrive/transfer_state.hpp"

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
#include <vector>

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

void download(const int &fd, const std::string &cmd) {    
    // parse command
    std::vector<std::string> parts = split_cmd(cmd);
    if (parts.size() < 2) {
        throw std::runtime_error("invalid_command: DOWNLOAD command requires a path argument");
    }
    std::string local_path = parts.size() >= 3 ? parts[2] : parts[1] + ".part";

    // check if local file exists
    if (std::filesystem::exists(local_path)) {
        throw std::runtime_error("file_exists: Local file already exists: " + local_path);
    }

    // send command
    send_msg(fd, cmd);

    // receive FILEINFO response
    std::string response = recv_msg(fd);
    if (!is_cmd(response, "FILEINFO")) {
        throw std::runtime_error("unknown_response: Expected FILEINFO response, got " + response);
    }
    parts = split_cmd(response);
    if (parts.size() < 3) {
        if (parts.size() > 0 && parts[0] == "ERROR") {
            throw std::runtime_error(response.substr(6));
        }
        throw std::runtime_error("invalid_response: FILEINFO response requires path and size arguments");
    }
    std::string &remote_path = parts[1]; // full remote path
    size_t file_size = std::stoull(parts[2]);

    // create transfer state entry
    TransferState::Transfer transfer;
    transfer.local_path = local_path;
    transfer.remote_path = remote_path;
    transfer.bytes_completed = 0;
    transfer.total_bytes = file_size;
    transfer.timestamp = std::to_string(std::time(nullptr));
    TransferState::addTransfer(".", transfer);

    // receive file in chunks
    while (transfer.bytes_completed < transfer.total_bytes) {
        size_t bytes_left = transfer.total_bytes - transfer.bytes_completed;
        size_t to_recv = bytes_left < TMP_BUFF_SIZE ? bytes_left : TMP_BUFF_SIZE;
        size_t recvd = recv_file_chunk(fd, local_path, transfer.bytes_completed, to_recv);
        transfer.bytes_completed += recvd;
        TransferState::updateProgress(".", remote_path, transfer.bytes_completed);
    }
    
    // finalize
    std::filesystem::rename(local_path, local_path.substr(0, local_path.size() - 5));
    local_path = local_path.substr(0, local_path.size() - 5);
    TransferState::removeTransfer(".", remote_path);
    std::cout << "OK\nFile downloaded successfully to " << local_path << std::endl;
}

void upload(const int &fd, const std::string &cmd) {
    // parse local path
    std::vector<std::string> parts = split_cmd(cmd);
    if (parts.size() < 2) {
        throw std::runtime_error("invalid_command: UPLOAD command requires a path argument");
    }
    std::string local_path = parts[1];

    // send cmd with file size
    size_t file_size = std::filesystem::file_size(local_path);
    std::string new_cmd = "UPLOAD " + std::to_string(file_size) + cmd.substr(parts[0].size());
    send_msg(fd, new_cmd);

    // READY response -> send file
    std::string response = recv_msg(fd);
    parts = split_cmd(response);
    if (parts.size() == 1 && parts[0] == "READY") {
        send_file(fd, local_path, 0);
        std::cout << recv_msg(fd) << std::endl;
    } else {
        std::cout << response << std::flush;
    }
}

void resume(const int &fd) {
    bool found_smth = false;

    // receive RESUME command from server
    std::cout << "Checking for incomplete uploads/downloads...\n" << std::flush;
    std::string cmd = recv_msg(fd);
    if (!is_cmd(cmd, "RESUME")) {
        throw std::runtime_error("unknown_response: Expected RESUME command, got " + cmd);
    }
    std::vector<std::string> parts = split_cmd(cmd);

    // incomplete upload transfer -> prompt user to resume
    if (parts.size() >= 3) {
        std::cout << "Incomplete uploads detected, resume? (y/n):\n> " << std::flush;
        std::string answer;
        std::getline(std::cin, answer);
        send_msg(fd, answer);
        
        // users chooses to resume -> resume transfer
        if (answer == "y") {
            std::cout << "Resuming upload of file '" << parts[1] << "' from offset " << parts[3] << "...\n" << std::flush;
            size_t offset = std::stoull(parts[3]);
            send_file(fd, parts[1], offset);
            std::cout << recv_msg(fd) << std::endl;
        }

        found_smth = true;
    }

    // check for incomplete downloads
    TransferState::clearTransfers(".");
    std::vector<TransferState::Transfer> transfers = TransferState::getActiveTransfers(".");

    // incomplete downloads found -> prompt to resume
    if (!transfers.empty()) {
        std::cout << "Incomplete downloads detected, resume? (y/n)\n>" << std::flush;
        std::string answer;
        std::getline(std::cin, answer);

        // user chooses to resume -> resume downloads
        if (answer == "y") {
            for (const auto& transfer : transfers) {
                std::cout << "Resuming download of file '" << transfer.remote_path << "' from offset " << transfer.bytes_completed << "...\n" << std::flush;

                // send RESUME command
                send_msg(fd, "RESUME " + transfer.remote_path + " " + std::to_string(transfer.bytes_completed));
        
                // receive file in chunks
                size_t bytes_completed = transfer.bytes_completed;
                while (bytes_completed < transfer.total_bytes) {
                    size_t bytes_left = transfer.total_bytes - bytes_completed;
                    size_t to_recv = bytes_left < TMP_BUFF_SIZE ? bytes_left : TMP_BUFF_SIZE;
                    size_t recvd = recv_file_chunk(fd, transfer.local_path, bytes_completed, to_recv);
                    bytes_completed += recvd;
                    TransferState::updateProgress(".", transfer.remote_path, bytes_completed);
                }
        
                // finalize
                std::cout << "\nOK\nFile downloaded successfully to " << transfer.local_path << std::endl;
                TransferState::removeTransfer(".", transfer.remote_path);
            }
            found_smth = true;
        }
    }
    if (!found_smth) {
        std::cout << "No incomplete uploads/downloads found." << std::endl;
    }
}

void authenticate(const int &fd, const std::string &user) {
    if (user.empty()) {
        std::cout << "[warning] operating in public mode - files are visible to everyone" << std::endl;
    }

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
        
        // server promts for registration -> send answers and wait for result
        } else if (response.starts_with("User " + user + " not found")) {
            std::string answer;
            std::getline(std::cin, answer);
            send_msg(fd, answer);
            std::cout << recv_msg(fd) << std::endl;
            if (answer == "y") {
                std::getline(std::cin, answer);
                send_msg(fd, answer);
                std::cout << recv_msg(fd) << std::endl;
            }
        } else {
            throw std::runtime_error("unknown_response: Unknown authentication response: " + response);
        }
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
            if (is_cmd(cmd, ("HELP"))) {
                print_help();
            } else if (is_cmd(cmd, ("EXIT"))) {
                if (mode == Mode::Remote) {
                    send_msg(fd, "EXIT");
                }
                std::cout << "Exiting...\n";
                return;

            // not a local command -> send to server if in remote mode
            } else {
                if (mode == Mode::Remote) {
                    try {
                        std::vector<std::string> parts = split_cmd(cmd);
                        if (parts[0] == "DOWNLOAD") {
                            download(fd, cmd);                        
                        } else if (parts[0] == "UPLOAD") {
                            upload(fd, cmd);
                        } else {
                            send_msg(fd, cmd);
                            std::cout << recv_msg(fd) << std::endl;
                        }
                    } catch (const std::exception &e) {
                        std::cerr << "ERROR: " << e.what() << std::flush;
                        if (std::string(e.what()).find("connection_closed") != std::string::npos) {
                            std::cerr << "\nConnection to server lost. Exiting...\n";
                            return;
                        }
                    }
                } else {
                    std::cout << "Unknown command: " << cmd << std::flush;
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
        resume(fd);
        main_loop(fd, Mode::Remote);
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        ::close(fd);
        return 1;
    }

    ::close(fd);
    return 0;
}
