/**
 * OfflineAccountService - Manages local-only account creation and vault setup.
 *
 * This service creates a local user account without any server interaction.
 * It handles:
 * - Creating a new local vault with email + master password
 * - Importing an existing vault from a .vaultbox file
 * - Exporting the current vault to a .vaultbox file
 * - Local master password validation for unlock
 */
import { VaultBoxFile } from "./vault-file-manager";

/**
 * Message types for communicating offline vault operations
 * through the extension's message bus.
 */
export const OFFLINE_MESSAGES = {
  CREATE_LOCAL_VAULT: "offlineCreateLocalVault",
  IMPORT_VAULT_FILE: "offlineImportVaultFile",
  EXPORT_VAULT_FILE: "offlineExportVaultFile",
  VAULT_FILE_IMPORTED: "offlineVaultFileImported",
  VAULT_FILE_EXPORTED: "offlineVaultFileExported",
  VAULT_SETUP_COMPLETE: "offlineVaultSetupComplete",
} as const;

/**
 * Data required to create a new local vault.
 */
export interface CreateLocalVaultRequest {
  email: string;
  masterPassword: string;
}

/**
 * Configuration for the offline vault mode.
 * Stored in chrome.storage.local under the key "offlineVaultConfig".
 */
export interface OfflineVaultConfig {
  isOfflineMode: true;
  setupComplete: boolean;
  vaultFileLastSaved?: string; // ISO date of last export
  autoSaveEnabled: boolean;
}

/**
 * Get/set the offline vault configuration from chrome.storage.local.
 */
export async function getOfflineConfig(): Promise<OfflineVaultConfig | null> {
  return new Promise((resolve) => {
    if (typeof chrome !== "undefined" && chrome.storage?.local) {
      chrome.storage.local.get("offlineVaultConfig", (result) => {
        resolve(result.offlineVaultConfig ?? null);
      });
    } else {
      resolve(null);
    }
  });
}

export async function setOfflineConfig(config: OfflineVaultConfig): Promise<void> {
  return new Promise((resolve) => {
    if (typeof chrome !== "undefined" && chrome.storage?.local) {
      chrome.storage.local.set({ offlineVaultConfig: config }, () => resolve());
    } else {
      resolve();
    }
  });
}

/**
 * Build a VaultBoxFile from the current extension state.
 * This reads encrypted data directly from chrome.storage.local.
 */
export async function buildVaultExport(
  userId: string,
  email: string,
): Promise<VaultBoxFile | null> {
  return new Promise((resolve) => {
    if (typeof chrome === "undefined" || !chrome.storage?.local) {
      resolve(null);
      return;
    }

    // Read all storage to find user-specific encrypted data
    chrome.storage.local.get(null, (allData) => {
      // The extension stores encrypted data with user-scoped keys
      // Key patterns: user_{userId}_ciphers_disk, user_{userId}_folders_disk, etc.
      const ciphers: any[] = [];
      const folders: any[] = [];
      const collections: any[] = [];
      const sends: any[] = [];
      const policies: any[] = [];
      let userKey = "";
      let privateKey = "";

      for (const [key, value] of Object.entries(allData)) {
        if (key.includes(userId)) {
          if (key.includes("ciphers")) {
            if (value && typeof value === "object") {
              Object.values(value).forEach((c: any) => ciphers.push(c));
            }
          } else if (key.includes("folders")) {
            if (value && typeof value === "object") {
              Object.values(value).forEach((f: any) => folders.push(f));
            }
          } else if (key.includes("collections")) {
            if (value && typeof value === "object") {
              Object.values(value).forEach((c: any) => collections.push(c));
            }
          } else if (key.includes("sends")) {
            if (value && typeof value === "object") {
              Object.values(value).forEach((s: any) => sends.push(s));
            }
          } else if (key.includes("policies")) {
            if (value && typeof value === "object") {
              Object.values(value).forEach((p: any) => policies.push(p));
            }
          } else if (key.includes("masterKeyEncryptedUserKey") || key.includes("userSymKey")) {
            userKey = value as string;
          } else if (key.includes("privateKey")) {
            privateKey = value as string;
          }
        }
      }

      const vaultFile: VaultBoxFile = {
        format: "vaultbox-offline-vault",
        version: 1,
        exportDate: new Date().toISOString(),
        account: {
          email: email,
          userId: userId,
          kdfType: 1, // Argon2id
          kdfIterations: 3,
          kdfMemory: 64,
          kdfParallelism: 4,
        },
        encryptedData: {
          userKey,
          privateKey,
          ciphers,
          folders,
          collections,
          sends,
          policies,
        },
      };

      resolve(vaultFile);
    });
  });
}
