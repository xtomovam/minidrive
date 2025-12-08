#include "session.hpp"

void Session::resumeUpload() {
    // check for active transfers to resume
    TransferState::clearTransfers(this->getClientDirectory());
    std::vector<TransferState::Transfer> transfers = TransferState::getActiveTransfers(this->getClientDirectory());

    if (!transfers.empty()) {
        this->send("RESUME " + transfers[0].local_path +  " " + transfers[0].remote_path + " " + std::to_string(transfers[0].bytes_completed));
        this->current_transfer = transfers[0];
        this->state = State::AwaitingResumeChoice;
    } else {
        this->send("RESUME");
    }
}

void Session::processResumeChoice(const std::string &choice) {
    if (choice == "y") {
        this->state = State::AwaitingFile;
    } else {
        this->state = State::AwaitingMessage;
    }
}

void Session::resumeDownload(const std::string &path, const size_t &offset) {
    // resume download from offset
    send_file(this->client_fd, path, offset);
}
    