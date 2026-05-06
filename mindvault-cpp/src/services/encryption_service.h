#pragma once
// Encryption service - AES-256 encryption for database
// Note: Requires OpenSSL or similar crypto library
// This is a placeholder implementation

#include <string>
#include <nlohmann/json.hpp>

namespace mindvault::services {

class EncryptionService {
public:
    // Check if encryption is enabled
    bool isEnabled() const {
        return enabled_;
    }

    // Set encryption password
    bool setPassword(const std::string& password) {
        if (password.length() < 8) return false;
        password_ = password;
        enabled_ = true;
        return true;
    }

    // Verify password
    bool verifyPassword(const std::string& password) const {
        return password == password_;
    }

    // Clear encryption
    void disable() {
        enabled_ = false;
        password_.clear();
    }

private:
    bool enabled_ = false;
    std::string password_;

    // TODO: Implement AES-256 encryption/decryption
    // Requires: OpenSSL libsodium or similar
    // Key derivation: PBKDF2 or Argon2
};

} // namespace mindvault::services
