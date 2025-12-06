#include "flows/upload_flow.hpp"
UploadFlow::UploadFlow(Session* s, const std::string local_path, const std::string remote_path, const size_t &filesize) : Flow(s), remote_path(remote_path) {
    this->filesize = filesize;
    this->bytes_remaining = filesize;

    // validate paths
    if (local_path.empty()) {
        throw std::runtime_error("no_path: UPLOAD command requires a path argument");
    }
    this->local_path = local_path;
    if (remote_path.empty()) {
        this->remote_path = local_path.substr(local_path.find_last_of("/\\") + 1);
    } else {
        this->remote_path = remote_path;
    }
    this->remote_path = this->session->getWorkingDirectory() + "/" + this->remote_path;
    this->session->verifyPath(this->remote_path, Session::VerifyType::None, Session::VerifyExistence::MustNotExist);

    // add transfer state
    TransferState::Transfer transfer;
    transfer.local_path = local_path;
    transfer.remote_path = this->remote_path;
    transfer.bytes_completed = 0;
    transfer.total_bytes = filesize;
    transfer.timestamp = std::to_string(std::time(nullptr));
    TransferState::addTransfer(this->session->getClientDirectory(), transfer);

    // prepare to receive file
    this->session->setState(Session::State::AwaitingFile);
    this->session->send("READY");
}

void UploadFlow::onMessage(const std::string& msg) {
    (void)msg;

    // receive file chunk
    size_t to_receive = this->bytes_remaining < TMP_BUFF_SIZE ? this->bytes_remaining : TMP_BUFF_SIZE;
    size_t offset = this->filesize - this->bytes_remaining;
    this->bytes_remaining -= recv_file_chunk(this->session->getClientFD(), this->remote_path, offset, to_receive);

    // update transfer state
    size_t file_size = this->filesize - this->bytes_remaining;
    TransferState::updateProgress(this->session->getClientDirectory(), this->remote_path, file_size);

    if (this->bytes_remaining == 0) { // file received -> leave flow
        TransferState::removeTransfer(this->session->getClientDirectory(), this->remote_path); // remove transfer state
        this->session->send("File uploaded successfully to " + this->remote_path);
        this->session->leaveFlow();
    }
}