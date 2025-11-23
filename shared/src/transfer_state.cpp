#include "minidrive/transfer_state.hpp"

void TransferState::addTransfer(const std::string& user_dir, const Transfer& transfer) {
    // add transfer to .transfers_state file in user_dir
    std::ofstream outfile(user_dir + "/.transfers_state", std::ios::binary);
    if (!outfile) {
        throw std::runtime_error("file_open_failed: Failed to open transfers state file for writing");
    }
    outfile << transfer.type << ":" << transfer.filepath << ":" << transfer.bytes_completed << ":" << transfer.total_bytes << ":" << transfer.timestamp << ":" << transfer.local_path << "\n";
}

void TransferState::updateProgress(const std::string& user_dir, const std::string& filename, size_t bytes) {
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
            lines.push_back(line);
            continue;
        }
        std::string file = line.substr(pos1 + 1, pos2 - pos1 - 1);
        if (file == filename) {
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

void TransferState::removeTransfer(const std::string& user_dir, const std::string& filename) {
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
        if (file != filename) {
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
        size_t pos5 = (pos4 == std::string::npos) ? std::string::npos : line.find(':', pos4 + 1);
        if (pos1 == std::string::npos || pos2 == std::string::npos || pos3 == std::string::npos || pos4 == std::string::npos || pos5 == std::string::npos) {
            continue; // malformed line
        }

        Transfer transfer;
        transfer.type = static_cast<Type>(std::stoi(line.substr(0, pos1)));
        transfer.filepath = line.substr(pos1 + 1, pos2 - pos1 - 1);
        transfer.bytes_completed = static_cast<size_t>(std::stoull(line.substr(pos2 + 1, pos3 - pos2 - 1)));
        transfer.total_bytes = static_cast<size_t>(std::stoull(line.substr(pos3 + 1, pos4 - pos3 - 1)));
        transfer.timestamp = line.substr(pos4 + 1, pos5 - pos4 - 1);
        transfer.local_path = line.substr(pos5 + 1);

        transfers.push_back(transfer);
    }

    return transfers;
}