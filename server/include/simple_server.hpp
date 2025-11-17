#pragma once

#include <cstdint>
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
#include <unordered_map>
#include <vector>

void start_simple_server(const std::uint16_t &port, const std::string &root);
