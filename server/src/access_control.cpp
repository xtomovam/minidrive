#include "access_control.hpp"

std::string hash_pwd(const std::string &password) {
    char hash[crypto_pwhash_STRBYTES];
    if (
        crypto_pwhash_str(
            hash,
            password.c_str(),
            password.size(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE
        ) != 0
    ) {
        throw std::runtime_error("hashing_failed: Password hashing failed");
    }
    return std::string(hash);
}

bool verify_pwd(const std::string &hash, const std::string &password) {
    return crypto_pwhash_str_verify(
        hash.c_str(),
        password.c_str(),
        password.size()
    ) == 0;
}

nlohmann::json load_users(const std::string &root) {
    std::string path = root + "/users.json";
    // if file does not exist, create empty database
    if (!std::filesystem::exists(path)) {
        nlohmann::json empty = nlohmann::json::object();
        std::ofstream outfile(path);
        if (!outfile.is_open()) {
            throw std::runtime_error("database: Could not create users database");
        }
        outfile << empty.dump(4);
        return empty;
    }

    // load database
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("database: Could not open users database for reading");
    }
    nlohmann::json users;
    file >> users;
    return users;
}

void save_users(const std::string &root, const nlohmann::json &users) {
    std::ofstream file(root + "/users.json");
    if (!file.is_open()) {
        throw std::runtime_error("database: Could not open users database for writing");
    }
    file << users.dump(4);
}

bool exists_user(const std::string &user) {
    nlohmann::json users = load_users("server/root");
    return users.contains(user);
}

void register_user(const std::string &user, const std::string &password) {
    nlohmann::json users = load_users("server/root");
    if (users.contains(user)) {
        throw std::runtime_error("user_exists: User already exists");
    }
    users[user] = hash_pwd(password);
    save_users("server/root", users);
}

bool verify(const std::string &user, const std::string &password) {
    nlohmann::json users = load_users("server/root");
    if (!users.contains(user)) {
        return false;
    }
    std::string stored_hash = users[user];
    return verify_pwd(stored_hash, password);
}