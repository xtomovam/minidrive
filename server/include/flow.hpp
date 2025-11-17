#pragma once

#include <string>

class Session;

class Flow {
public:
    Flow(Session* s);
    virtual ~Flow() = default;

    virtual void onMessage(const std::string& msg);

protected:
    Session* session;
};

class AuthenticateFlow : public Flow {
public:
    AuthenticateFlow(Session* s, const std::string &user);

    void onMessage(const std::string& msg) override;
    
private:
    enum class State {
        AwaitingRegistrationChoice,
        AwaitingRegistrationPassword,
        AwaitingAuthenticationPassword
    };
    State state;
    std::string username;
};