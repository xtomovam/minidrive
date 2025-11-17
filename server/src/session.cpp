#include "session.hpp"
#include "flows/flow.hpp"
#include "flows/authenticate_flow.hpp"
#include "flows/upload_flow.hpp"
#include <iostream>

Session::Session(const int &fd) : client_fd(fd) {}

Session::~Session() = default;

// main message handler
void Session::onMessage(const std::string &msg) {

    // expecting a file -> delegate to flow
    if (this->state == State::AwaitingFile) {
        if (this->current_flow) {
            this->current_flow->onMessage(msg);
            return;
        } else {
            throw std::runtime_error("no_flow: No active flow to receive file");
        }
    }

    // in a flow -> delegate to flow
    if (this->current_flow) {
        this->current_flow->onMessage(msg);
        return;
    }

    try {
        // simple commands
        if (is_cmd(msg, "LIST")) {
            this->list(word_from(msg, 5));
        } else if (is_cmd(msg, "DOWNLOAD")) {
            this->downloadFile(word_from(msg, 9));
        } else if (is_cmd(msg, "DELETE")) {
            this->deleteFile(word_from(msg, 7));
        } else if (is_cmd(msg, "CD")) {
            this->changeDirectory(word_from(msg, 3));
        } else if (is_cmd(msg, "MKDIR")) {
            this->makeDirectory(word_from(msg, 6));

        // commands requiring flows
        } else if (is_cmd(msg, "AUTH")) {
            this->current_flow = std::make_unique<AuthenticateFlow>(this, word_from(msg, 5));
        } else if (is_cmd(msg, "UPLOAD")) {
            this->current_flow = std::make_unique<UploadFlow>(this, word_from(msg, 7), (msg.find(' ', 7) != std::string::npos ? word_from(msg, msg.find(' ', 7) + 1) : ""));
        } else if (is_cmd(msg, "DOWNLOAD")) {
            this->downloadFile(word_from(msg, 9));
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

// API for flows

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

// command implementations

void Session::list(const std::string path) {
    namespace fs = std::filesystem;

    std::string full_path = this->working_directory + (path.empty() ? "" : "/" + path);
    
    // Debug output
    std::cout << "DEBUG LIST: working_directory='" << this->working_directory << "'" << std::endl;
    std::cout << "DEBUG LIST: path='" << path << "'" << std::endl;
    std::cout << "DEBUG LIST: full_path='" << full_path << "'" << std::endl;
    std::cout << "DEBUG LIST: exists=" << (fs::exists(full_path) ? "yes" : "no") << std::endl;
    
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

void Session::downloadFile(const std::string &path) {
    send_file(this->client_fd, this->client_directory + "/" + path);
}

void Session::deleteFile(const std::string &path) {
    if (path.empty()) {
        throw std::runtime_error("no_path: DELETE command requires a path argument");
    }

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

void Session::changeDirectory(const std::string &path) {
    if (path.empty()) {
        throw std::runtime_error("no_path: CD command requires a path argument");
    }
    if (path.find("..") != std::string::npos) {
        throw std::runtime_error("invalid_path: Path cannot contain '..'");
    }
    namespace fs = std::filesystem;

    std::string full_path = this->working_directory + "/" + path;
    
    if (!fs::exists(full_path)) {
        throw std::runtime_error("directory_not_found: Directory does not exist: " + path);
    }
    if (!fs::is_directory(full_path)) {
        throw std::runtime_error("not_directory: Path is not a directory: " + path);
    }

    this->working_directory = full_path;

    send_msg(this->client_fd, "OK Changed directory to " + path);
}

void Session::makeDirectory(const std::string &path) {
    if (path.empty()) {
        throw std::runtime_error("no_path: MKDIR command requires a path argument");
    }
    if (path.find("..") != std::string::npos) {
        throw std::runtime_error("invalid_path: Path cannot contain '..'");
    }
    namespace fs = std::filesystem;

    std::string full_path = this->working_directory + "/" + path;

    fs::create_directories(full_path);

    send_msg(this->client_fd, "OK Created directory " + path);
}