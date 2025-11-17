#include "session.hpp"
#include "flows/flow.hpp"
#include "flows/authenticate_flow.hpp"
#include "flows/upload_flow.hpp"
#include <iostream>

// constructor
Session::Session(const int &fd, const std::string &root) : client_fd(fd), root(root), working_directory(root + "/public"), client_directory(root + "/public") {}

// destructor
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
        } else if (is_cmd(msg, "RMDIR")) {
            this->removeDirectory(word_from(msg, 6));
        } else if (is_cmd(msg, "MOVE")) {
            this->move(word_from(msg, 5), msg.find(' ', 5) != std::string::npos ? word_from(msg, msg.find(' ', 5) + 1) : "");
        } else if (is_cmd(msg, "COPY")) {
            this->copy(word_from(msg, 5), msg.find(' ', 5) != std::string::npos ? word_from(msg, msg.find(' ', 5) + 1) : "");

        // commands requiring flows
        } else if (is_cmd(msg, "AUTH")) {
            if (word_from(msg, 5).empty()) {
                this->send("[warning] operating in public mode - files are visible to everyone");
            } else {
                this->current_flow = std::make_unique<AuthenticateFlow>(this, word_from(msg, 5));
            }
        } else if (is_cmd(msg, "UPLOAD")) {
            this->current_flow = std::make_unique<UploadFlow>(this, word_from(msg, 7), (msg.find(' ', 7) != std::string::npos ? word_from(msg, msg.find(' ', 7) + 1) : ""));
        } else if (is_cmd(msg, "DOWNLOAD")) {
            this->downloadFile(word_from(msg, 9));
        } else {
            throw std::runtime_error("unknown_command: Unknown command: " + msg);
        }
    } catch (const std::exception &e) {
        this->send(std::string("ERROR ") + e.what());
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

const int &Session::getClientFD() const {
    return this->client_fd;
}

const std::string &Session::getRoot() const {
    return this->root;
}

const std::string &Session::getWorkingDirectory() const {
    return this->working_directory;
}

const std::string &Session::getClientDirectory() const {
    return this->client_directory;
}

const std::string &Session::getClientUsername() const {
    return this->client_username;
}


Session::State Session::getState() const {
    return this->state;
}

void Session::setClientDirectory(const std::string &path) {
    this->client_directory = path;
}

void Session::setWorkingDirectory(const std::string &path) {
    // ensure path is within client directory
    this->working_directory = this->verifyPath(path, VerifyType::Directory, VerifyExistence::MustExist);
}

void Session::setClientUsername(const std::string &username) {
    this->client_username = username;
}

// security check

std::string Session::verifyPath(const std::string &path, const VerifyType &type, const VerifyExistence &existence) const {
    namespace fs = std::filesystem;

    // ensure path is within client directory
    fs::path abs_client_dir = fs::weakly_canonical(this->client_directory);
    fs::path abs_path = fs::weakly_canonical(path);
    if (!(std::mismatch(abs_client_dir.begin(), abs_client_dir.end(),abs_path.begin(), abs_path.end()).first == abs_client_dir.end())) {
        throw std::runtime_error("access_denied: Cannot change directory outside of client directory (full path: " + path + ")");
    }
    
    // verify type
    if (type == VerifyType::Directory && !fs::is_directory(path)) {
        throw std::runtime_error("not_directory: Path is not a directory: " + path);
    }
    if (type == VerifyType::File && fs::is_directory(path)) {
        throw std::runtime_error("is_directory: Path is a directory: " + path);
    }

    // verify existence
    if (!fs::exists(path) && existence == VerifyExistence::MustExist) {
        if (type == VerifyType::Directory) {
            throw std::runtime_error("directory_not_found: Directory does not exist: " + path);
        } else if (type == VerifyType::File) {
            throw std::runtime_error("file_not_found: File does not exist: " + path);
        } else {
            throw std::runtime_error("path_not_found: Path does not exist: " + path);
        }
    } else if (fs::exists(path) && existence == VerifyExistence::MustNotExist) {
        if (type == VerifyType::Directory) {
            throw std::runtime_error("overwrite_error: Directory already exists: " + path);
        } else if (type == VerifyType::File) {
            throw std::runtime_error("overwrite_error: File already exists: " + path);
        } else {
            throw std::runtime_error("overwrite_error: Path already exists: " + path);
        }
    }

    return abs_path.string();
}

// command implementations

void Session::list(const std::string path) {
    namespace fs = std::filesystem;

    std::string full_path = this->working_directory + (path.empty() ? "" : "/" + path);

    verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustExist);
    
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

    this->send("OK " + out.str());
}

void Session::downloadFile(const std::string &path) {
    this->verifyPath(this->client_directory + "/" + path, VerifyType::File, VerifyExistence::MustExist);
    send_file(this->client_fd, this->client_directory + "/" + path);
}

void Session::deleteFile(const std::string &path) {
    namespace fs = std::filesystem;

    if (path.empty()) {
        throw std::runtime_error("no_path: DELETE command requires a path argument");
    }

    std::string full_path = this->client_directory + "/" + path;
    this->verifyPath(full_path, VerifyType::File, VerifyExistence::MustExist);

    fs::remove(full_path);

    this->send("OK Deleted file " + path);
}

void Session::changeDirectory(const std::string &path) {
    namespace fs = std::filesystem;

    if (path.empty()) {
        throw std::runtime_error("no_path: CD command requires a path argument");
    }

    std::string full_path = this->working_directory + "/" + path;
    this->verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustExist);

    this->setWorkingDirectory(full_path);

    this->send("OK Changed directory to " + path);
}

void Session::makeDirectory(const std::string &path) {
    namespace fs = std::filesystem;

    if (path.empty()) {
        throw std::runtime_error("no_path: MKDIR command requires a path argument");
    }

    std::string full_path = this->working_directory + "/" + path;
    try {
        this->verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustNotExist);
    } catch (const std::runtime_error &e) {
        if (std::string(e.what()).find("access_denied") != std::string::npos) {
            throw;
        }
    }

    fs::create_directories(full_path);

    this->send("OK Created directory " + path);
}

void Session::removeDirectory(const std::string &path) {
    namespace fs = std::filesystem;

    if (path.empty()) {
        throw std::runtime_error("no_path: RMDIR command requires a path argument");
    }

    std::string full_path = this->working_directory + "/" + path;
    this->verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustExist);

    fs::remove_all(full_path);

    this->send("OK Removed directory " + path);
}

void Session::move(const std::string &source, const std::string &destination) {
    namespace fs = std::filesystem;

    if (source.empty() || destination.empty()) {
        throw std::runtime_error("no_path: MOVE command requires source and destination path arguments");
    }

    std::string full_source_path = this->working_directory + "/" + source;
    std::string full_destination_path = this->working_directory + "/" + destination;
    this->verifyPath(full_source_path, VerifyType::None, VerifyExistence::MustExist);
    this->verifyPath(full_destination_path.substr(0, full_destination_path.find_last_of("/\\")), VerifyType::Directory, VerifyExistence::MustExist);
    this->verifyPath(full_destination_path, VerifyType::None, VerifyExistence::MustNotExist);

    fs::rename(full_source_path, full_destination_path);

    this->send("OK Moved " + source + " to " + destination);
}

void Session::copy(const std::string &source, const std::string &destination) {
    namespace fs = std::filesystem;

    if (source.empty() || destination.empty()) {
        throw std::runtime_error("no_path: COPY command requires source and destination path arguments");
    }

    std::string full_source_path = this->working_directory + "/" + source;
    std::string full_destination_path = this->working_directory + "/" + destination;
    this->verifyPath(full_source_path, VerifyType::None, VerifyExistence::MustExist);
    this->verifyPath(full_destination_path.substr(0, full_destination_path.find_last_of("/\\")), VerifyType::None, VerifyExistence::DontCare);
    this->verifyPath(full_destination_path, VerifyType::None, VerifyExistence::MustNotExist);

    fs::copy(full_source_path, full_destination_path, fs::copy_options::recursive);

    this->send("OK Copied " + source + " to " + destination);
}