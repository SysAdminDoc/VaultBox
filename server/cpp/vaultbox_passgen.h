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
    auto randbytes = VBCrypto::random_bytes(len);
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; i++) {
        result += charset[randbytes[i] % charset.size()];
    }
    return result;
}

} // namespace VBPassGen
