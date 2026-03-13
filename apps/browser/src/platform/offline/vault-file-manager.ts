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

export class VaultFileManager {
  private fileHandle: FileSystemFileHandle | null = null;

  /**
   * Export vault data to a downloadable .vaultbox file.
   */
  async exportToFile(vaultData: VaultBoxFile): Promise<void> {
    const json = JSON.stringify(vaultData, null, 2);
    const blob = new Blob([json], { type: "application/json" });
    const filename = `vaultbox-${new Date().toISOString().slice(0, 10)}.vaultbox`;

    // Try File System Access API first (allows saving to external drives)
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
        await writable.write(blob);
        await writable.close();
        this.fileHandle = handle;
        return;
      } catch (e: any) {
        if (e.name === "AbortError") {
          return; // User cancelled
        }
        // Fall through to download API
      }
    }

    // Fallback: use chrome.downloads API
    const url = URL.createObjectURL(blob);
    try {
      if (typeof chrome !== "undefined" && chrome.downloads) {
        await chrome.downloads.download({
          url: url,
          filename: filename,
          saveAs: true,
        });
      } else {
        // Final fallback: create download link
        const a = document.createElement("a");
        a.href = url;
        a.download = filename;
        a.click();
      }
    } finally {
      setTimeout(() => URL.revokeObjectURL(url), 10000);
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
        const file = await handle.getFile();
        const text = await file.text();
        this.fileHandle = handle;
        return this.parseAndValidate(text);
      } catch (e: any) {
        if (e.name === "AbortError") {
          return null; // User cancelled
        }
        // Fall through to input element
      }
    }

    // Fallback: use file input element
    return new Promise((resolve) => {
      const input = document.createElement("input");
      input.type = "file";
      input.accept = ".vaultbox,.json";
      input.onchange = async () => {
        const file = input.files?.[0];
        if (!file) {
          resolve(null);
          return;
        }
        const text = await file.text();
        resolve(this.parseAndValidate(text));
      };
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
      const writable = await this.fileHandle.createWritable();
      await writable.write(json);
      await writable.close();
      return true;
    } catch {
      this.fileHandle = null;
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
    try {
      const data = JSON.parse(text);

      if (data.format !== "vaultbox-offline-vault") {
        throw new Error(
          "Invalid vault file format. Expected 'vaultbox-offline-vault' format identifier.",
        );
      }

      if (!data.version || data.version > 1) {
        throw new Error(`Unsupported vault file version: ${data.version}`);
      }

      if (!data.account || !data.encryptedData) {
        throw new Error("Vault file is missing required account or encryptedData sections.");
      }

      if (!data.encryptedData.userKey) {
        throw new Error("Vault file is missing the encrypted user key.");
      }

      return data as VaultBoxFile;
    } catch (e: any) {
      // eslint-disable-next-line no-console -- VaultBox: File validation error reporting
      console.error("[VaultBox] Failed to parse vault file:", e.message);
      return null;
    }
  }

  private supportsFileSystemAccess(): boolean {
    return typeof globalThis !== "undefined" && "showOpenFilePicker" in globalThis;
  }
}
