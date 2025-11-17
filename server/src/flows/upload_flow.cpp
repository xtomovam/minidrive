#include "flows/upload_flow.hpp"

UploadFlow::UploadFlow(Session* s, const std::string local_path, const std::string remote_path) : Flow(s), remote_path(remote_path) {
    if (local_path.empty()) {
        throw std::runtime_error("no_path: UPLOAD command requires a path argument");
    }
    if (remote_path.find("..") != std::string::npos) {
        throw std::runtime_error("invalid_path: Remote path cannot contain '..'");
    }
    if (remote_path.empty()) {
        this->remote_path = local_path.substr(local_path.find_last_of("/\\") + 1);
    }
    this->full_remote_path = this->session->getWorkingDirectory() + "/" + this->remote_path;
    this->session->setState(Session::State::AwaitingFile);
}

void UploadFlow::onMessage(const std::string& msg) {
    (void)msg;
    
    try {
        recv_file(this->session->getClientFD(), full_remote_path);
    } catch (const std::exception &e) {
        if (std::string(e.what()).starts_with("overwrite_error") || std::string(e.what()).starts_with("file_open_failed") || std::string(e.what()).starts_with("file_write_failed")) {
            this->session->send(std::string("ERROR ") + e.what());
            this->session->leaveFlow();
            return;
        } else {
            throw;
        }
    }
    this->session->send("OK File uploaded successfully to " + this->remote_path);
    this->session->leaveFlow();
}