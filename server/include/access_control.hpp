#pragma once

#include <string>

bool exists_user(const std::string &user);
void register_user(const std::string &user, const std::string &password);
bool verify(const std::string &user, const std::string &password);