#include "flows/authenticate_flow.hpp"

AuthenticateFlow::AuthenticateFlow(Session* s, const std::string &user) : Flow(s), username(user) {

    // public mode
    if (user.empty()) {
        s->send("[warning] operating in public mode - files are visible to everyone");
        this->session->leaveFlow();
        return;
    }

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

void AuthenticateFlow::success() {
    this->session->setClientUsername(this->username);
    this->session->setClientDirectory("server/root/" + this->username);
    this->session->setWorkingDirectory("server/root/" + this->username);
    this->session->leaveFlow();
}

void AuthenticateFlow::onMessage(const std::string& msg) {
    switch (this->state) {
        // registration choice
        case State::AwaitingRegistrationChoice:
            if (msg == "y") { // yes -> ask for password
                this->session->send("Password for " + this->username + ":");
                this->state = State::AwaitingRegistrationPassword;
            } else { // no -> cancel flow
                this->session->leaveFlow();
            }
            break;

        // registration password
        case State::AwaitingRegistrationPassword:
            register_user(this->username, msg);
            this->session->send("Registration successful.");
            
            std::filesystem::create_directories("server/root/" + this->username);

            this->success();
            break;

        // authentication password
        case State::AwaitingAuthenticationPassword:
            if (verify(this->username, msg)) {
                this->session->send("Authentication successful.");
                this->success();
            } else {
                this->session->send("Authentication failed.\n[warning] operating in public mode - files are visible to everyone");
                this->session->leaveFlow();
            }
            break;
    }
}