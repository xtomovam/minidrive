#include "session.hpp"

Session::Session(const int &fd) : client_fd(fd) {}

void Session::onMessage(const std::string &msg) {
    if (this->current_flow) {
        this->current_flow->onMessage(msg);
        return;
    }

    try {
        if (msg.starts_with("AUTH ")) {
            this->current_flow = std::make_unique<AuthenticateFlow>(this, msg.size() > 5 ? msg.substr(5) : "");
        } else
        if (msg.starts_with("LIST")) {
            this->list(msg.size() > 5 ? msg.substr(5) : "");
        } else if (msg.starts_with("CD ")) {
            this->changeDirectory(msg.size() > 3 ? msg.substr(3) : "");
        } else if (msg.starts_with("DELETE ")) {
            this->deleteFile(msg.size() > 7 ? msg.substr(7) : "");
        } else {
            send_msg(this->client_fd, "ERROR unknown_command: Unknown command: " + msg);
        }
    } catch (const std::exception &e) {
        send_msg(this->client_fd, std::string("ERROR ") + e.what());
    }
}

void Session::send(const std::string &msg) const {
    send_msg(this->client_fd, msg);
}

void Session::enterFlow(Flow* flow) {
    this->current_flow.reset(flow);
}

void Session::leaveFlow() {
    this->current_flow.reset();
}

const std::string &Session::getClientUsername() const {
    return this->client_username;
}

void Session::setClientDirectory(const std::string &path) {
    this->client_directory = path;
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