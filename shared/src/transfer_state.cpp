#include "minidrive/transfer_state.hpp"
#include <iostream>
void TransferState::addTransfer(const std::string& user_dir, const Transfer& transfer) {
    // add transfer to .transfers_state file in user_dir
    std::ofstream outfile(user_dir + "/.transfers_state", std::ios::binary | std::ios::app);
    if (!outfile) {
        throw std::runtime_error("file_open_failed: Failed to open transfers state file for writing");
    }
    outfile << transfer.local_path << ":" << transfer.remote_path << ":" << transfer.bytes_completed << ":" << transfer.total_bytes << ":" << transfer.timestamp << "\n";
    
    // create timer for removing transfer after TRANSFER_TIMEOUT_MINUTES minutes
    std::thread([user_dir, transfer]() {
        std::this_thread::sleep_for(std::chrono::minutes(TRANSFER_TIMEOUT_MINUTES));
        TransferState::removeTransfer(user_dir, transfer.local_path);
    }).detach();
}

void TransferState::updateProgress(const std::string& user_dir, const std::string& remote_path, size_t bytes) {
    // open .transfers_state file
    const std::string path = user_dir + "/.transfers_state";
    std::ifstream infile(path, std::ios::binary);
    if (!infile) {
        throw std::runtime_error("file_open_failed: Failed to open transfers state file for reading");
    }

    // read all lines and update the relevant one
    std::vector<std::string> lines;
    std::string line;
    bool updated = false;
    while (std::getline(infile, line)) {
        size_t pos1 = line.find(':');
        size_t pos2 = (pos1 == std::string::npos) ? std::string::npos : line.find(':', pos1 + 1);
        if (pos1 == std::string::npos || pos2 == std::string::npos) {
            continue; // ignore malformed line (and remove it)
        }
        std::string path = line.substr(pos1 + 1, pos2 - pos1 - 1);
        if (path == remote_path) {
            size_t pos3 = line.find(':', pos2 + 1);
            if (pos3 != std::string::npos) {
                line.replace(pos2 + 1, pos3 - pos2 - 1, std::to_string(bytes));
                updated = true;
            }
        }
        lines.push_back(line);
    }
    infile.close();

    // rewrite .transfers_state file
    if (!updated) {
        // nothing to change
        return;
    }
    std::ofstream outfile(path, std::ios::binary | std::ios::trunc);
    if (!outfile) {
        throw std::runtime_error("file_open_failed: Failed to open transfers state file for writing");
    }
    for (const auto& l : lines) {
        outfile << l << "\n";
    }
}

void TransferState::removeTransfer(const std::string& user_dir, const std::string& local_path) {
    // open .transfers_state file
    const std::string path = user_dir + "/.transfers_state";
    std::ifstream infile(path, std::ios::binary);
    if (!infile) {
        throw std::runtime_error("file_open_failed: Failed to open transfers state file for reading");
    }

    // read all lines except the one to remove
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(infile, line)) {
        size_t pos1 = line.find(':');
        size_t pos2 = (pos1 == std::string::npos) ? std::string::npos : line.find(':', pos1 + 1);
        if (pos1 == std::string::npos || pos2 == std::string::npos) {
            lines.push_back(line);
            continue;
        }
        std::string file = line.substr(pos1 + 1, pos2 - pos1 - 1);
        if (file != local_path) {
            lines.push_back(line);
        }
    }
    infile.close();

    // rewrite .transfers_state file
    std::ofstream outfile(path, std::ios::binary | std::ios::trunc);
    if (!outfile) {
        throw std::runtime_error("file_open_failed: Failed to open transfers state file for writing");
    }
    for (const auto& l : lines) {
        outfile << l << "\n";
    }
}

std::vector<TransferState::Transfer> TransferState::getActiveTransfers(const std::string& user_dir) {
    std::vector<Transfer> transfers;

    // open .transfers_state file
    const std::string path = user_dir + "/.transfers_state";
    std::ifstream infile(path, std::ios::binary);
    if (!infile) {
        // no transfers state file -> no active transfers
        return transfers;
    }

    // read all lines and parse transfers
    std::string line;
    while (std::getline(infile, line)) {
        size_t pos1 = line.find(':');
        size_t pos2 = (pos1 == std::string::npos) ? std::string::npos : line.find(':', pos1 + 1);
        size_t pos3 = (pos2 == std::string::npos) ? std::string::npos : line.find(':', pos2 + 1);
        size_t pos4 = (pos3 == std::string::npos) ? std::string::npos : line.find(':', pos3 + 1);
        if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos || pos4 == std::string::npos) {
            continue; // malformed line
        }

        Transfer transfer;
        transfer.local_path = line.substr(0, pos1);
        transfer.remote_path = line.substr(pos1 + 1, pos2 - pos1 - 1);
        transfer.bytes_completed = static_cast<size_t>(std::stoull(line.substr(pos2 + 1, pos3 - pos2 - 1)));
        std::cout << "Parsed bytes_completed: " << transfer.bytes_completed << std::endl;
        transfer.total_bytes = static_cast<size_t>(std::stoull(line.substr(pos3 + 1, pos4 - pos3 - 1)));
        std::cout << "Parsed total_bytes: " << transfer.total_bytes << std::endl;
        transfer.timestamp = line.substr(pos4 + 1);

        transfers.push_back(transfer);
    }

    return transfers;
}

void TransferState::clearTransfers(const std::string& user_dir) {
    const std::string path = user_dir + "/.transfers_state";

    // read existing lines
    std::ifstream infile(path, std::ios::binary);
    if (!infile) {
        return; // no file -> nothing to clear
    }

    std::string line;
    std::vector<std::string> keep;
    const std::time_t now = std::time(nullptr);
    const std::time_t timeout_sec = static_cast<std::time_t>(TRANSFER_TIMEOUT_MINUTES) * 60;

    while (std::getline(infile, line)) {
        size_t pos1 = line.find(':');
        size_t pos2 = (pos1 == std::string::npos) ? std::string::npos : line.find(':', pos1 + 1);
        size_t pos3 = (pos2 == std::string::npos) ? std::string::npos : line.find(':', pos2 + 1);
        size_t pos4 = (pos3 == std::string::npos) ? std::string::npos : line.find(':', pos3 + 1);
        if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos || pos4 == std::string::npos) {
            continue;
        }

        std::string timestamp_str = line.substr(pos4 + 1);
        try {
            unsigned long long ts = std::stoull(timestamp_str);
            // keep if not older than timeout
            if (static_cast<std::time_t>(ts) + timeout_sec > now) {
                keep.push_back(line);
            }
        } catch (const std::exception& e) {
            // on parse error keep the line
            keep.push_back(line);
        }
    }
    infile.close();

    // rewrite .transfers_state file with kept lines
    std::ofstream outfile(path, std::ios::binary | std::ios::trunc);
    if (!outfile) {
        throw std::runtime_error("file_open_failed: Failed to open transfers state file for writing");
    }
    for (const auto& l : keep) {
        outfile << l << "\n";
    }
}