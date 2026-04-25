# VaultBox Browser Extension — Unpacked Build

**This is a pre-built, drop-in Chrome / Edge / Brave extension. Load it
straight from this folder; no Node, no npm, no compile step.**

Current version: **v0.10.0** (matches the desktop server in the latest
GitHub Release).

## Install (Chrome / Edge / Brave / Vivaldi / Opera)

1. Clone this repo, or download it as a ZIP from
   `https://github.com/SysAdminDoc/VaultBox/archive/refs/heads/main.zip`
   and extract it somewhere permanent (don't use a temp folder — Chrome
   needs the path to stay valid).
2. Open `chrome://extensions/` (or `edge://extensions/`, `brave://extensions/`).
3. Toggle **Developer mode** on (top-right).
4. Click **Load unpacked** and pick **this `extension-chrome/`
   directory** (not the repo root).
5. Make sure **VaultBox Desktop** is running and unlocked.
6. Click the VaultBox toolbar icon and log in with the same email +
   master password you use for the desktop app.

That's it. Chrome will assign a stable extension id based on this
folder's path, so as long as you don't move/delete the folder, the
extension and its stored state stick around across browser restarts.

## Why a folder and not a `.crx`?

Since 2018, Chrome blocks any drag-installed CRX that wasn't signed by
the Web Store with the error `Package is invalid: CRX_REQUIRED_PROOF_MISSING`.
The signed `VaultBox-chrome.crx` in the GitHub Release works on
Brave/Vivaldi/Opera and on enterprise-managed Chrome (via
`ExtensionInstallForcelist` policy), but on stock Chrome the only
non-Web-Store install path is **Load unpacked**, which needs an
unzipped folder. This is that folder.

## Updating

When a new VaultBox release ships:

- Pull this repo (`git pull`) or download a newer ZIP.
- Open `chrome://extensions/` and click the circular reload icon on the
  VaultBox card. The new build under this folder takes effect immediately.

## Verifying you got the right build

```
$ grep -E '"version"' manifest.json
  "version": "0.10.0",
```

If `manifest.json` here doesn't say `0.10.0`, you've got an older
checkout — `git pull` and reload the extension.

## Building from source instead

If you want to build fresh from the TypeScript sources rather than use
this pre-built copy:

```bash
cd ../        # repo root
npm ci
cd apps/browser
npm run build:prod:chrome
# load apps/browser/build/ as the unpacked extension instead
```

That produces an identical bundle to what's in this folder.
