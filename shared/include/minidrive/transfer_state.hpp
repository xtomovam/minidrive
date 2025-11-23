#pragma once

#include <string>
#include <vector>
#include <fstream>

class TransferState {
public:
    enum Type { UPLOAD, DOWNLOAD };
    
    struct Transfer {
        Type type;
        std::string filepath;
        size_t bytes_completed;
        size_t total_bytes;
        std::string timestamp;
        std::string local_path;  // For downloads
    };
    
    static void addTransfer(const std::string& user_dir, const Transfer& transfer);
    static void updateProgress(const std::string& user_dir, const std::string& filename, size_t bytes);
    static void removeTransfer(const std::string& user_dir, const std::string& filename);
    static std::vector<Transfer> getActiveTransfers(const std::string& user_dir);
};