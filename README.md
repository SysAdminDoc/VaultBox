# VaultBox - Local Password Manager

A **KeePass alternative** with a modern UI. VaultBox is a fork of Bitwarden's browser extension that stores everything in an encrypted local file on your computer. No cloud servers, no accounts to create online, no telemetry.

## Download

Grab the latest from the [releases page](https://github.com/SysAdminDoc/VaultBox/releases/latest):

| Asset                      | Description                           |
| -------------------------- | ------------------------------------- |
| `VaultBox-Setup-x.x.x.exe` | Installer (recommended)               |
| `VaultBox-Server.exe`      | Standalone server (no install needed) |
| `VaultBox-chrome.zip`      | Chrome/Edge/Brave extension (MV3)     |
| `VaultBox-firefox.zip`     | Firefox extension                     |

## What This Is

VaultBox gives you Bitwarden's full password management experience (autofill, search, password generation, TOTP) while keeping everything on your machine. Think of it as **KeePass with browser autofill and a modern UI**.

Your passwords are stored in an encrypted local file (`vault.db`) just like KeePass stores passwords in a `.kdbx` file. Copy it to a USB drive for backup or transfer between machines.

## Architecture

```text
+-------------------+          +----------------------+          +------------------+
|  Browser Extension | <------> |  VaultBox Desktop    | <------> |  vault.db        |
|  (Autofill UI)    |  HTTP    |  127.0.0.1:8787      |          |  Encrypted vault |
+-------------------+  only    +----------------------+          +------------------+
                      localhost   WebView2 GUI + Server            %LOCALAPPDATA%\VaultBox\
                                  System tray icon
```

- **Desktop App** = Native C++ WebView2 app with embedded Bitwarden-styled vault manager and HTTP server
- **Extension** = Browser extension for autofill (rebranded Bitwarden, pointed at localhost)
- **Vault** = Encrypted SQLite file on disk, just like a KeePass .kdbx file

All encryption/decryption happens client-side. The server only stores already-encrypted data. This matches Bitwarden's zero-knowledge architecture.

## Key Features

- **Desktop Vault Manager** - WebView2-based GUI styled like Bitwarden's extension. Browse, search, edit, and organize your vault without a browser.
- **Web Vault** - Access your vault at `http://127.0.0.1:8787/` from any browser on your machine.
- **Single Encrypted File** - Your vault is one file (`vault.db`). Copy it, back it up, move it between PCs.
- **Password-Only Setup** - No email required. Set a master password and go.
- **Localhost Only** - Server binds to `127.0.0.1:8787`. No external network access. No DNS lookups.
- **Full Autofill** - Login, credit card, identity, and FIDO2 autofill works exactly like standard Bitwarden.
- **Password Generator** - Configurable length, character sets, copy-to-clipboard.
- **TOTP Codes** - Time-based one-time passwords generated locally.
- **Import/Export** - Bitwarden JSON/CSV, Chrome CSV, KeePass XML import. JSON/CSV export.
- **Folder Management** - Create, rename, delete folders with sidebar navigation.
- **System Tray** - Minimize or close to tray. Restore on double-click, quit from context menu.
- **Start at Login** - Optional auto-start via Settings toggle.
- **Installer** - Inno Setup installer with desktop shortcut and startup options.
- **No Telemetry** - Event collection, usage analytics, and crash reporting are completely removed.
- **No Prerequisites** - Single C++ exe, no Python/Java/runtime needed. WebView2 is included with Windows 10/11.

## How It Works

1. **Install** - Run the installer or drop `VaultBox-Server.exe` anywhere
2. **Server starts** - VaultBox opens with the desktop vault manager and sits in the system tray
3. **Create vault** - Set a master password
4. **Import passwords** - Import from Bitwarden, Chrome, KeePass, or add manually
5. **Install extension** - Load the browser extension for autofill support
6. **Backup** - Copy `%LOCALAPPDATA%\VaultBox\vault.db` to USB/NAS

## VaultBox vs KeePass vs Bitwarden

| Feature            | KeePass              | VaultBox              | Bitwarden         |
| ------------------ | -------------------- | --------------------- | ----------------- |
| Storage            | Local .kdbx file     | Local .db file        | Cloud             |
| Browser Autofill   | Via plugins (clunky) | Native (built-in)     | Native (built-in) |
| UI                 | Desktop-era          | Modern (Bitwarden UI) | Modern            |
| Email Required     | No                   | No                    | Yes               |
| Password Generator | Yes                  | Yes                   | Yes               |
| TOTP               | Via plugins          | Built-in              | Built-in (paid)   |
| Install Complexity | Download + plugins   | One-click installer   | Account + install |
| Multi-device Sync  | Manual file copy     | Manual file copy      | Automatic         |
| Internet Required  | No                   | No                    | Yes               |
| Open Source        | Yes                  | Yes                   | Yes               |

## Building from Source

### Prerequisites

- Visual Studio 2022+ with C++ workload (for the server)
- Node.js v22 + npm (for the browser extension)
- Inno Setup 6 (for the installer)

### Build the Server

```bash
cd server/cpp
build.bat
# Output: VaultBox-Server.exe
```

### Build the Installer

```bash
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" server/cpp/installer.iss
# Output: server/cpp/Output/VaultBox-Setup-0.5.0.exe
```

### Build the Extension

```bash
npm ci
cd apps/browser
npm run build:chrome    # Chrome/Edge/Brave
npm run build:firefox   # Firefox
```

### Load Extension in Chrome

1. Open `chrome://extensions/`
2. Enable "Developer mode" (top right)
3. Click "Load unpacked"
4. Select the `apps/browser/build/` directory

## Server API

The VaultBox server implements a Bitwarden-compatible API:

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

## Limitations

- **No Multi-Device Sync** - By design. Copy `vault.db` to transfer vaults between machines (like KeePass).
- **No Account Recovery** - No server to recover your account. Back up your vault file and remember your master password.
- **No Organization Features** - Organization sharing requires a cloud server. Personal vault only.
- **No Breach Reports** - HIBP password checking requires external network access.
- **No Emergency Access** - Requires cloud-side trusted contacts feature.

## License

This project is a fork of [Bitwarden](https://github.com/bitwarden/clients) and is subject to the Bitwarden License Agreement. VaultBox is not affiliated with Bitwarden Inc. See the original LICENSE files for details.
