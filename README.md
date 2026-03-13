# VaultBox - Local Password Manager

A **KeePass alternative** with a modern UI. VaultBox is a fork of Bitwarden's browser extension that stores everything locally on your computer. No cloud servers, no accounts to create online, no telemetry.

## Quick Install (Windows)

```powershell
irm https://github.com/SysAdminDoc/VaultBox/releases/latest/download/Install-VaultBox.ps1 -OutFile Install-VaultBox.ps1; .\Install-VaultBox.ps1
```

No prerequisites needed. The installer handles everything automatically - including Python, the local server, and browser extension setup.

Or download manually from the [latest release](https://github.com/SysAdminDoc/VaultBox/releases/latest):

| Asset                         | Description                                  |
| ----------------------------- | -------------------------------------------- |
| `Install-VaultBox.ps1`        | One-click GUI installer (recommended)        |
| `VaultBox-v0.1.0-chrome.zip`  | Chrome/Edge/Brave extension (manual install) |
| `VaultBox-v0.1.0-firefox.zip` | Firefox extension (manual install)           |

## What This Is

VaultBox gives you Bitwarden's full password management experience (autofill, search, password generation, TOTP) while keeping everything on your machine. Think of it as **KeePass with browser autofill and a modern UI**.

The extension talks to a lightweight local server running on `127.0.0.1:8787` instead of Bitwarden's cloud. Your passwords never leave your device.

## Architecture

```text
+-------------------+          +----------------------+          +------------------+
|  Browser Extension | <------> |  VaultBox Server     | <------> |  SQLite Database |
|  (Bitwarden UI)   |  HTTP    |  127.0.0.1:8787      |          |  vault.db        |
+-------------------+  only    +----------------------+          +------------------+
                      localhost   Python + FastAPI                 %LOCALAPPDATA%\VaultBox\
                                  System tray icon
```

- **Extension** = Standard Bitwarden browser extension pointed at localhost
- **Server** = Bitwarden-compatible API running locally (never binds to network interfaces)
- **Database** = SQLite storing encrypted vault data on disk

All encryption/decryption happens in the extension (client-side). The server only stores already-encrypted data. This matches Bitwarden's zero-knowledge architecture.

## Key Features

- **Localhost Only** - Server binds to `127.0.0.1:8787`. No external network access. No DNS lookups.
- **Local Vault Storage** - Encrypted vault data stored in SQLite at `%LOCALAPPDATA%\VaultBox\vault.db`.
- **Full Autofill** - Login, credit card, identity, and FIDO2 autofill works exactly like standard Bitwarden.
- **Password Generator** - Fully client-side password and passphrase generation.
- **TOTP Codes** - Time-based one-time passwords generated locally.
- **System Tray** - Server runs in the system tray with status indicator and quick access to data folder.
- **Auto-Start** - Server starts automatically on Windows login.
- **No Telemetry** - Event collection, usage analytics, and crash reporting are completely removed.
- **No Prerequisites** - Installer auto-downloads portable Python if needed. Nothing to install first.

## How It Works

1. **Install** - Run `Install-VaultBox.ps1` (one click)
2. **Server starts** - VaultBox Server runs in the system tray on `127.0.0.1:8787`
3. **Create account** - Open the extension, register with email + master password (stored locally)
4. **Use normally** - Add passwords, autofill, generate passwords, organize with folders
5. **Backup** - Copy `%LOCALAPPDATA%\VaultBox\vault.db` to USB/NAS for backup

## VaultBox vs KeePass vs Bitwarden

| Feature            | KeePass              | VaultBox              | Bitwarden         |
| ------------------ | -------------------- | --------------------- | ----------------- |
| Storage            | Local .kdbx file     | Local SQLite          | Cloud             |
| Browser Autofill   | Via plugins (clunky) | Native (built-in)     | Native (built-in) |
| UI                 | Desktop-era          | Modern (Bitwarden UI) | Modern            |
| Password Generator | Yes                  | Yes                   | Yes               |
| TOTP               | Via plugins          | Built-in              | Built-in (paid)   |
| Multi-device Sync  | Manual file copy     | Manual file copy      | Automatic         |
| Internet Required  | No                   | No                    | Yes               |
| Open Source        | Yes                  | Yes                   | Yes               |

## Building from Source

### Prerequisites

- Node.js v22 (check `.nvmrc`)
- npm
- Python 3.10+ (for the server)

### Build the Extension

```bash
git clone <this-repo> vaultbox
cd vaultbox

npm ci

# Chrome (MV3)
cd apps/browser
npm run build:chrome

# Firefox (MV2)
npm run build:firefox
```

### Run the Server

```bash
cd server
pip install -r requirements.txt
python vaultbox_server.py
```

The server starts on `http://127.0.0.1:8787` with a system tray icon.

### Load in Chrome

1. Open `chrome://extensions/`
2. Enable "Developer mode" (top right)
3. Click "Load unpacked"
4. Select the `apps/browser/build/` directory

### Load in Firefox

1. Open `about:debugging#/runtime/this-firefox`
2. Click "Load Temporary Add-on..."
3. Select any file inside the `apps/browser/build/` directory

## Server API

The VaultBox server implements the Bitwarden-compatible API:

| Endpoint                                     | Method | Description            |
| -------------------------------------------- | ------ | ---------------------- |
| `/accounts/prelogin`                         | POST   | Get KDF parameters     |
| `/accounts/register/send-verification-email` | POST   | Start registration     |
| `/accounts/register/finish`                  | POST   | Complete registration  |
| `/connect/token`                             | POST   | Login (get JWT + keys) |
| `/sync`                                      | GET    | Full vault sync        |
| `/ciphers`                                   | POST   | Create vault item      |
| `/ciphers/{id}`                              | PUT    | Update vault item      |
| `/ciphers/{id}`                              | DELETE | Delete vault item      |
| `/folders`                                   | POST   | Create folder          |
| `/folders/{id}`                              | PUT    | Update folder          |
| `/folders/{id}`                              | DELETE | Delete folder          |
| `/ciphers/import`                            | POST   | Bulk import            |
| `/accounts/profile`                          | GET    | User profile           |
| `/accounts/password`                         | POST   | Change master password |

All other Bitwarden API endpoints return safe defaults (empty lists, 200 OK).

## Security Model

- **Encryption**: AES-256-CBC with HMAC-SHA256 (same as Bitwarden)
- **Key Derivation**: Argon2id or PBKDF2-SHA256 (configurable)
- **Master Password**: Never stored in plaintext. Only the derived hash is used for validation.
- **Localhost Only**: Server binds exclusively to `127.0.0.1` - inaccessible from other machines.
- **Zero Knowledge**: Server stores only encrypted data. Decryption happens in the browser extension.
- **No Tracking**: All event collection, analytics, and telemetry services are disabled.

## Files

### Server

- `server/vaultbox_server.py` - Local Bitwarden-compatible API server (FastAPI + SQLite + system tray)
- `server/requirements.txt` - Python dependencies

### Extension Modifications

- `apps/browser/src/background/main.background.ts` - Standard API/Sync services pointed at localhost
- `apps/browser/src/platform/offline/offline-event-upload.service.ts` - No-op telemetry
- `apps/browser/src/manifest.v3.json` - Stripped permissions, rebranded
- `apps/browser/src/manifest.json` - Stripped permissions, rebranded (MV2)
- `apps/browser/src/_locales/en/messages.json` - Rebranded strings
- `libs/common/src/platform/services/default-environment.service.ts` - URLs point to localhost:8787
- `apps/browser/src/background/runtime.background.ts` - Disabled install page redirect

## Limitations

- **No Multi-Device Sync** - By design. Copy the SQLite database to transfer vaults between machines.
- **No Account Recovery** - No server to recover your account. Back up your database and remember your master password.
- **No Organization Features** - Organization sharing requires a cloud server. Personal vault only.
- **No Breach Reports** - HIBP password checking requires external network access.
- **No Emergency Access** - Requires cloud-side trusted contacts feature.

## License

This project is a fork of [Bitwarden](https://github.com/bitwarden/clients) and is subject to the Bitwarden License Agreement. VaultBox is not affiliated with Bitwarden Inc. See the original LICENSE files for details.
