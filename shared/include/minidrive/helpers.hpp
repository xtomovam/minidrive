#include <string>
#include <sys/socket.h>
#include <stdexcept>
#include <fstream>

std::string recv_msg(const int &fd);
void send_msg(const int &fd, const std::string &msg);

void recv_file(const int &fd, const std::string &filepath, const size_t &size);
void send_file(const int &fd, const std::string &filepath);

constexpr size_t TMP_BUFF_SIZE = 256;