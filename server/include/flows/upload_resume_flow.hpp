#pragma once

#include "flows/flow.hpp"

#include "iostream"

class UploadResumeFlow : public Flow {
public:
    UploadResumeFlow(Session* s, const std::string& full_remote_path, const size_t & filesize, const size_t &offset);

    void onMessage(const std::string& msg) override;

private:
    std::string remote_path = "";
    std::string full_remote_path = "";
    size_t filesize = 0;
    size_t offset = 0;
    size_t bytes_remaining = 0;
    bool resume = false;

    TransferState::Transfer getTransfer() const;
};