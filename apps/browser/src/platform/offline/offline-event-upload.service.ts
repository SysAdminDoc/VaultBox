/**
 * NoopEventUploadService - Discards all events instead of uploading to server.
 * Prevents any telemetry or usage data from being transmitted.
 */
import { EventUploadService as EventUploadServiceAbstraction } from "@bitwarden/common/abstractions/event/event-upload.service";
import { LogService } from "@bitwarden/common/platform/abstractions/log.service";

export class NoopEventUploadService implements EventUploadServiceAbstraction {
  constructor(private logService: LogService) {}

  init(): void {
    this.logService.info("[VaultBox Offline] Event upload service disabled (offline mode).");
  }

  /**
   * No-op: events are never uploaded.
   */
  async uploadEvents(): Promise<void> {
    // Silently discard - no telemetry in offline mode
    return;
  }
}
