#pragma once

#include <string>
#include <sys/socket.h>
#include <stdexcept>
#include <fstream>
#include <filesystem>

bool is_cmd(const std::string &msg, const std::string &cmd);

const std::string word_from(const std::string &str, const size_t &start);

const std::string recv_msg(const int &fd);
void send_msg(const int &fd, const std::string &msg);

void recv_file(const int &fd, const std::string &filepath);
void send_file(const int &fd, const std::string &filepath);

constexpr size_t TMP_BUFF_SIZE = 64 * 1024; // 64 KB buffer size