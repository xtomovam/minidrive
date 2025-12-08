#include "session.hpp"
#include "access_control.hpp"
#include <iostream>

// constructor
Session::Session(const int &fd, const std::string &root, std::function<void(int)> close_callback) : client_fd(fd), root(root), close_callback(close_callback), working_directory(root + "/public"), client_directory(root + "/public") {
    // clear transfers
    TransferState::clearTransfers(this->client_directory);
}

// destructor
Session::~Session() = default;

// main message handler
void Session::onMessage(const std::string &msg) {
    std::vector<std::string> parts = split_cmd(msg);
    while (parts.size() < 5) {
        parts.push_back("");
    }
    try {

        if (this->state == State::AwaitingRegistrationChoice) {
            this->processRegisterChoice(msg);
        } else if (this->state == State::AwaitingRegistrationPassword) {
            this->registerUser(msg);
        } else if (this->state == State::AwaitingPassword) {
            this->authenticateUser(msg);
        } else if (this->state == State::AwaitingResumeChoice) {
            this->processResumeChoice(msg);
        } else if (this->state == State::AwaitingFile) {
            this->uploadFileChunk();
        }

        // user commands
        else if (is_cmd(msg, "LIST")) {
            this->list(parts[1]);
        } else if (is_cmd(msg, "DELETE")) {
            this->deleteFile(parts[1]);
        } else if (is_cmd(msg, "CD")) {
            this->changeDirectory(parts[1]);
        } else if (is_cmd(msg, "MKDIR")) {
            this->makeDirectory(parts[1]);
        } else if (is_cmd(msg, "RMDIR")) {
            this->removeDirectory(parts[1]);
        } else if (is_cmd(msg, "MOVE")) {
            this->move(parts[1], parts[2]);
        } else if (is_cmd(msg, "COPY")) {
            this->copy(parts[1], parts[2]);
        } else if (is_cmd(msg, "EXIT")) {
            this->exit();
        } else if (is_cmd(msg, "UPLOAD")) {
            this->uploadFile(parts[2], parts[3], std::stoull(parts[1]));
        } else if (is_cmd(msg, "DOWNLOAD")) {
            this->downloadFile(parts[1]);

        // control commands
        } else if (is_cmd(msg, "AUTH")) {
            this->auth(parts[1]);
        } else if (is_cmd(msg, "RESUME")) {
            this->resumeDownload(parts[1], std::stoull(parts[2]));
        } else {
            throw std::runtime_error("unknown_command: Unknown command: " + msg);
        }
    } catch (const std::exception &e) {
        this->send(std::string("ERROR ") + e.what());
        this->setState(State::AwaitingMessage);
        std::cerr << "Error processing command from client fd=" << this->client_fd << ": " << e.what() << "\n";
    }
}

void Session::exit() {
    close_callback(this->client_fd);
}

// API for flows

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

void Session::send(const std::string &msg) const {
    send_msg(this->client_fd, msg);
}

void Session::setState(const State &new_state) {
    this->state = new_state;
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

// command implementations

void Session::list(const std::string path) {
    namespace fs = std::filesystem;

    std::string full_path = this->working_directory + (path.empty() ? "" : "/" + path);

    verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustExist);
    
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

    this->send(out.str());
}

void Session::downloadFile(const std::string &path) {
    std::string full_path = this->client_directory + "/" + path;
    this->verifyPath(full_path, VerifyType::File, VerifyExistence::MustExist);
    size_t file_size = static_cast<size_t>(std::filesystem::file_size(full_path));
    std::string filesize = std::to_string(file_size);
    this->send("FILEINFO " + full_path + " " + std::to_string(file_size));
    try {
        send_file(this->client_fd, full_path);
    } catch (const std::exception &e) {
    }
}

void Session::deleteFile(const std::string &path) {
    namespace fs = std::filesystem;

    if (path.empty()) {
        throw std::runtime_error("no_path: DELETE command requires a path argument");
    }

    std::string full_path = this->client_directory + "/" + path;
    this->verifyPath(full_path, VerifyType::File, VerifyExistence::MustExist);

    fs::remove(full_path);

    this->send("Deleted file " + path);
}

void Session::changeDirectory(const std::string &path) {
    namespace fs = std::filesystem;

    if (path.empty()) {
        throw std::runtime_error("no_path: CD command requires a path argument");
    }

    std::string full_path = this->working_directory + "/" + path;
    this->verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustExist);

    this->setWorkingDirectory(full_path);

    this->send("Changed directory to " + path);
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

    this->send("Created directory " + path);
}

void Session::removeDirectory(const std::string &path) {
    namespace fs = std::filesystem;

    if (path.empty()) {
        throw std::runtime_error("no_path: RMDIR command requires a path argument");
    }

    std::string full_path = this->working_directory + "/" + path;
    this->verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustExist);

    fs::remove_all(full_path);

    this->send("Removed directory " + path);
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

    this->send("Moved " + source + " to " + destination);
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

    this->send("Copied " + source + " to " + destination);
}

void Session::setCurrentTransfer(const TransferState::Transfer &transfer) {
    this->current_transfer = transfer;
}