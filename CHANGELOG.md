# Changelog

All notable changes to bitwarden-offline will be documented in this file.

## [extension-v0.10.0] - 2026-04-25

Browser-extension hardening pass. C++ server stays at v0.9.0; only the
Chrome/Firefox extension version was bumped (manifest `0.8.4 → 0.10.0`).

### Reliability

- `VaultFileManager.exportToFile` no longer leaks blob URLs or fails silently when
  the File System Access API is partially supported. The fallback now (a) prefers
  `chrome.downloads.download`, (b) falls back to an in-DOM anchor that is
  appended/`.click()`/removed in a single tick (some Chromium versions ignore
  `.click()` on detached anchors), and (c) extends `URL.revokeObjectURL` to 60 s
  so slow disks don't race the cleanup.
- `VaultFileManager.importFromFile` always settles its Promise. The previous
  fallback used an unattached `<input type=file>` with no `cancel` listener, so
  callers could hang forever if the user dismissed the picker. The new
  implementation listens for both `change` and `cancel`, attaches the input
  briefly to the DOM, removes it on settle, and rejects oversized files
  (> 64 MiB) before reading them into memory.
- `VaultFileManager.parseAndValidate` no longer throws-and-catches for control
  flow; it now returns `null` with a single console.error for each distinct
  validation failure, rejects non-objects/arrays at the root, type-checks
  `version` as a finite number, and back-fills missing array buckets so
  consumers never read `undefined` for `ciphers/folders/...`.
- `VaultFileManager.quickSave` aborts the writable stream on failure so the
  underlying file is not left locked across renderer reloads.
- `VaultFileManager.supportsFileSystemAccess` now requires *both*
  `showOpenFilePicker` and `showSaveFilePicker` (Firefox exposes neither today;
  the previous check could be tricked by polyfills that only stub one).

### Security / correctness

- `buildVaultExport` no longer scans `chrome.storage.local` with naive
  `key.includes(userId)` matching. Bitwarden's StateProvider scopes per-user
  state with the exact prefix `user_<userId>_`; we now key off that prefix
  and dispatch on the bucket *segment* rather than substring contains. This
  prevents (a) cross-user contamination when a user-id substring happens to
  appear inside another user's key, (b) keys like `archived_ciphers_disk`
  being mis-classified as ciphers, and (c) `userKey = value as string` blindly
  casting object-shaped state into a string.
- `getOfflineConfig` and `setOfflineConfig` now check `chrome.runtime.lastError`
  on every storage callback (Chrome's required acknowledgment to suppress
  unchecked-error warnings), validate the shape of the returned config object
  before trusting it, and surface set failures as a rejected Promise instead
  of swallowing them.

### Concurrency

- `OfflineSyncService.fullSync` is now reentrancy-safe. Concurrent callers
  share a single in-flight Promise so the local `lastSync` date is written at
  most once per logical sync and downstream listeners see one
  `syncCompleted` message instead of one per caller. The `syncInProgress`
  flag is cleared in a `finally` so it can never get stuck `true` after an
  exception.

### Maintenance / hygiene

- Unused parameters in offline service shims renamed to `_*` to satisfy the
  no-unused-vars lint rule without disabling it.
- `OfflineApiService` documents that it is currently *not* wired into the DI
  graph (VaultBox uses the standard ApiService talking to 127.0.0.1:8787) so
  future contributors don't waste time tracing why production never enters
  these methods.

## [v0.9.0] - 2026-04-16

### Security

- Fix attribute-breakout XSS in the embedded SPA. `escAttr` now round-trips values through `JSON.stringify` + HTML escaping, and all inline `onclick` handlers use it for interpolated user data (entry ids, folder ids/names, backup paths, GitHub release URLs).
- Tighten CORS: `/api/vaultbox/*` privileged endpoints are now same-origin SPA only so that a malicious browser extension cannot read the decrypted vault or export while VaultBox is unlocked. Bitwarden-compatible endpoints continue to accept extension origins (they require Bearer auth).
- Add CSP, `X-Frame-Options: DENY`, `X-Content-Type-Options: nosniff`, `Referrer-Policy: no-referrer`, and `Cache-Control: no-store` to SPA and API responses.
- Unlock endpoint gains exponential backoff after 4 consecutive failures.
- Multi-vault switch validates the target path stays inside the data directory.
- `/accounts/kdf` rejects dangerously low PBKDF2 iteration counts.
- Clipboard auto-clear only overwrites if VaultBox is still the clipboard owner.

### Reliability

- Fail-fast port-conflict detection: `bind_to_port` runs synchronously on startup and shows an actionable `MessageBox` on conflict instead of a blank WebView2.
- Clean shutdown: server thread is joined (no more detached listener leaking past exit).
- Broad `g_vault.mtx` coverage around all vault mutations; separate `g_vault_switch_mtx` for multi-vault path swaps.
- Server-side auto-lock watchdog acts as a backstop when the JS timer is throttled.
- Refresh-token rotation (deletes the old token when issuing a new pair).
- `DB` constructor throws on SQLite open failure; startup error surfaced via `MessageBox`.

### Performance / Limits

- Added indexes: `idx_ciphers_user(_del)`, `idx_ciphers_folder`, `idx_folders_user`, `idx_tokens_user`.
- `PRAGMA synchronous=NORMAL`, `temp_store=MEMORY`.
- HTTP payload cap 64 MB.
- Log queue capped at 512 messages; in-DOM log buffer capped at 200 KB.
- Auto-backup debounced ~3 s so bulk imports don't spawn a thread per entry.
- Log polling paused when panel is hidden or tab is backgrounded.

### UX / Imports

- Encrypted Bitwarden JSON exports are detected and rejected with a clear error instead of importing zero items.
- KeePass XML entities (`&amp;`, `&lt;`, `&quot;`, numeric refs) are decoded on import.
- Update check uses real semver comparison (`0.10.0` now correctly compares newer than `0.9.0`).
- Drag-and-drop import filters on `dataTransfer.types === 'Files'`, ignores drops on the lock screen.
- Empty vaults still auto-lock (old JS check required at least one entry).
- Portable mode can be signalled with a `vaultbox.portable` marker file next to the exe.

### Round-2 additions

- Server auto-lock watchdog now also sees browser-extension traffic: `touch_activity()` is called from the pre-routing handler for every non-polling request, not just `/api/vaultbox/vault`.
- `set_error_handler` no longer masks 404s on `/api/vaultbox/*`: typos in SPA endpoints now surface as real errors instead of `200 {}`.
- Multi-line CSV support: `read_csv_record` collects physical lines until any unclosed `"` is balanced, so Bitwarden exports with multi-line notes round-trip correctly.
- Register endpoints gained input validation (email shape, hash length bounds, KDF-iteration lower bound) and use a shared `opt_int_str` helper for safe optional numeric binds.
- `VaultState::unlocked` is now `std::atomic<bool>` to fix the data race between `/api/vaultbox/status` / tray-tooltip readers and `clear()` writers.
- System-tray tooltip read of `g_vault.entries.size()` now holds `g_vault.mtx`.

### Round-3 additions

- **Lock on system suspend** (`WM_POWERBROADCAST` / `PBT_APMSUSPEND`) - keys never linger through a sleep-resume cycle.
- **Lock on workstation lock** (`WM_WTSSESSION_CHANGE` with `WTS_SESSION_LOCK` / `LOGOFF` / console-remote disconnect). Registered via `WTSRegisterSessionNotification` / unregistered on quit. Required linking `wtsapi32.lib` (added to `build.bat` and `.github/workflows/build.yml`).
- **Transactional bulk operations** using `BEGIN IMMEDIATE` / `COMMIT` / `ROLLBACK`:
  - `/ciphers/import` - ~100x faster for large Bitwarden imports (no fsync per row) and atomic on failure.
  - `/folders/{id}` DELETE (Bitwarden-compat) and `VBCrypto::delete_folder` - no more orphaned cipher->folder references on crash.
  - Account deletion (`/accounts/delete` / DELETE `/accounts`) - all four table wipes succeed or none do, followed by an in-memory `g_vault.clear()`.

### Round-4 additions

- **All four import paths are now transactional** (Bitwarden JSON, Bitwarden CSV, Chrome CSV, KeePass XML). Previously each imported entry opened its own SQLite connection, acquired the vault mutex, and fsync'd independently - a 1 000-entry KeePass import could take 10+ seconds. After: a single `BEGIN IMMEDIATE` / `COMMIT` pair wraps the whole import on one DB connection under one mutex acquisition. Typically 10-100x faster; atomic on failure.
- **Refactor**: `VBCrypto::save_entry`/`save_folder` now wrap thin `save_entry_tx(DB&, …)`/`save_folder_tx(DB&, …)` helpers that callers can use inside their own transactions. No behaviour change for one-off saves.
- **Exports lock the vault**: `export_bitwarden_json` and `export_csv` used to iterate `g_vault.entries` without holding `g_vault.mtx`, racing with concurrent saves/deletes. Now take the mutex for the duration of the iteration.
- `import_bitwarden_csv` folder deduplication is now map-based (`nameToId`) instead of repeatedly scanning `g_vault.folders`, which also removes the previous unlocked mutation of that vector during import.

### Versioning

- Unified all `v*` strings (`APP_VERSION`, `installer.iss`, `build.bat`, README badges, SPA "about", CHANGELOG) to v0.9.0.

## [v0.8.3] - %Y->- (HEAD -> main, origin/main, origin/HEAD)

- Replace Bitwarden shield icon with VaultBox logo on about page
- Revert to standard ApiService/DefaultSyncService for local server sync
- Changed: Bump browser extension version to v0.8.4
- Wire up offline services, enlarge toolbar icons, remove cloud dead code
- Fixed: Fix passkey port disconnection due to null serverConfig
- Enable passkey/FIDO2 support in browser extension
- Removed: Remove remaining nudge/spotlight cards from generator and admin settings
- Removed: Remove all nudge cards, welcome screen, collapse filters by default
- Default session timeout to Never for offline vault
- Strip cloud features, rebrand to VaultBox, use DuckDuckGo favicons
