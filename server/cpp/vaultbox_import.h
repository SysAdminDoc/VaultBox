// VaultBox Desktop - Import/Export
#pragma once
#include "vaultbox_server.h"
#include "vaultbox_db.h"
#include "vaultbox_crypto.h"

namespace VBImport {

// ============================================================================
// CSV Parser (handles quoted fields, including fields with embedded newlines)
// ============================================================================
inline std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    i++;
                } else {
                    inQuotes = false;
                }
            } else {
                field += c;
            }
        } else {
            if (c == '"') {
                inQuotes = true;
            } else if (c == ',') {
                fields.push_back(field);
                field.clear();
            } else if (c != '\r') {
                field += c;
            }
        }
    }
    fields.push_back(field);
    return fields;
}

// Read a full logical CSV record from the stream. A record may span multiple
// physical lines if a quoted field contains embedded newlines (e.g. multi-line
// notes in a Bitwarden export). Returns false at EOF without any data.
inline bool read_csv_record(std::istream& in, std::string& out) {
    out.clear();
    std::string line;
    if (!std::getline(in, line)) return false;
    out = line;
    // Count unescaped quotes to detect "we're still inside a quoted field".
    auto unbalanced = [](const std::string& s) {
        bool inQ = false;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '"') {
                if (inQ && i + 1 < s.size() && s[i + 1] == '"') { i++; continue; }
                inQ = !inQ;
            }
        }
        return inQ;
    };
    while (unbalanced(out) && std::getline(in, line)) {
        out += "\n";
        out += line;
    }
    return true;
}

inline std::string csv_escape(const std::string& s) {
    if (s.find_first_of(",\"\r\n") == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

// ============================================================================
// Import Bitwarden JSON (plaintext export format)
// ============================================================================
inline int import_bitwarden_json(const std::string& filepath) {
    if (!g_vault.unlocked) return -1;

    std::ifstream f(filepath);
    if (!f.is_open()) return -1;

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    json data;
    try { data = json::parse(content); } catch (...) { return -1; }

    // Reject Bitwarden's encrypted export format - we can't decrypt it with the
    // current user key (it uses a password-derived key from the exporting account).
    // Telling the user is much friendlier than importing zero items.
    if (data.is_object() && data.contains("encrypted") && data["encrypted"].is_boolean() && data["encrypted"].get<bool>()) {
        vb_log("Import rejected: encrypted Bitwarden JSON export not supported. Re-export unencrypted.");
        return -2;
    }

    int count = 0;
    {
        std::lock_guard<std::mutex> lk(g_vault.mtx);
        if (!g_vault.unlocked) return -1;

        DB db;
        db.exec("BEGIN IMMEDIATE");
        try {
            // Import folders first, mapping the export's folder ids to freshly
            // generated local ids so we can rewire item folderIds below.
            std::map<std::string, std::string> folderIdMap;
            if (data.contains("folders") && data["folders"].is_array()) {
                for (auto& fj : data["folders"]) {
                    std::string name = fj.value("name", "");
                    if (name.empty()) continue;
                    std::string newId = VBCrypto::save_folder_tx(db, name);
                    if (!newId.empty()) {
                        std::string oldId = fj.value("id", "");
                        if (!oldId.empty()) folderIdMap[oldId] = newId;
                    }
                }
            }

            auto items = data.value("items", json::array());
            for (auto& item : items) {
                DecryptedEntry de;
                de.name = item.value("name", "");
                de.notes = item.value("notes", "");
                de.type = item.value("type", 1);
                de.favorite = item.value("favorite", false);

                std::string oldFolderId = item.value("folderId", "");
                if (!oldFolderId.empty() && folderIdMap.count(oldFolderId))
                    de.folderId = folderIdMap[oldFolderId];

                if (item.contains("login") && item["login"].is_object()) {
                    auto& login = item["login"];
                    de.username = login.value("username", "");
                    de.password = login.value("password", "");
                    de.totp = login.value("totp", "");
                    if (login.contains("uris") && login["uris"].is_array() && !login["uris"].empty())
                        de.uri = login["uris"][0].value("uri", "");
                }
                if (item.contains("card") && item["card"].is_object()) {
                    auto& card = item["card"];
                    de.username = card.value("cardholderName", "");
                    de.password = card.value("number", "");
                }
                if (item.contains("identity") && item["identity"].is_object()) {
                    auto& ident = item["identity"];
                    de.username = ident.value("firstName", "") + std::string(" ") + ident.value("lastName", "");
                    de.uri = ident.value("email", "");
                }

                if (VBCrypto::save_entry_tx(db, de, true)) count++;
            }
            db.exec("COMMIT");
        } catch (...) {
            db.exec("ROLLBACK");
            return -1;
        }
    }

    VBCrypto::refresh_vault();
    vb_log("Imported " + std::to_string(count) + " entries from Bitwarden JSON");
    return count;
}

// ============================================================================
// Import Bitwarden CSV
// ============================================================================
inline int import_bitwarden_csv(const std::string& filepath) {
    if (!g_vault.unlocked) return -1;

    std::ifstream f(filepath);
    if (!f.is_open()) return -1;

    std::string record;
    if (!read_csv_record(f, record)) return -1; // header

    int count = 0;
    {
        std::lock_guard<std::mutex> lk(g_vault.mtx);
        if (!g_vault.unlocked) return -1;

        // Seed folder-name -> id map from the already-decrypted in-memory vault.
        std::map<std::string, std::string> nameToId;
        for (auto& ef : g_vault.folders) nameToId[ef.name] = ef.id;

        DB db;
        db.exec("BEGIN IMMEDIATE");
        try {
            while (read_csv_record(f, record)) {
                if (record.empty()) continue;
                auto fields = parse_csv_line(record);
                if (fields.size() < 10) continue;

                DecryptedEntry de;
                // folder=0, favorite=1, type=2, name=3, notes=4, fields=5, reprompt=6,
                // login_uri=7, login_username=8, login_password=9, login_totp=10
                std::string folderName = fields[0];
                de.favorite = (fields[1] == "1");
                de.type = 1; // Login
                if (fields[2] == "note") de.type = 2;
                else if (fields[2] == "card") de.type = 3;
                else if (fields[2] == "identity") de.type = 4;
                de.name = fields[3];
                de.notes = fields[4];
                de.uri = fields.size() > 7 ? fields[7] : "";
                de.username = fields.size() > 8 ? fields[8] : "";
                de.password = fields.size() > 9 ? fields[9] : "";
                de.totp = fields.size() > 10 ? fields[10] : "";

                if (!folderName.empty()) {
                    auto it = nameToId.find(folderName);
                    if (it == nameToId.end()) {
                        std::string fid = VBCrypto::save_folder_tx(db, folderName);
                        if (!fid.empty()) {
                            nameToId[folderName] = fid;
                            de.folderId = fid;
                        }
                    } else {
                        de.folderId = it->second;
                    }
                }

                if (VBCrypto::save_entry_tx(db, de, true)) count++;
            }
            db.exec("COMMIT");
        } catch (...) {
            db.exec("ROLLBACK");
            return -1;
        }
    }

    VBCrypto::refresh_vault();
    vb_log("Imported " + std::to_string(count) + " entries from Bitwarden CSV");
    return count;
}

// ============================================================================
// Import Chrome CSV (name, url, username, password)
// ============================================================================
inline int import_chrome_csv(const std::string& filepath) {
    if (!g_vault.unlocked) return -1;

    std::ifstream f(filepath);
    if (!f.is_open()) return -1;

    std::string record;
    if (!read_csv_record(f, record)) return -1; // header

    int count = 0;
    {
        std::lock_guard<std::mutex> lk(g_vault.mtx);
        if (!g_vault.unlocked) return -1;

        DB db;
        db.exec("BEGIN IMMEDIATE");
        try {
            while (read_csv_record(f, record)) {
                if (record.empty()) continue;
                auto fields = parse_csv_line(record);
                if (fields.size() < 4) continue;

                DecryptedEntry de;
                de.type = 1;
                de.name = fields[0];
                de.uri = fields[1];
                de.username = fields[2];
                de.password = fields[3];
                if (fields.size() > 4) de.notes = fields[4];

                if (VBCrypto::save_entry_tx(db, de, true)) count++;
            }
            db.exec("COMMIT");
        } catch (...) {
            db.exec("ROLLBACK");
            return -1;
        }
    }

    VBCrypto::refresh_vault();
    vb_log("Imported " + std::to_string(count) + " entries from Chrome CSV");
    return count;
}

// Decode the small set of XML entities that appear in KeePass plaintext exports.
// KeePass escapes: & < > " ' as &amp; &lt; &gt; &quot; &apos;. Numeric refs are rare
// in KeePass output but we handle them defensively.
inline std::string xml_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ) {
        if (s[i] != '&') { out += s[i++]; continue; }
        auto semi = s.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 8) { out += s[i++]; continue; }
        std::string ent = s.substr(i + 1, semi - i - 1);
        if (ent == "amp")       out += '&';
        else if (ent == "lt")   out += '<';
        else if (ent == "gt")   out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#') {
            int code = 0;
            try {
                if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                    code = std::stoi(ent.substr(2), nullptr, 16);
                else
                    code = std::stoi(ent.substr(1));
            } catch (...) { code = 0; }
            if (code > 0 && code < 0x80) out += (char)code;
            else if (code >= 0x80 && code < 0x800) {
                out += (char)(0xC0 | (code >> 6));
                out += (char)(0x80 | (code & 0x3F));
            } else if (code >= 0x800 && code < 0x10000) {
                out += (char)(0xE0 | (code >> 12));
                out += (char)(0x80 | ((code >> 6) & 0x3F));
                out += (char)(0x80 | (code & 0x3F));
            }
        } else {
            out += s.substr(i, semi - i + 1); // unknown entity: keep as-is
        }
        i = semi + 1;
    }
    return out;
}

// ============================================================================
// Import KeePass XML (KDBX4 export format)
// ============================================================================
// Minimal XML parser for KeePass export (no external dependency)
inline int import_keepass_xml(const std::string& filepath) {
    if (!g_vault.unlocked) return -1;

    std::ifstream f(filepath);
    if (!f.is_open()) return -1;

    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    int count = 0;
    {
        std::lock_guard<std::mutex> lk(g_vault.mtx);
        if (!g_vault.unlocked) return -1;

        DB db;
        db.exec("BEGIN IMMEDIATE");
        try {
            size_t pos = 0;
            // Find each <Entry> block
            while ((pos = content.find("<Entry>", pos)) != std::string::npos) {
                size_t end = content.find("</Entry>", pos);
                if (end == std::string::npos) break;

                std::string entry = content.substr(pos, end - pos + 8);
                pos = end + 8;

                DecryptedEntry de;
                de.type = 1;

                // Extract <String><Key>...</Key><Value>...</Value></String> pairs
                size_t spos = 0;
                while ((spos = entry.find("<String>", spos)) != std::string::npos) {
                    size_t send = entry.find("</String>", spos);
                    if (send == std::string::npos) break;

                    std::string block = entry.substr(spos, send - spos);
                    spos = send + 9;

                    auto kstart = block.find("<Key>"); auto kend = block.find("</Key>");
                    if (kstart == std::string::npos || kend == std::string::npos) continue;
                    std::string key = block.substr(kstart + 5, kend - kstart - 5);

                    auto vstart = block.find("<Value"); auto vend = block.find("</Value>");
                    if (vstart == std::string::npos || vend == std::string::npos) continue;
                    auto vclose = block.find('>', vstart);
                    if (vclose == std::string::npos) continue;
                    std::string val = block.substr(vclose + 1, vend - vclose - 1);

                    val = xml_decode(val);
                    if (key == "Title") de.name = val;
                    else if (key == "UserName") de.username = val;
                    else if (key == "Password") de.password = val;
                    else if (key == "URL") de.uri = val;
                    else if (key == "Notes") de.notes = val;
                }

                if (!de.name.empty()) {
                    if (VBCrypto::save_entry_tx(db, de, true)) count++;
                }
            }
            db.exec("COMMIT");
        } catch (...) {
            db.exec("ROLLBACK");
            return -1;
        }
    }

    VBCrypto::refresh_vault();
    vb_log("Imported " + std::to_string(count) + " entries from KeePass XML");
    return count;
}

// ============================================================================
// Export as Bitwarden JSON
// ============================================================================
inline bool export_bitwarden_json(const std::string& filepath) {
    std::lock_guard<std::mutex> lk(g_vault.mtx);
    if (!g_vault.unlocked) return false;

    json output;
    output["encrypted"] = false;

    json folders = json::array();
    for (auto& f : g_vault.folders) {
        folders.push_back({{"id", f.id}, {"name", f.name}});
    }
    output["folders"] = folders;

    json items = json::array();
    for (auto& e : g_vault.entries) {
        json item;
        item["id"] = e.id;
        item["folderId"] = e.folderId.empty() ? json(nullptr) : json(e.folderId);
        item["organizationId"] = nullptr;
        item["collectionIds"] = nullptr;
        item["name"] = e.name;
        item["notes"] = e.notes.empty() ? json(nullptr) : json(e.notes);
        item["type"] = e.type;
        item["favorite"] = e.favorite;
        item["reprompt"] = 0;

        if (e.type == 1) {
            json login;
            login["username"] = e.username.empty() ? json(nullptr) : json(e.username);
            login["password"] = e.password.empty() ? json(nullptr) : json(e.password);
            if (!e.uri.empty()) {
                login["uris"] = json::array({json({{"match", nullptr}, {"uri", e.uri}})});
            } else {
                login["uris"] = json::array();
            }
            login["totp"] = e.totp.empty() ? json(nullptr) : json(e.totp);
            item["login"] = login;
        } else if (e.type == 2) {
            item["secureNote"] = json({{"type", 0}});
        } else if (e.type == 3) {
            json card;
            card["cardholderName"] = e.username;
            card["number"] = e.password;
            item["card"] = card;
        } else if (e.type == 4) {
            json ident;
            auto sp = e.username.find(' ');
            if (sp != std::string::npos) {
                ident["firstName"] = e.username.substr(0, sp);
                ident["lastName"] = e.username.substr(sp + 1);
            } else {
                ident["firstName"] = e.username;
                ident["lastName"] = "";
            }
            ident["email"] = e.uri;
            item["identity"] = ident;
        }
        items.push_back(item);
    }
    output["items"] = items;

    std::ofstream out(filepath);
    if (!out.is_open()) return false;
    out << output.dump(2);
    out.close();

    vb_log("Exported " + std::to_string(items.size()) + " entries to Bitwarden JSON");
    return true;
}

// ============================================================================
// Export as CSV
// ============================================================================
inline bool export_csv(const std::string& filepath) {
    std::lock_guard<std::mutex> lk(g_vault.mtx);
    if (!g_vault.unlocked) return false;

    std::ofstream out(filepath);
    if (!out.is_open()) return false;

    out << "folder,favorite,type,name,notes,fields,reprompt,login_uri,login_username,login_password,login_totp\n";

    for (auto& e : g_vault.entries) {
        std::string typeName = "login";
        if (e.type == 2) typeName = "note";
        else if (e.type == 3) typeName = "card";
        else if (e.type == 4) typeName = "identity";

        out << csv_escape(e.folderName) << ","
            << (e.favorite ? "1" : "") << ","
            << typeName << ","
            << csv_escape(e.name) << ","
            << csv_escape(e.notes) << ","
            << "," // fields
            << "," // reprompt
            << csv_escape(e.uri) << ","
            << csv_escape(e.username) << ","
            << csv_escape(e.password) << ","
            << csv_escape(e.totp) << "\n";
    }

    out.close();
    vb_log("Exported " + std::to_string(g_vault.entries.size()) + " entries to CSV");
    return true;
}

// ============================================================================
// File open/save dialog helpers
// ============================================================================
inline std::string open_file_dialog(HWND parent, const wchar_t* filter, const wchar_t* title) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn)) return from_wstr(path);
    return "";
}

inline std::string save_file_dialog(HWND parent, const wchar_t* filter, const wchar_t* title, const wchar_t* defExt) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_OVERWRITEPROMPT;
    if (GetSaveFileNameW(&ofn)) return from_wstr(path);
    return "";
}

} // namespace VBImport
