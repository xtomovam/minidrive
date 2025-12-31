#include "session.hpp"
#include "access_control.hpp"

// static member initialization
std::shared_mutex Session::files_mutex;
std::unordered_set<std::string> Session::locked_files;

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
        std::string err_msg = "ERROR " + std::string(e.what());
        size_t pos = err_msg.find(':');
        if (pos != std::string::npos) {
            err_msg.replace(pos, 2, ":\n");
        }
        this->send(err_msg);
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

const std::string &Session::getWorkingDirectory() const {
    return this->working_directory;
}

const std::string &Session::getClientDirectory() const {
    return this->client_directory;
}

Session::State Session::getState() const {
    return this->state;
}

// helpers

std::string Session::path(const std::string &relative_path) const {
    if (relative_path.starts_with("/")) {
        return this->root + relative_path;
    } else {
        return this->working_directory + "/" + relative_path;
    }
}

// command implementations

void Session::list(const std::string &path) {
    std::string full_path = this->path(path);
    verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustExist);
    
    std::ostringstream out;
    size_t n = 0;
    for (const auto &entry : std::filesystem::directory_iterator(full_path)) {
        if (n > 0) {
            out << "\n";
        }

        if (std::filesystem::is_directory(entry.status())) {
            out << "[DIR]  ";
        } else {
            out << "       ";
        }

        out << entry.path().filename().string();
        n++;
    }

    this->send("OK\n" + out.str());
}


void Session::deleteFile(const std::string &path) {
    if (path.empty()) {
        throw std::runtime_error("no_path: DELETE command requires a path argument");
    }
    std::string full_path = this->path(path);
    this->verifyPath(full_path, VerifyType::File, VerifyExistence::MustExist);

    // check if file is currently being downloaded
    if (isFileLocked(full_path)) {
        throw std::runtime_error("file_in_use: Cannot delete file while it is being downloaded");
    }

    std::filesystem::remove(full_path);

    this->send("OK\nDeleted file " + path);
}

void Session::changeDirectory(const std::string &path) {
    if (path.empty()) {
        throw std::runtime_error("no_path: CD command requires a path argument");
    }
    std::string full_path = this->path(path);
    this->verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustExist);

    this->working_directory = full_path;

    this->send("OK\nChanged directory to " + path);
}

void Session::makeDirectory(const std::string &path) {
    if (path.empty()) {
        throw std::runtime_error("no_path: MKDIR command requires a path argument");
    }
    std::string full_path = this->path(path);
    this->verifyPath(full_path, VerifyType::None, VerifyExistence::MustNotExist);

    std::filesystem::create_directories(full_path);

    this->send("OK\nCreated directory " + path);
}

void Session::removeDirectory(const std::string &path) {
    if (path.empty()) {
        throw std::runtime_error("no_path: RMDIR command requires a path argument");
    }
    std::string full_path = this->path(path);
    this->verifyPath(full_path, VerifyType::Directory, VerifyExistence::MustExist);

    std::filesystem::remove_all(full_path);

    this->send("OK\nRemoved directory " + path);
}

void Session::move(const std::string &source, const std::string &destination) {
    if (source.empty() || destination.empty()) {
        throw std::runtime_error("no_path: MOVE command requires source and destination path arguments");
    }
    std::string full_source_path = this->path(source);
    std::string full_destination_path = this->path(destination);
    this->verifyPath(full_source_path, VerifyType::None, VerifyExistence::MustExist);
    this->verifyPath(full_destination_path, VerifyType::None, VerifyExistence::MustNotExist);

    std::string dest_parent = full_destination_path.substr(0, full_destination_path.find_last_of("/\\"));
    std::filesystem::create_directories(dest_parent);
    std::filesystem::rename(full_source_path, full_destination_path);

    this->send("OK\nMoved " + source + " to " + destination);
}

void Session::copy(const std::string &source, const std::string &destination) {
    if (source.empty() || destination.empty()) {
        throw std::runtime_error("no_path: COPY command requires source and destination path arguments");
    }
    std::string full_source_path = this->working_directory + "/" + source;
    std::string full_destination_path = this->working_directory + "/" + destination;
    this->verifyPath(full_source_path, VerifyType::None, VerifyExistence::MustExist);
    this->verifyPath(full_destination_path, VerifyType::None, VerifyExistence::MustNotExist);

    std::string dest_parent = full_destination_path.substr(0, full_destination_path.find_last_of("/\\"));
    std::filesystem::create_directories(dest_parent);
    std::filesystem::copy(full_source_path, full_destination_path, std::filesystem::copy_options::recursive);

    this->send("OK\nCopied " + source + " to " + destination);
}

void Session::lockFileForDownload(const std::string &filepath) {
    std::unique_lock<std::shared_mutex> lock(files_mutex);
    locked_files.insert(filepath);
}

void Session::unlockFileForDownload(const std::string &filepath) {
    std::unique_lock<std::shared_mutex> lock(files_mutex);
    locked_files.erase(filepath);
}

bool Session::isFileLocked(const std::string &filepath) {
    std::shared_lock<std::shared_mutex> lock(files_mutex);
    return locked_files.count(filepath) > 0;
}