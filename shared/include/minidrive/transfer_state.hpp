#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <thread>

constexpr time_t TRANSFER_TIMEOUT_MINUTES = 60;

// upload transfer state management
class TransferState {
public:
    enum Type { UPLOAD, DOWNLOAD };
    
    struct Transfer {
        std::string local_path;
        std::string remote_path;
        size_t bytes_completed;
        size_t total_bytes;
        std::string timestamp;
    };
    
    static void addTransfer(const std::string& user_dir, const Transfer& transfer);
    static void updateProgress(const std::string& user_dir, const std::string& remote_path, size_t bytes);
    static void removeTransfer(const std::string& user_dir, const std::string& filename);
    static std::vector<Transfer> getActiveTransfers(const std::string& user_dir);
    static void clearTransfers(const std::string& user_dir);
};