#include "flows/upload_resume_flow.hpp"

UploadResumeFlow::UploadResumeFlow(Session* s, const std::string& full_remote_path, size_t offset) : Flow(s), full_remote_path(full_remote_path), offset(offset) {
    this->filesize = std::filesystem::file_size(full_remote_path);
    this->bytes_remaining = this->filesize;
}

void UploadResumeFlow::onMessage(const std::string& msg) {
    std::cout << "UploadResumeFlow received message: " << msg << std::endl;
    if (!this->resume) {
        if (msg == "y") {
            std::cout << "Resuming upload of file: " << this->full_remote_path << " from offset " << this->offset << std::endl;
            this->resume = true;
            this->session->setState(Session::State::AwaitingFile);
            return;
        } else {
            std::cout << "Not resuming upload. Exiting flow.\n";
            this->session->leaveFlow();
        }
    }

    else {
        std::cout << "Continuing file upload...\n";
        // receive file chunk
        std::cout << "Receiving file chunk, bytes remaining: " << this->bytes_remaining << std::endl;
        size_t to_receive = this->bytes_remaining < TMP_BUFF_SIZE ? this->bytes_remaining : TMP_BUFF_SIZE;
        size_t offset = this->filesize - this->bytes_remaining;
        this->bytes_remaining -= recv_file_chunk(this->session->getClientFD(), this->full_remote_path, offset, to_receive);

        // update transfer state
        size_t file_size = this->filesize - this->bytes_remaining;
        TransferState::updateProgress(this->session->getClientDirectory(), this->full_remote_path, file_size);

        if (this->bytes_remaining == 0) { // file received -> leave flow
            TransferState::removeTransfer(this->session->getClientDirectory(), this->full_remote_path); // remove transfer state
            this->session->send("File uploaded successfully to " + this->full_remote_path);
            this->session->leaveFlow();
        }
    }
}