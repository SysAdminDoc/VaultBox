# VaultBox - Fully Offline Password Manager

A fork of Bitwarden's browser extension modified to work **100% offline**. Your passwords never leave your device. No servers, no telemetry, no phone-home.

## Quick Install (Windows)

```powershell
irm https://github.com/SysAdminDoc/VaultBox/releases/latest/download/Install-VaultBox.ps1 -OutFile Install-VaultBox.ps1; .\Install-VaultBox.ps1
```

Or download manually from the [latest release](https://github.com/SysAdminDoc/VaultBox/releases/latest):

| Asset                          | Description                                       |
| ------------------------------ | ------------------------------------------------- |
| `VaultBox-v0.1.0-chrome.zip`   | Chrome extension (load unpacked)                  |
| `VaultBox-v0.1.0-firefox.zip`  | Firefox extension (load as temporary add-on)      |
| `Install-VaultBox.ps1`         | Windows installer (auto-elevates, creates shortcuts) |

## What This Is

VaultBox is a browser extension that provides the full Bitwarden password management experience (autofill, search, password generation, TOTP) while ensuring **zero network communication**. Your vault is stored locally and can be exported to local or external drives for backup and portability.

## Key Features

- **Zero Network Traffic** - All server communication is blocked at the API layer. No DNS lookups, no HTTP requests, no WebSocket connections to any Bitwarden server or third party.
- **Local Vault Storage** - Vault data lives in the browser's local storage (encrypted). Export/import `.vaultbox` files to USB drives, NAS, or any local storage.
- **Full Autofill** - Login, credit card, identity, and FIDO2 autofill works exactly like standard Bitwarden.
- **Password Generator** - Fully client-side password and passphrase generation.
- **TOTP Codes** - Time-based one-time passwords generated locally.
- **No Telemetry** - Event collection, usage analytics, and crash reporting are completely removed.
- **No External URLs** - Install page, help links, and marketing redirects are all disabled.

## Architecture Changes from Bitwarden

| Component            | Bitwarden                                                        | VaultBox                                                          |
| -------------------- | ---------------------------------------------------------------- | ----------------------------------------------------------------- |
| API Service          | HTTP requests to `api.bitwarden.com`                             | `OfflineApiService` - blocks all network, returns local responses |
| Sync Service         | Fetches vault from server                                        | `OfflineSyncService` - no-op, vault is already local              |
| Notifications        | SignalR WebSocket to server                                      | `NoopServerNotificationsService` - disabled                       |
| Event Upload         | Posts usage events to server                                     | `NoopEventUploadService` - events discarded                       |
| Auth                 | OAuth2 token exchange with identity server                       | Local master password validation only                             |
| Environment URLs     | `*.bitwarden.com` / `*.bitwarden.eu`                             | `localhost:0/blocked` (unreachable)                               |
| Manifest Permissions | `webRequest`, `webRequestAuthProvider`, broad `host_permissions` | Stripped to minimum needed for autofill                           |

## How It Works

1. **Install** the extension (load unpacked from `apps/browser/build/`)
2. **Import** your vault from a Bitwarden JSON export or `.vaultbox` file
3. **Use** the extension normally - autofill, search, generate passwords
4. **Export** your vault to a `.vaultbox` file for backup on local/external drives

## Building

### Prerequisites

- Node.js v22 (check `.nvmrc`)
- npm

### Build Steps

```bash
# Clone and enter the repo
git clone <this-repo> vaultbox
cd vaultbox

# Install dependencies
npm ci

# Build the Chrome extension
cd apps/browser
npm run build:chrome

# The built extension is in apps/browser/build/
```

### Load in Chrome

1. Open `chrome://extensions/`
2. Enable "Developer mode" (top right)
3. Click "Load unpacked"
4. Select the `apps/browser/build/` directory

### Build and Load in Firefox

```bash
cd apps/browser
npm run build:firefox
```

1. Open `about:debugging#/runtime/this-firefox`
2. Click "Load Temporary Add-on..."
3. Select any file inside the `apps/browser/build/` directory

## Vault File Format (.vaultbox)

The `.vaultbox` file is a JSON file containing your encrypted vault data. The encryption keys are derived from your master password using Argon2id (or PBKDF2), matching Bitwarden's standard encryption.

```json
{
  "format": "vaultbox-offline-vault",
  "version": 1,
  "exportDate": "2026-03-13T...",
  "account": {
    "email": "user@example.com",
    "userId": "...",
    "kdfType": 1,
    "kdfIterations": 3,
    "kdfMemory": 64,
    "kdfParallelism": 4
  },
  "encryptedData": {
    "userKey": "...",
    "privateKey": "...",
    "ciphers": [...],
    "folders": [...],
    "collections": [...],
    "sends": [...],
    "policies": [...]
  }
}
```

**Important:** The vault file contains encrypted data only. Without the master password, the data is unreadable. However, treat vault files as sensitive and store them securely.

## Security Model

- **Encryption**: AES-256-CBC with HMAC-SHA256 (same as Bitwarden)
- **Key Derivation**: Argon2id or PBKDF2-SHA256 (configurable)
- **Master Password**: Never stored in plaintext. Only the derived master key hash is used for validation.
- **Zero Trust Network**: The extension has no ability to make network requests. The `send()` method in the API service throws an error for any network call attempt.
- **No Tracking**: All event collection, analytics, and telemetry services are replaced with no-ops.

## Files Modified

### New Files (Offline Services)

- `apps/browser/src/platform/offline/offline-api.service.ts` - Blocks all network requests
- `apps/browser/src/platform/offline/offline-sync.service.ts` - Local-only sync
- `apps/browser/src/platform/offline/offline-event-upload.service.ts` - No-op telemetry
- `apps/browser/src/platform/offline/vault-file-manager.ts` - Import/export vault files
- `apps/browser/src/platform/offline/offline-account.service.ts` - Local account management
- `apps/browser/src/platform/offline/index.ts` - Barrel exports

### Modified Files

- `apps/browser/src/background/main.background.ts` - DI wiring swapped to offline services
- `apps/browser/src/background/runtime.background.ts` - Disabled install page redirect
- `apps/browser/src/manifest.v3.json` - Stripped permissions, rebranded
- `apps/browser/src/manifest.json` - Stripped permissions, rebranded (MV2)
- `apps/browser/src/_locales/en/messages.json` - Rebranded strings
- `libs/common/src/platform/services/default-environment.service.ts` - Server URLs replaced with localhost
- Various component files - External link navigations disabled

## Limitations

- **No Multi-Device Sync** - This is by design. Use vault file export/import to transfer between devices.
- **No Account Recovery** - There is no server to recover your account. Back up your vault file and remember your master password.
- **No Organization Features** - Organization sharing requires a server. Personal vault only.
- **No Breach Reports** - HIBP password checking requires network access.
- **No Emergency Access** - Requires server-side trusted contacts feature.

## License

This project is a fork of [Bitwarden](https://github.com/bitwarden/clients) and is subject to the Bitwarden License Agreement. VaultBox is not affiliated with Bitwarden Inc. See the original LICENSE files for details.
