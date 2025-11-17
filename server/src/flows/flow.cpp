#include "flows/flow.hpp"
#include "session.hpp"
#include "access_control.hpp"

Flow::Flow(Session* s) : session(s) {
}

void Flow::onMessage(const std::string& msg) {
    (void)msg;
}