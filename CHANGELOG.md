# Changelog

All notable changes to bitwarden-offline will be documented in this file.

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
