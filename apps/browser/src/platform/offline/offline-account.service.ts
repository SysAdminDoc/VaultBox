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
    if (typeof chrome === "undefined" || !chrome.storage?.local) {
      resolve(null);
      return;
    }
    chrome.storage.local.get("offlineVaultConfig", (result) => {
      // chrome.storage exposes errors via runtime.lastError — reading the
      // property is required to suppress the "unchecked lastError" warning.
      if (chrome.runtime?.lastError) {
        // eslint-disable-next-line no-console -- VaultBox: surface storage errors
        console.error("[VaultBox] getOfflineConfig failed:", chrome.runtime.lastError.message);
        resolve(null);
        return;
      }
      const cfg = result?.offlineVaultConfig;
      if (cfg && typeof cfg === "object" && cfg.isOfflineMode === true) {
        resolve(cfg as OfflineVaultConfig);
      } else {
        resolve(null);
      }
    });
  });
}

export async function setOfflineConfig(config: OfflineVaultConfig): Promise<void> {
  return new Promise((resolve, reject) => {
    if (typeof chrome === "undefined" || !chrome.storage?.local) {
      resolve();
      return;
    }
    chrome.storage.local.set({ offlineVaultConfig: config }, () => {
      if (chrome.runtime?.lastError) {
        reject(new Error(chrome.runtime.lastError.message ?? "chrome.storage.local.set failed"));
        return;
      }
      resolve();
    });
  });
}

/**
 * Build a VaultBoxFile from the current extension state.
 * This reads encrypted data directly from chrome.storage.local.
 *
 * Implementation notes
 * --------------------
 * The Bitwarden state-provider scopes per-user keys with the prefix
 * `user_<userId>_<bucket>_<location>` (e.g. `user_<id>_ciphers_disk`). We
 * key off that exact shape rather than substring-matching on the user id,
 * because user ids are GUIDs and a substring scan is fragile (consider an
 * id that happens to occur inside another user's key, or any key that
 * coincidentally contains the substring "ciphers").
 *
 * Buckets we don't recognise are simply ignored — we do not want to leak
 * extension state we don't understand into the export envelope.
 */
export async function buildVaultExport(
  userId: string,
  email: string,
): Promise<VaultBoxFile | null> {
  if (!userId || typeof userId !== "string") {
    return null;
  }

  return new Promise((resolve) => {
    if (typeof chrome === "undefined" || !chrome.storage?.local) {
      resolve(null);
      return;
    }

    chrome.storage.local.get(null, (allData) => {
      const userPrefix = `user_${userId}_`;
      const ciphers: any[] = [];
      const folders: any[] = [];
      const collections: any[] = [];
      const sends: any[] = [];
      const policies: any[] = [];
      let userKey = "";
      let privateKey = "";

      // Walk through values that belong to this user only.
      const collectArray = (out: any[], value: unknown): void => {
        if (value == null) {
          return;
        }
        if (Array.isArray(value)) {
          for (const item of value) {
            if (item != null) {
              out.push(item);
            }
          }
          return;
        }
        if (typeof value === "object") {
          for (const item of Object.values(value as Record<string, unknown>)) {
            if (item != null) {
              out.push(item);
            }
          }
        }
      };

      const stringValue = (value: unknown): string => {
        if (typeof value === "string") {
          return value;
        }
        // Some Bitwarden state buckets store wrapped objects ({ keyB64, ... });
        // serialise them so they round-trip through the export envelope without
        // losing structure. Consumers can still re-parse the JSON if needed.
        if (value != null && typeof value === "object") {
          try {
            return JSON.stringify(value);
          } catch {
            return "";
          }
        }
        return "";
      };

      for (const [key, value] of Object.entries(allData)) {
        if (!key.startsWith(userPrefix)) {
          continue;
        }
        const tail = key.slice(userPrefix.length);
        // tail is e.g. "ciphers_disk", "folders_disk", "masterKeyEncryptedUserKey_memory"
        const bucket = tail.split("_", 1)[0]; // first underscore-delimited segment

        switch (bucket) {
          case "ciphers":
            collectArray(ciphers, value);
            break;
          case "folders":
            collectArray(folders, value);
            break;
          case "collections":
            collectArray(collections, value);
            break;
          case "sends":
            collectArray(sends, value);
            break;
          case "policies":
            collectArray(policies, value);
            break;
          default:
            if (
              tail.startsWith("masterKeyEncryptedUserKey") ||
              tail.startsWith("userSymKey") ||
              tail.startsWith("userKey")
            ) {
              if (!userKey) {
                userKey = stringValue(value);
              }
            } else if (tail.startsWith("privateKey")) {
              if (!privateKey) {
                privateKey = stringValue(value);
              }
            }
            break;
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
