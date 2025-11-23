#include "flows/upload_flow.hpp"

UploadFlow::UploadFlow(Session* s, const std::string local_path, const std::string remote_path, size_t filesize) : Flow(s), remote_path(remote_path) {
    // already an active transfer -> resume
    std::vector<TransferState::Transfer> active_transfers = TransferState::getActiveTransfers(this->session->getClientDirectory());
    for (const auto& transfer : active_transfers) {
        if (transfer.filepath == (remote_path.empty() ? local_path.substr(local_path.find_last_of("/\\") + 1) : remote_path)) {
            this->session->setState(Session::State::AwaitingFile);
            this->session->send("READY");
            return;
        }
    }

    // validate paths
    if (local_path.empty()) {
        throw std::runtime_error("no_path: UPLOAD command requires a path argument");
    }
    if (remote_path.empty()) {
        this->remote_path = local_path.substr(local_path.find_last_of("/\\") + 1);
    }
    this->full_remote_path = this->session->getWorkingDirectory() + "/" + this->remote_path;
    this->session->verifyPath(this->full_remote_path, Session::VerifyType::None, Session::VerifyExistence::MustNotExist);

    // add transfer state
    TransferState::Transfer transfer;
    transfer.type = TransferState::UPLOAD;
    transfer.filepath = this->remote_path;
    transfer.bytes_completed = 0;
    transfer.total_bytes = filesize;
    transfer.timestamp = std::to_string(std::time(nullptr));
    transfer.local_path = local_path;
    TransferState::addTransfer(this->session->getClientDirectory(), transfer);

    // prepare to receive file
    this->session->setState(Session::State::AwaitingFile);
    this->session->send("READY");
}

void UploadFlow::onMessage(const std::string& msg) {
    (void)msg;

    // get already received bytes
    std::vector<TransferState::Transfer> active_transfers = TransferState::getActiveTransfers(this->session->getClientDirectory());
    size_t offset = 0;
    for (const auto& transfer : active_transfers) {
        if (transfer.filepath == this->remote_path) {
            offset = transfer.bytes_completed;
            break;;
        }
    }
       
    recv_file(this->session->getClientFD(), this->full_remote_path, this->session->getClientDirectory(), offset);

    this->session->send("File uploaded successfully to " + this->remote_path);
    this->session->leaveFlow();
}