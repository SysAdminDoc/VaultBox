/**
 * OfflineSyncService - Replaces server-based sync with local-only operations.
 *
 * In offline mode, "sync" means loading vault data from the local state store.
 * There is no server to sync with. The vault is populated via file import
 * and all changes are persisted locally.
 */
import { firstValueFrom, map, Observable, of, switchMap } from "rxjs";

import { AccountService } from "@bitwarden/common/auth/abstractions/account.service";
import { AuthService } from "@bitwarden/common/auth/abstractions/auth.service";
import { AuthenticationStatus } from "@bitwarden/common/auth/enums/authentication-status";
import {
  SyncCipherNotification,
  SyncFolderNotification,
  SyncSendNotification,
} from "@bitwarden/common/models/response/notification.response";
import { LogService } from "@bitwarden/common/platform/abstractions/log.service";
import { MessageSender } from "@bitwarden/common/platform/messaging";
import { StateProvider, SYNC_DISK, UserKeyDefinition } from "@bitwarden/common/platform/state";
import { SyncOptions, SyncService } from "@bitwarden/common/platform/sync/sync.service";
import { UserId } from "@bitwarden/common/types/guid";

const LAST_SYNC_DATE = new UserKeyDefinition<Date>(SYNC_DISK, "lastSync", {
  deserializer: (d: any) => (d != null ? new Date(d) : null),
  clearOn: ["logout"],
});

/**
 * Offline-only sync service. No server communication.
 * "Sync" just means marking the local state as current.
 */
export class OfflineSyncService implements SyncService {
  syncInProgress = false;

  constructor(
    private accountService: AccountService,
    private authService: AuthService,
    private stateProvider: StateProvider,
    private logService: LogService,
    private messageSender: MessageSender,
  ) {}

  async getLastSync(): Promise<Date | null> {
    const userId = await firstValueFrom(this.accountService.activeAccount$.pipe(map((a) => a?.id)));
    if (userId == null) {
      return null;
    }
    return await firstValueFrom(this.lastSync$(userId));
  }

  lastSync$(userId: UserId): Observable<Date | null> {
    return this.stateProvider.getUser(userId, LAST_SYNC_DATE).state$;
  }

  activeUserLastSync$(): Observable<Date | null> {
    return this.accountService.activeAccount$.pipe(
      switchMap((a) => {
        if (a == null) {
          return of(null);
        }
        return this.lastSync$(a.id);
      }),
    );
  }

  async setLastSync(date: Date, userId: UserId): Promise<void> {
    await this.stateProvider.getUser(userId, LAST_SYNC_DATE).update(() => date);
  }

  /**
   * In offline mode, fullSync is a no-op that just updates the last sync date.
   * All vault data is already in local state (loaded via file import).
   *
   * Concurrent calls share the in-flight promise so the local sync date is
   * never written twice for a single trigger and downstream listeners only
   * see one `syncCompleted` message per logical sync.
   */
  private inFlight: Promise<boolean> | null = null;

  async fullSync(
    _forceSync: boolean,
    _allowThrowOnErrorOrOptions?: boolean | SyncOptions,
  ): Promise<boolean> {
    if (this.inFlight != null) {
      return this.inFlight;
    }
    this.syncInProgress = true;
    this.inFlight = (async () => {
      try {
        const userId = await firstValueFrom(
          this.accountService.activeAccount$.pipe(map((a) => a?.id)),
        );

        if (userId == null) {
          this.logService.info("[VaultBox Offline] No active user, skipping sync.");
          return false;
        }

        const authStatus = await firstValueFrom(this.authService.authStatusFor$(userId));
        if (authStatus === AuthenticationStatus.LoggedOut) {
          this.logService.info("[VaultBox Offline] User logged out, skipping sync.");
          return false;
        }

        // Mark as synced - vault data is already local
        await this.setLastSync(new Date(), userId);
        this.logService.info("[VaultBox Offline] Local sync completed (no server contact).");
        this.messageSender.send("syncCompleted", { successfully: true });
        return true;
      } catch (e) {
        this.logService.error("[VaultBox Offline] Sync error:", e);
        return false;
      } finally {
        this.syncInProgress = false;
        this.inFlight = null;
      }
    })();
    return this.inFlight;
  }

  // Notification-based sync methods are no-ops in offline mode
  async syncUpsertFolder(
    _notification: SyncFolderNotification,
    _isEdit: boolean,
    _userId: UserId,
  ): Promise<boolean> {
    return true;
  }

  async syncDeleteFolder(_notification: SyncFolderNotification, _userId: UserId): Promise<boolean> {
    return true;
  }

  async syncUpsertCipher(
    _notification: SyncCipherNotification,
    _isEdit: boolean,
    _userId: UserId,
  ): Promise<boolean> {
    return true;
  }

  async syncDeleteCipher(_notification: SyncFolderNotification, _userId: UserId): Promise<boolean> {
    return true;
  }

  async syncUpsertSend(_notification: SyncSendNotification, _isEdit: boolean): Promise<boolean> {
    return true;
  }

  async syncDeleteSend(_notification: SyncSendNotification): Promise<boolean> {
    return true;
  }
}
