#pragma once

#include "../../shared/include/minidrive/helpers.hpp"

#include <memory>

class Flow;

class Session {
public:
    enum class State {
        AwaitingMessage,
        AwaitingFile
    };

    Session(const int &fd, const std::string &root);
    ~Session(); // Custom destructor to handle unique_ptr<Flow>

    void onMessage(const std::string &msg);

    // API for flows
    void send(const std::string &msg) const;
    void setState(const State &new_state);
    void enterFlow(Flow* flow);
    void leaveFlow();

    // getters and setters
    const int &getClientFD() const;
    const std::string &getRoot() const;
    const std::string &getWorkingDirectory() const;
    const std::string &getClientDirectory() const;
    const std::string &getClientUsername() const;
    State getState() const;
    void setClientDirectory(const std::string &path);
    void setWorkingDirectory(const std::string &path);
    void setClientUsername(const std::string &username);
    
private:
    const int client_fd;
    const std::string root;
    std::string working_directory = "";
    std::string client_directory = "";
    std::string client_username = "";
    std::unique_ptr <Flow> current_flow;
    State state = State::AwaitingMessage;

    void list(const std::string path);
    void downloadFile(const std::string &path);
    void deleteFile(const std::string &path);

    void changeDirectory(const std::string &path);
    void makeDirectory(const std::string &path);

};