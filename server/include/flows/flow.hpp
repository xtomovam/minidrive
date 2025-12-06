#pragma once

#include "session.hpp"

class Flow {
public:
    Flow(Session *s);
    virtual ~Flow() = default;

    virtual void onMessage(const std::string& msg);

protected:
    Session* session;
};