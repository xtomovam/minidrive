#pragma once

#include "flows/flow.hpp"

class UploadFlow : public Flow {
public:
    UploadFlow(Session* s, const std::string local_path, const std::string remote_path, size_t filesize);

    void onMessage(const std::string& msg) override;

private:
    std::string remote_path = "";
    std::string full_remote_path = "";
};