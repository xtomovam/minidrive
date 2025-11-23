#include "minidrive/helpers.hpp"
#include <iostream>

bool is_cmd(const std::string &msg, const std::string &cmd) {
    return msg.starts_with(cmd) && (msg.size() == cmd.size() || msg[cmd.size()] == ' ');
}

const std::string word_from(const std::string &str, const size_t &start) {
    if (start >= str.size()) {
        return "";
    }

    std::string word;
    size_t pos = str.find(' ', start);
    if (pos == std::string::npos) {
        word = str.substr(start);
    } else {
        word = str.substr(start, pos - start);
    }
    return word;
}

const std::string nth_word(const std::string &str, const size_t &n) {
    size_t count = 0;
    size_t start = 0;
    while (count < n && start < str.size()) {
        size_t pos = str.find(' ', start);
        if (pos == std::string::npos) {
            return "";
        }
        start = pos + 1;
        count++;
    }
    return word_from(str, start);
}

const std::vector<std::string> split_cmd(const std::string &cmd) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < cmd.size()) {
        size_t pos = cmd.find(' ', start);
        if (pos == std::string::npos) {
            parts.push_back(cmd.substr(start));
            break;
        } else {
            parts.push_back(cmd.substr(start, pos - start));
            start = pos + 1;
        }
    }
    return parts;
}

size_t receive_length_prefix(const int &fd) {
    char c = '\0';
    size_t length = 0;
    while(true) {
        ssize_t recvd = ::recv(fd, &c, 1, 0);
        if (recvd < 0) {
            throw std::runtime_error("recv: Failed to receive length");
        }
        if (recvd == 0) {
            throw std::runtime_error("connection_closed: Connection closed by remote node");
        }
        if (c == ' ') {
            break;
        }
        length *= 10;
        length += c - '0';
    }
    return length;
}

const std::string recv_msg(const int &fd) {
    std::string result;
    
    // receive message length
    size_t len = receive_length_prefix(fd);
    
    // receive full message
    size_t remaining = len;
    char temp[TMP_BUFF_SIZE];
    while (remaining > 0) {
        ssize_t recvd = ::recv(fd, temp, sizeof(temp), 0);
        if (recvd < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("recv: Failed to receive message");
        }
        if (recvd == 0) {
            throw std::runtime_error("connection_closed: Connection closed by remote node");
        }
        result.append(temp, static_cast<size_t>(recvd));
        remaining -= static_cast<size_t>(recvd);
    }
    return result;
}

void send_msg(const int &fd, const std::string &msg) {
    // add length prefix
    size_t len = msg.size();
    std::string full_msg = std::to_string(len) + ' ' + msg;

    // send full message
    ssize_t total_sent = 0;
    ssize_t total_size = static_cast<ssize_t>(full_msg.size());
    while (total_sent < total_size) {
        ssize_t sent = ::send(fd, full_msg.c_str() + total_sent, static_cast<size_t>(total_size - total_sent), 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("send: Failed to send message");
        }
        total_sent += sent;
    }
    //std::cout << "Sent message (" << total_size << " bytes)" << std::endl;
}

void recv_file(const int &fd, const std::string &filepath, const std::string &user_dir, const size_t offset) {
    namespace fs = std::filesystem;
    std::string err = "";
    std::ofstream outfile;

    // open file for writing
    if (offset == 0 && fs::exists(filepath) && fs::is_regular_file(filepath)) {
        err = "overwrite_error: File already exists"; // prevent overwriting existing files
    } else {
        // ensure parent directory exists
        fs::path file_path(filepath);
        fs::path parent_dir = file_path.parent_path();
        if (!parent_dir.empty() && !fs::exists(parent_dir)) {
            try {
                fs::create_directories(parent_dir);
            } catch (const std::exception& e) {
                err = "directory_create_failed: Failed to create parent directory: " + std::string(e.what());
            }
        }
        
        if (err.empty()) {
            outfile.open(filepath, std::ios::binary | std::ios::app);
            if (!outfile) {
                err = "file_open_failed: Failed to open file for writing (path: " + filepath + ")";
            }
        }
    }

    // receive file length
    size_t length = receive_length_prefix(fd);
    size_t chunks = length / TMP_BUFF_SIZE + ((length % TMP_BUFF_SIZE) ? 1 : 0);
    std::cout << "Receiving file of size " << length << " bytes (" << chunks << " chunks) to '" << filepath << "'\n";

    // receive file data
    size_t remaining = length;
    size_t chunks_received = 0;
    char temp[TMP_BUFF_SIZE];
    while (remaining > 0) {
        ssize_t recvd = ::recv(fd, temp, (remaining < sizeof(temp)) ? remaining : sizeof(temp), 0);
        if (recvd < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("recv: Failed to receive file data");
        }
        if (recvd == 0) {
            throw std::runtime_error("connection_closed: Connection closed by remote node");
        }
        if (err.empty()) {
            // write to file
            outfile.write(temp, static_cast<std::streamsize>(recvd));
            if (!outfile) {
                throw std::runtime_error("file_write_failed: Failed to write to file");
            }
            
            // update transfer progress
            size_t file_size = fs::file_size(filepath);
            TransferState::updateProgress(user_dir, filepath, file_size);
        }
        remaining -= static_cast<size_t>(recvd);
        chunks_received++;
    }

    if (!err.empty()) {
        throw std::runtime_error(err);
    }
}

void send_file(const int &fd, const std::string &filepath) {
    // open file
    std::ifstream infile(filepath, std::ios::binary);
    if (!infile) {
        throw std::runtime_error("file_open_failed: Failed to open file for reading (path: " + filepath + ")");
    }

    // send file size
    infile.seekg(0, std::ios::end);
    size_t file_size = static_cast<size_t>(infile.tellg());
    infile.seekg(0, std::ios::beg);
    ssize_t sent = ::send(fd, (std::to_string(file_size) + ' ').c_str(), (std::to_string(file_size) + ' ').size(), 0);
    if (sent < 0) {
        throw std::runtime_error("send_failed: Failed to send file size");
    }

    // send file data
    char temp[TMP_BUFF_SIZE];
    while (true) {
        infile.read(temp, sizeof(temp));
        std::streamsize read_bytes = infile.gcount();
        if (read_bytes <= 0) {
            break; // EOF
        }
        ssize_t sent = ::send(fd, temp, static_cast<size_t>(read_bytes), 0);
        if (sent < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("send_failed: Failed to send file data");
        }
    }
}