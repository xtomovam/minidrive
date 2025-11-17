#include "flows/authenticate_flow.hpp"

AuthenticateFlow::AuthenticateFlow(Session* s, const std::string &user) : Flow(s), username(user) {
    // user not exists -> ask for registration
    if (!exists_user(user)) {
        s->send("User " + user + " not found. Register? (y/n):");
        this->state = State::AwaitingRegistrationChoice;
    }

    // user exists -> ask for authentication
    else {
        s->send("Password for " + user + ":");
        this->state = State::AwaitingAuthenticationPassword;
    }
}

void AuthenticateFlow::onMessage(const std::string& msg) {
    switch (this->state) {
        // registration choice
        case State::AwaitingRegistrationChoice:
            if (msg == "y") { // yes -> ask for password
                this->session->send("Password for " + this->username + ":");
                this->state = State::AwaitingRegistrationPassword;
            } else { // no -> cancel flow
                this->session->send("Registration cancelled.");
                this->session->leaveFlow();
            }
            break;

        // registration password
        case State::AwaitingRegistrationPassword:
            register_user(this->username, msg);
            std::filesystem::create_directories(this->session->getRoot() + "/" + this->username);
            
            this->session->send("Registration successful.");

            this->session->exit();
            break;

        // authentication password
        case State::AwaitingAuthenticationPassword:
            if (verify(this->username, msg)) {
                this->session->send("Logged as " + this->username);
                this->session->setClientUsername(this->username);
                this->session->setClientDirectory(this->session->getRoot() + "/" + this->username);
                this->session->setWorkingDirectory(this->session->getRoot() + "/" + this->username);
                this->session->leaveFlow();
            } else {
                this->session->send("Authentication failed.\n[warning] operating in public mode - files are visible to everyone");
                this->session->leaveFlow();
            }
            break;
    }
}