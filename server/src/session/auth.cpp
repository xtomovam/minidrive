#include "session.hpp"
#include "access_control.hpp"

void Session::auth(const std::string &username) {
    std::cout << "Auth called for user: " << username << std::endl;
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
            this->send("Password for " + username + ":");
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
    } else {
        this->send("Authentication failed: Incorrect password.");
    }
    this->state = State::AwaitingMessage;
    this->resume(); // proceed to resuming uploads
}