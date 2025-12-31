#include "session.hpp"

void Session::downloadFile(const std::string &path) {
    if (path.empty()) {
        throw std::runtime_error("no_path: DOWNLOAD command requires a path argument");
    }
    std::string full_path = this->path(path);
    this->verifyPath(full_path, VerifyType::File, VerifyExistence::MustExist);

    // lock the file for download
    lockFileForDownload(full_path);

    // prepare download state
    this->download_path = full_path;
    this->download_total_bytes = static_cast<size_t>(std::filesystem::file_size(full_path));
    this->download_bytes_sent = 0;
    this->download_stream.close();
    this->download_stream.clear();
    this->download_stream.open(full_path, std::ios::binary);
    if (!this->download_stream) {
        unlockFileForDownload(full_path);
        throw std::runtime_error("file_open_failed: Failed to open file for reading (path: " + full_path + ")");
    }

    // announce file info then switch to DownloadingFile state
    this->send("FILEINFO " + full_path + " " + std::to_string(this->download_total_bytes));
    this->state = State::DownloadingFile;
}

void Session::downloadFileChunk() {
    if (this->state != State::DownloadingFile) {
        return;
    }

    // finished
    if (this->download_bytes_sent >= this->download_total_bytes) {
        this->state = State::AwaitingMessage;
        unlockFileForDownload(this->download_path);
        if (this->download_stream.is_open()) {
            this->download_stream.close();
        }
        return;
    }

    // read and send next chunk via helper
    size_t remaining = this->download_total_bytes - this->download_bytes_sent;
    size_t to_read = std::min(TMP_BUFF_SIZE, remaining);
    size_t sent = send_file_chunk(this->client_fd, this->download_stream, to_read);

    this->download_bytes_sent += sent;

    // if finished, clean up
    if (this->download_bytes_sent >= this->download_total_bytes) {
        this->state = State::AwaitingMessage;
        unlockFileForDownload(this->download_path);
        if (this->download_stream.is_open()) {
            this->download_stream.close();
        }
    }
}