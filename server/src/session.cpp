#include "session.hpp"
#include "flows/flow.hpp"
#include "flows/authenticate_flow.hpp"
#include "flows/upload_flow.hpp"

Session::Session(const int &fd) : client_fd(fd) {}

Session::~Session() = default; // Implementation here where Flow is complete

void Session::onMessage(const std::string &msg) {
    if (this->state == State::AwaitingFile) {
        if (this->current_flow) {
            this->current_flow->onMessage(msg);
            return;
        } else {
            throw std::runtime_error("no_flow: No active flow to receive file");
        }
    }

    if (this->current_flow) {
        this->current_flow->onMessage(msg);
        return;
    }

    try {
        // simple commands
        if (msg.starts_with("LIST")) {
            this->list(msg.size() > 5 ? msg.substr(5) : "");
        } else if (msg.starts_with("CD ")) {
            this->changeDirectory(msg.size() > 3 ? msg.substr(3) : "");
        } else if (msg.starts_with("DELETE ")) {
            this->deleteFile(msg.size() > 7 ? msg.substr(7) : "");

        // commands requiring flows
        } else if (msg.starts_with("AUTH ")) {
            this->current_flow = std::make_unique<AuthenticateFlow>(this, msg.size() > 5 ? msg.substr(5) : "");
        } else if (msg.starts_with("UPLOAD ")) {
            this->current_flow = std::make_unique<UploadFlow>(
                this, 
                msg.size() > 7 ? msg.substr(7, msg.find(' ', 7) - 7) : "", 
                msg.find(' ', 7) != std::string::npos ? msg.substr(msg.find(' ', 7) + 1) : "");
        } else if (msg.starts_with("DOWNLOAD ")) {
            // create and enter download flow
            // this->current_flow = std::make_unique<DownloadFlow>(this, msg);
            throw std::runtime_error("not_implemented: DOWNLOAD flow not implemented yet");
        
        } else {
            throw std::runtime_error("unknown_command: Unknown command: " + msg);
        }
    } catch (const std::exception &e) {
        send_msg(this->client_fd, std::string("ERROR ") + e.what());
        if (this->current_flow) {
            this->leaveFlow();
        }
    }
}

void Session::send(const std::string &msg) const {
    send_msg(this->client_fd, msg);
}

void Session::setState(const State &new_state) {
    this->state = new_state;
}

void Session::enterFlow(Flow* flow) {
    this->current_flow.reset(flow);
}

void Session::leaveFlow() {
    this->current_flow.reset();
    this->state = State::AwaitingMessage;
}

// getters and setters

Session::State Session::getState() const {
    return this->state;
}

const std::string &Session::getClientUsername() const {
    return this->client_username;
}

const int &Session::getClientFD() const {
    return this->client_fd;
}

const std::string &Session::getWorkingDirectory() const {
    return this->working_directory;
}

const std::string &Session::getClientDirectory() const {
    return this->client_directory;
}

void Session::setClientDirectory(const std::string &path) {
    this->client_directory = path;
}

void Session::setWorkingDirectory(const std::string &path) {
    this->working_directory = path;
}

void Session::setClientUsername(const std::string &username) {
    this->client_username = username;
}


void Session::list(const std::string path) {
    namespace fs = std::filesystem;

    std::string full_path = this->working_directory + "/" + path;
    std::ostringstream out;
    size_t n = 0;
    for (const auto &entry : fs::directory_iterator(full_path)) {
        if (n > 0) {
            out << "\n";
        }

        if (fs::is_directory(entry.status())) {
            out << "[DIR]  ";
        } else {
            out << "       ";
        }

        out << entry.path().filename().string();
        n++;
    }

    if (n == 0) {
        out << "\n";
    }

    send_msg(this->client_fd, "OK " + out.str());
}

void Session::changeDirectory(const std::string &path) {
    namespace fs = std::filesystem;

    std::string full_path = this->working_directory + "/" + path;

    fs::current_path(full_path);

    this->working_directory = full_path;

    send_msg(this->client_fd, "OK Changed directory to " + path);
}

void Session::deleteFile(const std::string &path) {
    namespace fs = std::filesystem;

    std::string full_path = this->client_directory + "/" + path;

    if (!fs::exists(full_path)) {
        throw std::runtime_error("file_not_found: File does not exist");
    }
    if (fs::is_directory(full_path)) {
        throw std::runtime_error("is_directory: Path is a directory");
    }
    fs::remove(full_path);

    send_msg(this->client_fd, "OK Deleted file " + path);
}