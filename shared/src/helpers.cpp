#include "minidrive/helpers.hpp"
#include <iostream>
std::string receive(const int &fd) {
    std::string result;
    char temp[256];
    while (result.find('\n') == std::string::npos) {
        ssize_t recvd = ::recv(fd, temp, sizeof(temp) - 1, 0);
        if (recvd < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("recv");
        }
        if (recvd == 0) {
            throw std::runtime_error("connection_closed");
        }
        result.append(temp, static_cast<size_t>(recvd));
        if (result.find('\n') != std::string::npos) {
            break;
        }
    }
    return result;
}