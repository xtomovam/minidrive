#pragma once

#include "flows/flow.hpp"
#include "access_control.hpp"

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