#pragma once

#include "../../shared/include/minidrive/helpers.hpp"

#include <memory>
#include <functional>
#include <iostream>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <unordered_set>

class Session {
public:
    enum class State {
        AwaitingMessage,
        AwaitingRegistrationChoice,
        AwaitingRegistrationPassword,
        AwaitingPassword,
        AwaitingResumeChoice,
        AwaitingFile,
        DownloadingFile
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
    
    // downloading files
    void downloadFileChunk();
    
    // file locking for concurrent downloads
    static void lockFileForDownload(const std::string &filepath);
    static void unlockFileForDownload(const std::string &filepath);
    static bool isFileLocked(const std::string &filepath);
    
    // getters and setters
    const int &getClientFD() const;
    const std::string &getRoot() const;
    const std::string &getWorkingDirectory() const;
    const std::string &getClientDirectory() const;
    State getState() const;
    
private:
    const int client_fd;
    const std::string root;
    std::function<void(int)> close_callback;
    std::string working_directory = "public";
    std::string client_directory = "public";
    
    // download state
    std::ifstream download_stream;
    std::string download_path;
    size_t download_bytes_sent = 0;
    size_t download_total_bytes = 0;
    
    // global file lock tracking across all sessions
    static std::shared_mutex files_mutex;
    static std::unordered_set<std::string> locked_files;
    
    // session helpers
    std::string verifyPath(const std::string &path, const VerifyType &type, const VerifyExistence &existence) const;
    void send(const std::string &msg) const;
    void setState(const State &new_state);
    
    std::string client_username = "";
    State state = State::AwaitingMessage;
    bool auth_initiated = false;
    bool resume_initiated = false;
    TransferState::Transfer current_transfer;

    // helpers
    std::string path(const std::string &relative_path) const;

    // authentication
    void auth(const std::string &username);
    void processRegisterChoice(std::string choice);
    void registerUser(std::string password);
    void authenticateUser(std::string password);

    // resuming uploads/downloads
    void resumeUpload();
    void processResumeChoice(const std::string &choice);
    void resumeDownload(const std::string &path, const size_t &offset);

    // uploading files
    void uploadFile(const std::string &local_path, const std::string &remote_path, const size_t &filesize);
    void uploadFileChunk();

    // file operations
    void list(const std::string &path);
    void downloadFile(const std::string &path);
    void deleteFile(const std::string &path);
    void changeDirectory(const std::string &path);
    void makeDirectory(const std::string &path);
    void removeDirectory(const std::string &path);
    void move(const std::string &source, const std::string &destination);
    void copy(const std::string &source, const std::string &destination);
};