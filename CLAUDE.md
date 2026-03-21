# VaultBox

## Overview

Offline Bitwarden-compatible password manager. Single C++ exe with embedded HTTP server + WebView2 GUI.

## Tech Stack

- C++17, MSVC (Visual Studio 2026 v18)
- WebView2 (static linked via `WebView2LoaderStatic.lib`) for SPA GUI
- cpp-httplib for embedded HTTP server on `127.0.0.1:8787`
- SQLite for local vault database (Bitwarden-compatible schema)
- BCrypt API (Windows native) for vault crypto (PBKDF2, HKDF, AES-CBC, HMAC)
- Inno Setup for Windows installer

## Build

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -SkipAutomaticLocation; Set-Location 'c:\Users\--\repos\bitwarden-offline\server\cpp'; rc /nologo vaultbox.rc; cl /EHsc /O2 /std:c++17 /bigobj /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I'deps\webview2' vaultbox_server.cpp sqlite3.obj vaultbox.res 'deps\webview2\WebView2LoaderStatic.lib' /Fe:VaultBox-Server.exe /nologo /link ws2_32.lib bcrypt.lib rpcrt4.lib shell32.lib user32.lib gdi32.lib advapi32.lib comctl32.lib dwmapi.lib uxtheme.lib comdlg32.lib ole32.lib"
```

## Installer Build

```
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
```

## Key Files

| File                             | Purpose                                                     |
| -------------------------------- | ----------------------------------------------------------- |
| `server/cpp/vaultbox_server.cpp` | main(), window creation, message loop                       |
| `server/cpp/vaultbox_server.h`   | Shared types, globals, utilities, startup registry          |
| `server/cpp/vaultbox_http.h`     | HTTP routes (Bitwarden-compat + VaultBox extensions)        |
| `server/cpp/vaultbox_crypto.h`   | Bitwarden crypto chain + TOTP encrypt/decrypt               |
| `server/cpp/vaultbox_db.h`       | SQLite query helpers                                        |
| `server/cpp/vaultbox_gui.h`      | WebView2 host, system tray, window procedure                |
| `server/cpp/vaultbox_ui.h`       | Embedded SPA HTML/CSS/JS (8 raw string chunks)              |
| `server/cpp/vaultbox_import.h`   | Import/export (Bitwarden JSON/CSV, Chrome CSV, KeePass XML) |
| `server/cpp/vaultbox_passgen.h`  | Password generator                                          |

## Gotchas

- **MSVC C2026**: Raw string literals max 16380 chars. SPA HTML split into 8 concatenated `R"VBHTML(...)VBHTML"` chunks.
- **WebView2 overflow bug**: `overflow:hidden` on `<html>` causes blank render. Only apply to `<body>`.
- **Window behavior**: X and minimize hide to tray. Quit only from tray menu or SPA quit command (`WM_VAULTBOX_QUIT`).
- **sqlite3.obj**: Pre-compiled, linked directly (not recompiled each build).
- **WebView2 SDK**: Headers + static lib in `deps/webview2/`.

## Current Version: v0.8.3

Features: auto-lock timer, clipboard auto-clear, password strength meter, TOTP display, dark/light theme, multi-vault, portable mode, fuzzy search, drag-and-drop import, auto-update check, WAL checkpoint, tray tooltip, security settings API, registry persistence.

## Browser Extension

Customized Bitwarden browser extension (Angular, MV3 Chrome / MV2 Firefox) with cloud features stripped:

### Build

```bash
cd apps/browser && npm run build
```

### Packaging

- **Chrome .zip**: `Compress-Archive -Path 'build/*' -DestinationPath 'VaultBox-chrome.zip'`
- **Chrome .crx**: `brave.exe --pack-extension=build --pack-extension-key=build.pem`
- **Firefox .xpi**: Copy .zip and rename to .xpi

### Key Customizations

- **OfflineApiService** replaces ApiService — blocks all network requests, handles cipher CRUD locally with synthetic responses
- **OfflineSyncService** replaces DefaultSyncService — sync is a local no-op that marks state as current
- **NoopEventUploadService** — blocks all telemetry/event upload
- Removed cloud features: sync, notifications, push, organizations, collections, 2FA management, device management, phishing detection, premium gates
- Removed all nudge/spotlight/onboarding cards (vault, generator, autofill, account security, admin settings)
- Removed intro carousel guard — goes straight to login
- Removed TOTP copy button (TOTP managed by server)
- Removed WebRequestBackground (import, field, permission, startListening call)
- Removed phishing detection initialization (PhishingDataService, PhishingDetectionService)
- Removed orphaned components (download-bitwarden, more-from-bitwarden)
- Removed premium badges from archive and attachments
- DuckDuckGo favicons (`icons.duckduckgo.com/ip3/{domain}.ico`) instead of Bitwarden icons server
- Default session timeout: Never (instead of OnRestart)
- Default filters collapsed, compact mode enabled, quick-copy mode enabled
- Rebranded about page → VaultBox + GitHub link
- Archive accessible without premium
- Toolbar icons regenerated with reduced padding (lock fills full canvas)
- Locked/gray icon variants match VaultBox style (desaturated + dimmed)

### Offline Services (`src/platform/offline/`)

| File                              | Purpose                                                                        |
| --------------------------------- | ------------------------------------------------------------------------------ |
| `offline-api.service.ts`          | Extends ApiService, blocks `send()`, returns synthetic CipherResponse for CRUD |
| `offline-sync.service.ts`         | Implements SyncService, fullSync is no-op that updates lastSync date           |
| `offline-event-upload.service.ts` | NoopEventUploadService, discards all telemetry                                 |
| `offline-account.service.ts`      | Local vault config, vault file export/import helpers                           |

## Release Assets

- VaultBox-Server.exe (standalone)
- VaultBox-chrome.zip, VaultBox-chrome.crx, VaultBox-firefox.xpi (browser extensions)
