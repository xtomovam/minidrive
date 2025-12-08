#pragma once

#include "../../shared/include/minidrive/helpers.hpp"

#include <memory>
#include <functional>
#include <iostream>

class Flow;

class Session {
public:
    enum class State {
        AwaitingMessage,
        AwaitingRegistrationChoice,
        AwaitingRegistrationPassword,
        AwaitingPassword,
        AwaitingResumeChoice,
        AwaitingFile
    };
    enum class VerifyType {
        File,
        Directory,
        None
    };
    enum class VerifyExistence {
        MustExist,
        MustNotExist,
        DontCare
    };

    Session(const int &fd, const std::string &root, std::function<void(int)> close_callback);
    ~Session(); // Custom destructor to handle unique_ptr<Flow>

    void onMessage(const std::string &msg);
    void exit();
    
    // API for flows
    std::string verifyPath(const std::string &path, const VerifyType &type, const VerifyExistence &existence) const;
    void send(const std::string &msg) const;
    void receive_file(const std::string &filepath) const;
    void setState(const State &new_state);
    void leaveFlow();
    void setCurrentTransfer(const TransferState::Transfer &transfer);
    
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
    std::function<void(int)> close_callback;
    std::string working_directory = "public";
    std::string client_directory = "public";
    std::string client_username = "";
    std::unique_ptr <Flow> current_flow;
    State state = State::AwaitingMessage;
    bool auth_initiated = false;
    TransferState::Transfer current_transfer;

    // authentication
    void auth(const std::string &username);
    void processRegisterChoice(std::string choice);
    void registerUser(std::string password);
    void authenticateUser(std::string password);

    // resuming uploads/downloads
    void resume();
    void processResumeChoice(std::string choice);

    void list(const std::string path);
    void uploadFile();
    void downloadFile(const std::string &path);
    void deleteFile(const std::string &path);

    void changeDirectory(const std::string &path);
    void makeDirectory(const std::string &path);
    void removeDirectory(const std::string &path);
    void move(const std::string &source, const std::string &destination);
    void copy(const std::string &source, const std::string &destination);
};