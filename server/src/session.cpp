#include "session.hpp"
#include "flows/flow.hpp"
#include "access_control.hpp"
#include "flows/upload_flow.hpp"
#include "flows/upload_resume_flow.hpp"
#include <iostream>

// constructor
Session::Session(const int &fd, const std::string &root, std::function<void(int)> close_callback) : client_fd(fd), root(root), close_callback(close_callback), working_directory(root + "/public"), client_directory(root + "/public") {}

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
            return;
        } else if (this->state == State::AwaitingRegistrationPassword) {
            this->registerUser(msg);
            return;
        } else if (this->state == State::AwaitingPassword) {
            this->authenticateUser(msg);
            return;
        }

        // in a flow -> delegate to flow
        if (this->current_flow) {
            this->current_flow->onMessage(msg);
            return;
        }

        // simple commands
        if (is_cmd(msg, "LIST")) {
            this->list(parts[1]);
        } else if (is_cmd(msg, "DOWNLOAD")) {
            this->downloadFile(parts[1]);
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

        // commands requiring flows
        } else if (is_cmd(msg, "AUTH")) {
            this->auth(parts[1]);
        } else if (is_cmd(msg, "UPLOAD")) {
            this->current_flow = std::make_unique<UploadFlow>(this, parts[2], parts[3], std::stoull(parts[1]));
        } else if (is_cmd(msg, "DOWNLOAD")) {
            this->downloadFile(parts[1]);
        } else {
            throw std::runtime_error("unknown_command: Unknown command: " + msg);
        }
    } catch (const std::exception &e) {
        this->send(std::string("ERROR ") + e.what());
        if (this->current_flow) {
            this->leaveFlow();
        }
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

void Session::receive_file(const std::string &filepath) const {
    this->verifyPath(filepath, VerifyType::File, VerifyExistence::MustNotExist);
    recv_file(this->client_fd, filepath, this->client_directory, 0);
}

void Session::setState(const State &new_state) {
    this->state = new_state;
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

void Session::resume() {
    // check for active transfers to resume
    std::cout << "Checking for active transfers to resume\n";
    TransferState::clearTransfers(this->getClientDirectory());
    std::cout << "Cleared transfers\n";
    std::vector<TransferState::Transfer> transfers = TransferState::getActiveTransfers(this->getClientDirectory());
    std::cout << "Active transfers: " << transfers.size() << std::endl;

    if (!transfers.empty()) {
        std::cout << "Found active transfer to resume" << std::endl;
        std::string full_remote_path = transfers[0].remote_path;
        size_t offset = transfers[0].bytes_completed;
        size_t file_size = transfers[0].total_bytes;
        this->current_flow = std::make_unique<UploadResumeFlow>(this, full_remote_path, file_size, offset);
        std::cout << "Prepared to resume upload of " << full_remote_path << " at offset " << offset << std::endl;
        this->send("RESUME " + transfers[0].local_path +  " " + transfers[0].remote_path + " " + std::to_string(transfers[0].bytes_completed));
    } else {
        std::cout << "No active transfers found" << std::endl;
        this->send("RESUME");
        std::cout << "No transfers to resume" << std::endl;
    }
}

void Session::auth(const std::string &username) {
    // no re-authentication allowed
    if (this->auth_initiated) {
        throw std::runtime_error("permission_denied: Unable to re-authenticate");
    }
    this->auth_initiated = true;

    // set username
    this->client_username = username;
    
    if (!username.empty()) {
        // non-existent user -> prompt for registration
        if (!exists_user(username)) {
            this->send("User " + username + " not found. Register? (y/n)");
            this->state = State::AwaitingRegistrationChoice; // implement register()
            
        } else {
            // existing user -> ask for password
            this->state = State::AwaitingPassword;
        }
        
        // no username -> public mode
    } else {
        this->resume();
    }
}

void Session::processRegisterChoice(std::string choice) {
    if (choice == "y") { // yes -> ask for password
        this->send("Password for " + this->client_username + ":");
        this->state = State::AwaitingRegistrationPassword;
    } else { // no -> cancel flow
        this->send("Registration cancelled.");
    }
}

void Session::registerUser(std::string password) {
    // register user
    register_user(this->client_username, password);
    this->send("User " + this->client_username + " registered successfully.");
    this->exit();
}

void Session::authenticateUser(std::string password) {
    // authenticate user
    if (authenticate_user(this->client_username, password)) {
        this->send("Logged as " + this->client_username + ".");
        this->resume(); // proceed to resume uploads
    } else {
        throw std::runtime_error("authentication_failed: Incorrect password for user " + this->client_username);
    }
}