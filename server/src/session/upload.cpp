#include "session.hpp"

void Session::uploadFile() {
    size_t bytes_left = this->current_transfer.total_bytes - this->current_transfer.bytes_completed;
    size_t to_recv = bytes_left < TMP_BUFF_SIZE ? bytes_left : TMP_BUFF_SIZE;
    size_t bytes_sent=  recv_file_chunk(this->client_fd, this->current_transfer.remote_path, this->current_transfer.bytes_completed, to_recv);

    bytes_left -= bytes_sent;
    this->current_transfer.bytes_completed += bytes_sent;
    TransferState::updateProgress(this->getClientDirectory(), this->current_transfer.remote_path, this->current_transfer.bytes_completed);
    
    if (bytes_left == 0) { // file received -> finish upload
        this->send("File uploaded successfully to " + this->current_transfer.remote_path);
        TransferState::removeTransfer(this->getClientDirectory(), this->current_transfer.remote_path);
        this->state = State::AwaitingMessage;
        this->leaveFlow();
        std::cout << "Upload of file " << this->current_transfer.remote_path << " completed.\n";
    }
}