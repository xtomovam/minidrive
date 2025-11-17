#pragma once

#include "flow.hpp"
#include "../../shared/include/minidrive/helpers.hpp"

#include <memory>

class Session {
public:
    Session(const int &fd);

    void onMessage(const std::string &msg);

    // API for flows
    void send(const std::string &msg) const;
    void enterFlow(Flow* flow);
    void leaveFlow();

    // getters and setters
    const std::string &getClientUsername() const;
    void setClientDirectory(const std::string &path);

private:
    int client_fd;
    std::string client_username = "";
    std::string client_directory = "server/root/public";
    std::unique_ptr <Flow> current_flow;
    std::string working_directory = "server/root/public";

    void list(const std::string path);
    void changeDirectory(const std::string &path);
    void deleteFile(const std::string &path);
};