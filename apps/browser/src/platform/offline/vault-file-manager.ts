/**
 * VaultFileManager - Handles import/export of vault data to/from local files.
 *
 * Supports:
 * - Exporting the entire encrypted vault to a .vaultbox file (JSON)
 * - Importing a .vaultbox file to populate the extension's local state
 * - Using the File System Access API for persistent file handles (Chrome 86+)
 * - Fallback to download/upload for browsers without FSA API
 *
 * File format (.vaultbox):
 * {
 *   "format": "vaultbox-offline-vault",
 *   "version": 1,
 *   "exportDate": "ISO-8601",
 *   "account": {
 *     "email": "user@example.com",
 *     "userId": "...",
 *     "kdfType": 1,
 *     "kdfIterations": 600000,
 *     "kdfMemory": 64,
 *     "kdfParallelism": 4
 *   },
 *   "encryptedData": {
 *     "userKey": "...",        // Master-key-encrypted user symmetric key
 *     "privateKey": "...",     // User-key-encrypted private key
 *     "ciphers": [...],        // Array of encrypted cipher data objects
 *     "folders": [...],        // Array of encrypted folder data objects
 *     "collections": [...],
 *     "sends": [...],
 *     "policies": [...]
 *   }
 * }
 */

export interface VaultBoxFile {
  format: "vaultbox-offline-vault";
  version: number;
  exportDate: string;
  account: VaultBoxAccount;
  encryptedData: VaultBoxEncryptedData;
}

export interface VaultBoxAccount {
  email: string;
  userId: string;
  name?: string;
  kdfType: number;
  kdfIterations: number;
  kdfMemory?: number;
  kdfParallelism?: number;
  securityStamp?: string;
}

export interface VaultBoxEncryptedData {
  userKey: string; // Encrypted symmetric key (encrypted with master key)
  privateKey?: string; // Encrypted private key
  ciphers: any[];
  folders: any[];
  collections: any[];
  sends: any[];
  policies: any[];
}

// 64 MiB matches the server-side payload cap; anything larger is almost
// certainly garbage being fed into the importer.
const MAX_VAULT_FILE_BYTES = 64 * 1024 * 1024;

export class VaultFileManager {
  private fileHandle: FileSystemFileHandle | null = null;

  /**
   * Export vault data to a downloadable .vaultbox file.
   */
  async exportToFile(vaultData: VaultBoxFile): Promise<void> {
    const json = JSON.stringify(vaultData, null, 2);
    const blob = new Blob([json], { type: "application/json" });
    const filename = `vaultbox-${new Date().toISOString().slice(0, 10)}.vaultbox`;

    // Try File System Access API first (allows saving to external drives).
    // Note: chrome.downloads is preferred over FSA inside extension popups because
    // popup pages do not always have user-activation when this is invoked, but
    // FSA exposes write-anywhere semantics that the downloads API cannot.
    if (this.supportsFileSystemAccess()) {
      try {
        const handle = await (globalThis as any).showSaveFilePicker({
          suggestedName: filename,
          types: [
            {
              description: "VaultBox Vault File",
              accept: { "application/json": [".vaultbox"] },
            },
          ],
        });
        const writable = await handle.createWritable();
        try {
          await writable.write(blob);
        } finally {
          await writable.close();
        }
        this.fileHandle = handle;
        return;
      } catch (e: any) {
        if (e?.name === "AbortError" || e?.name === "NotAllowedError") {
          return; // User cancelled or no user activation — don't fall through to a second prompt
        }
        // Other failures fall through to the chrome.downloads / anchor fallback.
      }
    }

    // Fallback: use chrome.downloads API where available.
    if (typeof chrome !== "undefined" && chrome.downloads?.download) {
      const url = URL.createObjectURL(blob);
      try {
        await chrome.downloads.download({
          url: url,
          filename: filename,
          saveAs: true,
        });
      } finally {
        // Keep the URL alive until the browser has read the blob.
        setTimeout(() => URL.revokeObjectURL(url), 60_000);
      }
      return;
    }

    // Final fallback: anchor-based download. Must be appended to the DOM in
    // some Chromium versions for `.click()` to actually trigger a download.
    const url = URL.createObjectURL(blob);
    try {
      const a = document.createElement("a");
      a.href = url;
      a.download = filename;
      a.rel = "noopener";
      a.style.display = "none";
      document.body.appendChild(a);
      try {
        a.click();
      } finally {
        a.remove();
      }
    } finally {
      setTimeout(() => URL.revokeObjectURL(url), 60_000);
    }
  }

  /**
   * Import vault data from a .vaultbox file.
   * Returns the parsed vault data for the caller to process.
   */
  async importFromFile(): Promise<VaultBoxFile | null> {
    // Try File System Access API first
    if (this.supportsFileSystemAccess()) {
      try {
        const [handle] = await (globalThis as any).showOpenFilePicker({
          types: [
            {
              description: "VaultBox Vault File",
              accept: { "application/json": [".vaultbox", ".json"] },
            },
          ],
          multiple: false,
        });
        if (!handle) {
          return null;
        }
        const file = await handle.getFile();
        // Reject obviously oversized files before reading them into memory.
        if (file.size > MAX_VAULT_FILE_BYTES) {
          // eslint-disable-next-line no-console -- VaultBox: surface bad import to dev console
          console.error(
            `[VaultBox] Vault file rejected: ${file.size} bytes exceeds ${MAX_VAULT_FILE_BYTES} byte limit.`,
          );
          return null;
        }
        const text = await file.text();
        this.fileHandle = handle;
        return this.parseAndValidate(text);
      } catch (e: any) {
        if (e?.name === "AbortError" || e?.name === "NotAllowedError") {
          return null; // User cancelled or no user activation
        }
        // Fall through to input element
      }
    }

    // Fallback: file input element. Some browsers do not fire `change` on
    // detached inputs that were never activated by user gesture, so we attach
    // the input to the DOM, listen for both `change` and `cancel`, and
    // guarantee resolution so callers never hang.
    return new Promise((resolve) => {
      const input = document.createElement("input");
      input.type = "file";
      input.accept = ".vaultbox,.json,application/json";
      input.style.display = "none";

      let settled = false;
      const settle = (value: VaultBoxFile | null) => {
        if (settled) {
          return;
        }
        settled = true;
        input.remove();
        resolve(value);
      };

      input.addEventListener("change", async () => {
        const file = input.files?.[0];
        if (!file) {
          settle(null);
          return;
        }
        if (file.size > MAX_VAULT_FILE_BYTES) {
          // eslint-disable-next-line no-console -- VaultBox: surface bad import to dev console
          console.error(
            `[VaultBox] Vault file rejected: ${file.size} bytes exceeds ${MAX_VAULT_FILE_BYTES} byte limit.`,
          );
          settle(null);
          return;
        }
        try {
          const text = await file.text();
          settle(this.parseAndValidate(text));
        } catch (e) {
          // eslint-disable-next-line no-console -- VaultBox: surface bad import to dev console
          console.error("[VaultBox] Vault file read failed:", e);
          settle(null);
        }
      });
      // Browsers expose `cancel` on file inputs (Chromium 113+, Firefox 91+).
      input.addEventListener("cancel", () => settle(null));

      document.body.appendChild(input);
      input.click();
    });
  }

  /**
   * Quick-save to the last used file handle (if File System Access API available).
   * Returns false if no handle is available.
   */
  async quickSave(vaultData: VaultBoxFile): Promise<boolean> {
    if (!this.fileHandle) {
      return false;
    }

    let writable: FileSystemWritableFileStream | null = null;
    try {
      // Verify we still have permission
      const permission = await (this.fileHandle as any).queryPermission({ mode: "readwrite" });
      if (permission !== "granted") {
        const requested = await (this.fileHandle as any).requestPermission({ mode: "readwrite" });
        if (requested !== "granted") {
          this.fileHandle = null;
          return false;
        }
      }

      const json = JSON.stringify(vaultData, null, 2);
      writable = await this.fileHandle.createWritable();
      await writable.write(json);
      await writable.close();
      writable = null;
      return true;
    } catch {
      this.fileHandle = null;
      // Best-effort: ensure the writable stream is released even on failure
      // so the underlying file does not stay locked across renderer reloads.
      if (writable) {
        try {
          await writable.abort();
        } catch {
          // already aborted / closed
        }
      }
      return false;
    }
  }

  /**
   * Import from raw JSON text (for programmatic use, e.g., from chrome.storage).
   */
  importFromText(text: string): VaultBoxFile | null {
    return this.parseAndValidate(text);
  }

  private parseAndValidate(text: string): VaultBoxFile | null {
    let data: any;
    try {
      data = JSON.parse(text);
    } catch (e: any) {
      // eslint-disable-next-line no-console -- VaultBox: File validation error reporting
      console.error("[VaultBox] Failed to parse vault file as JSON:", e?.message ?? e);
      return null;
    }

    if (!data || typeof data !== "object" || Array.isArray(data)) {
      // eslint-disable-next-line no-console -- VaultBox: File validation error reporting
      console.error("[VaultBox] Vault file must be a JSON object.");
      return null;
    }

    if (data.format !== "vaultbox-offline-vault") {
      // eslint-disable-next-line no-console -- VaultBox: File validation error reporting
      console.error(
        "[VaultBox] Invalid vault file format. Expected 'vaultbox-offline-vault' format identifier.",
      );
      return null;
    }

    if (typeof data.version !== "number" || !Number.isFinite(data.version) || data.version > 1) {
      // eslint-disable-next-line no-console -- VaultBox: File validation error reporting
      console.error(`[VaultBox] Unsupported vault file version: ${data.version}`);
      return null;
    }

    if (
      data.account == null ||
      typeof data.account !== "object" ||
      data.encryptedData == null ||
      typeof data.encryptedData !== "object"
    ) {
      // eslint-disable-next-line no-console -- VaultBox: File validation error reporting
      console.error("[VaultBox] Vault file is missing required account or encryptedData sections.");
      return null;
    }

    if (typeof data.encryptedData.userKey !== "string" || data.encryptedData.userKey.length === 0) {
      // eslint-disable-next-line no-console -- VaultBox: File validation error reporting
      console.error("[VaultBox] Vault file is missing the encrypted user key.");
      return null;
    }

    // Defensive defaults so consumers never read undefined arrays.
    for (const key of ["ciphers", "folders", "collections", "sends", "policies"] as const) {
      if (!Array.isArray(data.encryptedData[key])) {
        data.encryptedData[key] = [];
      }
    }

    return data as VaultBoxFile;
  }

  private supportsFileSystemAccess(): boolean {
    return (
      typeof globalThis !== "undefined" &&
      typeof (globalThis as any).showOpenFilePicker === "function" &&
      typeof (globalThis as any).showSaveFilePicker === "function"
    );
  }
}
