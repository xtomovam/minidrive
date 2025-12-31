#include "session.hpp"

void Session::uploadFile(const std::string &local_path, const std::string &remote_path, const size_t &filesize) {
    // processs paths
    if (local_path.empty()) {
        throw std::runtime_error("no_path: UPLOAD command requires a path argument");
    }
    this->current_transfer.local_path = local_path;
    this->current_transfer.remote_path = this->getWorkingDirectory() + "/";
    if (remote_path.empty()) {
        this->current_transfer.remote_path += local_path.substr(local_path.find_last_of("/\\") + 1);
    } else {
        this->current_transfer.remote_path += remote_path;
    }
    this->current_transfer.remote_path += ".part";
    this->verifyPath(this->current_transfer.remote_path, VerifyType::None, VerifyExistence::MustNotExist);

    // log transfer
    this->current_transfer.bytes_completed = 0;
    this->current_transfer.total_bytes = filesize;
    this->current_transfer.timestamp = std::to_string(std::time(nullptr));
    TransferState::addTransfer(this->getClientDirectory(), this->current_transfer);

    // prepare to receive file
    this->setState(State::AwaitingFile);
    this->send("READY");
}

void Session::uploadFileChunk() {
    size_t bytes_left = this->current_transfer.total_bytes - this->current_transfer.bytes_completed;
    size_t to_recv = bytes_left < TMP_BUFF_SIZE ? bytes_left : TMP_BUFF_SIZE;
    size_t bytes_sent=  recv_file_chunk(this->client_fd, this->current_transfer.remote_path, this->current_transfer.bytes_completed, to_recv);

    bytes_left -= bytes_sent;
    this->current_transfer.bytes_completed += bytes_sent;
    TransferState::updateProgress(this->getClientDirectory(), this->current_transfer.remote_path, this->current_transfer.bytes_completed);
    
    if (bytes_left == 0) { // file received -> finish upload
        std::filesystem::rename(this->current_transfer.remote_path, this->current_transfer.remote_path.substr(0, this->current_transfer.remote_path.size() - 5));
        this->current_transfer.remote_path = this->current_transfer.remote_path.substr(0, this->current_transfer.remote_path.size() - 5);
        this->send("OK\nUploaded file to " + this->current_transfer.remote_path);
        TransferState::removeTransfer(this->getClientDirectory(), this->current_transfer.remote_path);
        this->state = State::AwaitingMessage;
    }
}