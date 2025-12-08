#include "session.hpp"

void Session::resume() {
    // check for active transfers to resume
    TransferState::clearTransfers(this->getClientDirectory());
    std::vector<TransferState::Transfer> transfers = TransferState::getActiveTransfers(this->getClientDirectory());

    if (!transfers.empty()) {
        this->send("RESUME " + transfers[0].local_path +  " " + transfers[0].remote_path + " " + std::to_string(transfers[0].bytes_completed));
        this->setCurrentTransfer(transfers[0]);
        this->state = State::AwaitingResumeChoice;
    } else {
        this->send("RESUME");
    }
}

void Session::processResumeChoice(std::string choice) {
    if (choice == "y") {
        this->state = State::AwaitingFile;
    } else {
        this->state = State::AwaitingMessage;
    }
}