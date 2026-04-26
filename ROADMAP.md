# VaultBox Roadmap

Forward-looking scope for the offline Bitwarden fork (C++ WebView2 + embedded HTTP server + local SQLite vault).

## Planned Features

### Vault & Sync

- USB-keyfile unlock (second factor): require a file on a specific removable drive to decrypt vault.
- YubiKey / FIDO2 hardware-token second factor for master-password-based unlock.
- Multi-device local sync over LAN (mDNS discovery + mutual TLS + vault diff) — no cloud, just peer sync.
- Encrypted vault-diff export for email-based manual sync between machines.
- Vault history (time-travel): restore any item to a version from the last 90 days.

### Import/Export

- Add 1Password 1pux, LastPass CSV, Dashlane JSON, NordPass CSV importers.
- Browser-native exporters: pull credentials from Chrome/Edge/Firefox local password stores (DPAPI/NSS) with consent.
- KeePassXC-compatible export (KDBX v4) so VaultBox can round-trip into the KeePass ecosystem.
- Scheduled encrypted backup to a user-picked folder with retention policy (keep 7 daily / 4 weekly / 12 monthly).

### Security

- Argon2id key derivation option (switch from default PBKDF2-600k) matching KeePassXC and Bitwarden's newer setting.
- Breach check via HIBP k-anonymity API for stored passwords (opt-in, localhost-only proxy).
- Password health report: weak/reused/old entries with per-entry recommendations.
- Duress password: secondary master password that unlocks a decoy vault.
- Auto-lock on workstation lock / idle / screen-saver (extend current idle timer).

### UX

- Built-in TOTP migration from Google Authenticator / Aegis JSON / otpauth-migration URIs.
- Biometric unlock via Windows Hello / Apple Touch ID (already mapped via Bitwarden extension shim; verify end-to-end).
- CLI tool (`vaultbox-cli`) for scripting access (unlock, fetch, insert) from PowerShell/shell.
- PortableApps-compatible packaging so VaultBox drops into the PortableApps menu.

## Competitive Research

- **KeePass / KeePassXC** — the reference local manager; VaultBox wins on "modern UI + autofill that matches stock Bitwarden". Match KeePass on: Argon2id default, keyfile support, history.
- **Bitwarden (self-hosted Vaultwarden)** — closest cousin; VaultBox's pitch is "same UX, no server needed". Document migration path from Vaultwarden.
- **1Password** — best-in-class UX, fully cloud; VaultBox won't compete on polish, but should pick off users who are leaving on privacy grounds.
- **Proton Pass** — new entrant with alias generation; consider adding SimpleLogin-style email alias generator as optional.

## Nice-to-Haves

- Encrypted file attachments per entry (up to a configurable size cap, stored inside vault.db).
- SSH key vault: store private keys, expose via an SSH agent socket on localhost.
- Credit-card autofill with BIN detection and Luhn validation in the generator.
- Time-based one-time "share" link for copying a single credential to a co-worker (local URL, expires in N minutes, localhost).
- Mobile companion (read-only PWA or a minimal native app) that reads an encrypted vault copy from Syncthing.
- CLI auto-type daemon for X11/Wayland-compatible systems (stretch; macOS/Linux).

## Open-Source Research (Round 2)

### Related OSS Projects

- **KeePassXC** — https://github.com/keepassxreboot/keepassxc — C++/Qt, the leading offline password manager; browser integration via KeePassXC-Browser + native messaging, YubiKey HMAC-SHA1 challenge-response, KDBX 4 format
- **Vaultwarden** — https://github.com/dani-garcia/vaultwarden — Rust server, Bitwarden-API compatible; drop-in target if VaultBox ever wants to federate
- **Psono** — https://github.com/psono/psono-client — self-hosted password manager with secret-sharing and TOTP
- **Passbolt** — https://github.com/passbolt/passbolt_api — GPG-based team password manager; interesting audit log + policy model
- **Padloc** — https://github.com/padloc/padloc — TS/E2E-encrypted with WebCrypto; simple vault format worth studying
- **gopass** — https://github.com/gopasspw/gopass — Go/CLI pass-compatible; great sync-over-git model
- **pass (the standard unix password manager)** — https://git.zx2c4.com/password-store — GPG + git pattern; file-per-entry
- **Himitsu** — https://git.sr.ht/~sircmpwn/himitsu — Plan-9-style credential agent for localhost app integration
- **Buttercup** — https://github.com/buttercup/buttercup-desktop — Electron, WebDAV/Dropbox sync, browser extension via native messaging

### Features to Borrow

- KDBX 4 import/export so users can migrate in/out without lock-in (KeePassXC)
- YubiKey HMAC-SHA1 challenge-response as a second unlock factor on top of password (KeePassXC)
- Native-messaging bridge for browser autofill extension (KeePassXC-Browser, Buttercup)
- Per-entry "expire after N days" with a red badge and a "rotate now" action (KeePassXC)
- TOTP with drift correction and a visible countdown ring (KeePassXC, Padloc)
- SSH agent on a localhost Unix-domain-socket / named pipe, keys decrypted from the vault on demand (KeePassXC, gopass)
- Password-strength meter using zxcvbn with per-character feedback (Padloc, Bitwarden)
- Diceware / EFF-wordlist passphrase generator with entropy meter (KeePassXC)
- Audit panel: reused passwords, weak passwords, breached passwords via HIBP k-anonymity (Bitwarden desktop, KeePassXC HIBP)
- "Emergency kit" export — printable PDF with master-password recovery QR (1Password-style; open-source implementations in Padloc)
- Per-folder/collection policies (allow/deny export, allow/deny clipboard, require re-auth) (Psono)
- Git-backed sync branch for power users — treat vault as encrypted blob in a dedicated private repo (pass, gopass)

### Patterns & Architectures Worth Studying

- Argon2id with calibrated iterations per-device (KeePassXC calibrates on first-run to hit a target time budget)
- WebView2 ↔ native-host message-channel pattern via `window.chrome.webview.hostObjects` (Microsoft) vs postMessage JSON (Buttercup)
- Vault-as-single-file with an append-only audit log embedded (KeePassXC KDBX4)
- E2E model where the server is untrusted but still enforces API policies (Bitwarden/Vaultwarden) — useful if VaultBox grows a sync service
- Native-messaging autofill with per-domain user approval and a one-tap allow-list (KeePassXC-Browser) — safer than content-script credential injection
- CLI that operates on the same vault concurrently as the GUI via file-locks (gopass, pass)

## Implementation Deep Dive (Round 3)

### Reference Implementations to Study

- **keepassxreboot/keepassxc src/crypto/** — https://github.com/keepassxreboot/keepassxc/tree/develop/src/crypto — GPLv3 reference for KDBX4 reader/writer, Argon2id KDF, AES-256 + ChaCha20 ciphers; the canonical C++ implementation to mirror
- **keepassxreboot/keepassxc issue #10400** — https://github.com/keepassxreboot/keepassxc/issues/10400 — interop bug: KeePassXC's Bitwarden import only handles PBKDF2, not Argon2id exports; spec constraint for the planned Bitwarden importer
- **bitwarden/clients apps/cli/src/vault** — https://github.com/bitwarden/clients/tree/main/apps/cli/src/vault — official TS reference for vault decrypt flow (stretched master key, protected symmetric key, AES-CBC + HMAC-SHA-256 encrypt-then-MAC)
- **dani-garcia/vaultwarden discussion #2558** — https://github.com/dani-garcia/vaultwarden/discussions/2558 — Argon2 discussion with concrete parameters (`m=64MB, t=3, p=4`) matching Bitwarden server defaults
- **MicrosoftEdge/WebView2Samples Win32_WebView2APISample** — https://github.com/MicrosoftEdge/WebView2Samples/tree/main/SampleApps/WebView2APISample — ClientCertificateRequested + virtual host name mapping + SharedBuffer patterns in one sample
- **MicrosoftDocs working-with-local-content.md** — https://github.com/MicrosoftDocs/edge-developer/blob/main/microsoft-edge/webview2/concepts/working-with-local-content.md — decision matrix for virtual-host vs WebResourceRequested vs real loopback server — favour virtual-host unless REST semantics are needed
- **originaluko/haveibeenpwned** — https://github.com/originaluko/haveibeenpwned — simple Python HIBP k-anonymity client; shape of the 5-char prefix request + local suffix match
- **utelle/wxsqlite3** — https://github.com/utelle/SQLite3MultipleCiphers — drop-in SQLite with AES-256/ChaCha20 at-rest encryption; MIT-licensed alternative to SQLCipher (GPL/commercial)

### Known Pitfalls from Similar Projects

- **AES-CBC not AES-GCM** — https://bitwarden.com/help/bitwarden-security-white-paper/ — Bitwarden uses AES-256-CBC with encrypt-then-MAC HMAC-SHA-256; importing their blobs with a GCM decoder silently produces garbage — MAC verify before decrypt or you enable padding-oracle attacks
- **PBKDF2 600k default** — https://bitwarden.com/help/kdf-algorithms/ — older exports can still be at 100k or 200k iterations; read the per-vault KDF metadata, never hardcode
- **Argon2id memory cost OOM** — dani-garcia vaultwarden discussion above — `m=1GB` settings from security-maxed users will OOM on low-end hardware; clamp to 512MB and warn
- **WebView2 loopback exemption** — https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/working-with-local-content — packaged apps require `CheckNetIsolation.exe LoopbackExempt -a` or fetch to `127.0.0.1` is silently blocked; unpacked Win32 exes are exempt by default
- **IPv4 vs IPv6 loopback split** — https://github.com/MicrosoftEdge/WebView2Feedback/issues/4709 — `localhost` resolves to `::1` first on Win11; if server binds only `127.0.0.1`, first navigation hangs 21s on AAAA timeout — bind both or force `127.0.0.1` in URLs
- **HIBP User-Agent mandatory** — https://www.troyhunt.com/passkeys-k-anonymity-searches-massive-speed-enhancements-bulk-domain-verification-api/ — requests without UA get 403; descriptive UA string per HIBP docs
- **SHA-1 (not SHA-256) for HIBP** — https://haveibeenpwned.com/API/v3 — Pwned Passwords API uses SHA-1 despite SHA-1 being broken for collision — k-anonymity model doesn't need collision resistance, but don't "fix" it to SHA-256
- **SQLCipher license** — https://www.zetetic.net/sqlcipher/open-source/ — open-source SQLCipher is BSD-style but the prebuilt Windows binaries require commercial license; plan for wxSQLite3/SQLite3MultipleCiphers if that's a blocker
- **DPAPI master-key breakage after OS reinstall** — https://learn.microsoft.com/en-us/windows/win32/secauthn/dpapi — DPAPI-wrapped keys don't survive OS rebuild; always require master password as primary, DPAPI only as convenience cache

### Library Integration Checklist

- **OpenSSL 3.3+** or **libsodium 1.0.20** — OpenSSL: AES-256-CBC via `EVP_aes_256_cbc()`, HMAC via `EVP_MAC_fetch("HMAC")`, PBKDF2 via `PKCS5_PBKDF2_HMAC`; libsodium: `crypto_pwhash_ALG_ARGON2ID13` with Bitwarden-matching params — gotcha: OpenSSL's Argon2 API is only in 3.2+
- **SQLite3MultipleCiphers 1.9.0** (prefer over SQLCipher for Windows) — https://github.com/utelle/SQLite3MultipleCiphers — set cipher via `PRAGMA cipher='aes256cbc'` or `chacha20`; gotcha: must call `PRAGMA key='...'` before any query, including `PRAGMA journal_mode=WAL`
- **Microsoft.Web.WebView2 1.0.2792+** (SDK) + **Evergreen Runtime or Fixed Version** — Fixed Version for offline clinic installs; gotcha: SDK and Runtime must have matching major — mismatched interfaces silently return `HRESULT 0x80070490`
- **cpp-httplib 0.18+** (header-only) OR **civetweb 1.16** — for the embedded loopback server; cpp-httplib supports TLS via OpenSSL (`CPPHTTPLIB_OPENSSL_SUPPORT`), mTLS via `set_ca_cert_path` + client cert on WebView2 side; gotcha: cpp-httplib's HTTPS is single-threaded unless you set `new_task_queue`
- **nlohmann/json 3.11+** — for parsing Bitwarden JSON exports and vault entries; gotcha: use `json::parse(..., nullptr, /*allow_exceptions=*/false)` on untrusted import paths — malformed JSON otherwise kills the process
- **libsecp256k1 or OpenSSL EC** for FIDO2 — WebAuthn credential verification uses ES256 (P-256 ECDSA); plan for COSE key parsing — see Yubico's `libfido2` (C library, BSD licensed) at https://github.com/Yubico/libfido2
- **mDNS via mjansson/mdns** — https://github.com/mjansson/mdns — public-domain C library for LAN peer discovery; gotcha: Windows Firewall prompt on first bind — register the UDP 5353 rule during install, not at first run
- **HIBP range API** — no auth, no rate limit, mandatory `User-Agent: VaultBox/<version> (contact)`; call `GET https://api.pwnedpasswords.com/range/{5-hex}` with `Add-Padding: true` header for length-preserving obfuscation against network observers
