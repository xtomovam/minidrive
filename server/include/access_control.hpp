#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sodium.h>

bool exists_user(const std::string &user, const std::string &root);
void register_user(const std::string &user, const std::string &password, const std::string &root);
bool authenticate_user(const std::string &user, const std::string &password, const std::string &root);