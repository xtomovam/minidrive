#include "minidrive/helpers.hpp"
#include <iostream>

std::string recv_msg(const int &fd) {
    std::string result;
    char temp[TMP_BUFF_SIZE];
    
    // parse length
    ssize_t recvd = ::recv(fd, temp, sizeof(temp) - 1, 0);
    result.append(temp, static_cast<size_t>(recvd));
    size_t len = std::stoull(result.substr(0, result.find(' ')));
    result.erase(0, result.find(' ') + 1);

    size_t remaining = len - result.size();
    while (remaining > 0) {
        ssize_t recvd = ::recv(fd, temp, sizeof(temp) - 1, 0);
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
}

void recv_file(const int &fd, const std::string &filepath, const size_t &size) {
    std::ofstream outfile(filepath, std::ios::binary);
    if (!outfile) {
        throw std::runtime_error("file_open_failed: Failed to open file for writing");
    }
    size_t remaining = size;
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
        outfile.write(temp, static_cast<std::streamsize>(recvd));
        if (!outfile) {
            throw std::runtime_error("file_write_failed: Failed to write to file");
        }
        remaining -= static_cast<size_t>(recvd);
    }
}

void send_file(const int &fd, const std::string &filepath) {
    std::ifstream infile(filepath, std::ios::binary);
    if (!infile) {
        throw std::runtime_error("file_open_failed: Failed to open file for reading");
    }
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