// VaultBox Desktop - Password Generator
#pragma once
#include "vaultbox_server.h"
#include "vaultbox_crypto.h"

namespace VBPassGen {

struct PassGenOptions {
    int length = 20;
    bool upper = true;
    bool lower = true;
    bool digits = true;
    bool symbols = true;
    bool ambiguous = false;
};

inline std::string generate_password(const PassGenOptions& opts) {
    std::string charset;
    if (opts.lower) charset += "abcdefghijkmnopqrstuvwxyz";
    if (opts.upper) charset += "ABCDEFGHJKLMNPQRSTUVWXYZ";
    if (opts.digits) charset += "23456789";
    if (opts.symbols) charset += "!@#$%^&*()-_=+[]{}:;<>,.?/~";

    if (opts.ambiguous) {
        if (opts.lower) charset += "l";
        if (opts.upper) charset += "IO";
        if (opts.digits) charset += "01";
        charset += "|";
    }

    if (charset.empty()) charset = "abcdefghijkmnopqrstuvwxyz23456789";

    int len = std::max(4, std::min(128, opts.length));
    size_t csz = charset.size();
    // Rejection sampling to eliminate modulo bias
    uint8_t limit = (uint8_t)(256 - (256 % csz));
    std::string result;
    result.reserve(len);
    int filled = 0;
    while (filled < len) {
        auto randbytes = VBCrypto::random_bytes(len - filled + 16);
        for (size_t i = 0; i < randbytes.size() && filled < len; i++) {
            if (randbytes[i] < limit) {
                result += charset[randbytes[i] % csz];
                filled++;
            }
        }
    }
    return result;
}

} // namespace VBPassGen
