#pragma once

#include "flows/flow.hpp"

class UploadFlow : public Flow {
public:
    UploadFlow(Session* s, const std::string local_path, const std::string remote_path, const size_t &filesiz);

    void onMessage(const std::string& msg) override;

private:
    std::string local_path = "";
    std::string remote_path = "";
    size_t filesize = 0;
    size_t bytes_remaining = 0;
    bool resume = false;
};