#include "session.hpp"
#include "access_control.hpp"

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
        if (!exists_user(username, this->root)) {
            this->send("User " + username + " not found. Register? (y/n)");
            this->state = State::AwaitingRegistrationChoice; // implement register()
            
        } else {
            // existing user -> ask for password
            this->send("Password for " + username + ":");
            this->state = State::AwaitingPassword;
        }
        
        // no username -> public mode
    } else {
        this->resumeUpload();
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
    register_user(this->client_username, password, this->root);
    this->send("User " + this->client_username + " registered successfully.");
    this->exit();
}

void Session::authenticateUser(std::string password) {
    // authenticate user
    if (authenticate_user(this->client_username, password, this->root)) {
        this->send("Logged as " + this->client_username + ".");
    } else {
        this->send("Authentication failed: Incorrect password.");
    }
    this->client_directory = this->root + "/" + this->client_username;
    this->working_directory = this->client_directory;
    if (!std::filesystem::exists(this->client_directory)) {
        std::filesystem::create_directory(this->client_directory);
    }
    this->state = State::AwaitingMessage;
    this->resumeUpload(); // proceed to resuming uploads
}