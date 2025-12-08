#include "flows/upload_resume_flow.hpp"

UploadResumeFlow::UploadResumeFlow(Session* s, const std::string& full_remote_path, const size_t &filesize, const size_t &offset) : Flow(s), full_remote_path(full_remote_path), filesize(filesize), offset(offset) {
    this->bytes_remaining = this->filesize - this->offset;
}

void UploadResumeFlow::onMessage(const std::string& msg) {
    // process resume choice
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

    // handle file upload
    else {
        // receive file chunk
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