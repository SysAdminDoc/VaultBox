/**
 * OfflineApiService - Blocks ALL network requests and provides local-only responses.
 *
 * This service wraps the standard ApiService and overrides the `send()` method
 * to prevent any network communication. Key methods that need to "work" offline
 * (like cipher CRUD, sync, auth) return synthetic/local responses.
 *
 * NOTE: this class is currently NOT wired into the active DI graph — VaultBox
 * uses the standard ApiService talking to the local C++ server at
 * 127.0.0.1:8787. This file is kept for use by air-gapped builds that have no
 * companion server. Keep the network-block invariant intact even though no
 * production path exercises it today.
 */
import { Utils } from "@bitwarden/common/platform/misc/utils";
import { SyncResponse } from "@bitwarden/common/platform/sync";
import { ApiService } from "@bitwarden/common/services/api.service";
import { UserId } from "@bitwarden/common/types/guid";
import { CipherCreateRequest } from "@bitwarden/common/vault/models/request/cipher-create.request";
import { CipherPartialRequest } from "@bitwarden/common/vault/models/request/cipher-partial.request";
import { CipherRequest } from "@bitwarden/common/vault/models/request/cipher.request";
import { CipherResponse } from "@bitwarden/common/vault/models/response/cipher.response";

export class OfflineApiService extends ApiService {
  /**
   * Block ALL network requests. This is the single chokepoint that prevents
   * any data from leaving the browser.
   */
  override async send(
    method: "GET" | "POST" | "PUT" | "DELETE" | "PATCH",
    path: string,
    _body: any,
    _authedOrUserId: UserId | boolean,
    _hasResponse: boolean,
    _apiUrl?: string | null,
    _alterHeaders?: (headers: Headers) => void,
  ): Promise<any> {
    throw new Error(
      `[VaultBox Offline] Network request blocked: ${method} ${path}. ` +
        `This extension operates in offline-only mode and never contacts any server.`,
    );
  }

  /**
   * Block identity token requests - auth is handled locally.
   */
  override async postIdentityToken(): Promise<any> {
    throw new Error("[VaultBox Offline] Server authentication is disabled. Use local vault login.");
  }

  /**
   * No token refresh needed in offline mode.
   */
  override async refreshIdentityToken(): Promise<any> {
    // No-op: tokens are synthetic in offline mode
    return;
  }

  /**
   * Return a revision date of 0 so sync never thinks it needs to contact the server.
   */
  override async getAccountRevisionDate(): Promise<number> {
    return 0;
  }

  /**
   * Block sync - handled by OfflineSyncService instead.
   */
  override async getSync(): Promise<SyncResponse> {
    throw new Error("[VaultBox Offline] Server sync is disabled. Vault data is managed locally.");
  }

  // --- Cipher CRUD: return the request data back as a synthetic response ---
  // These methods are called by CipherService when saving/updating ciphers.
  // In offline mode, we generate a local ID and return the data as if the server accepted it.

  override async postCipher(request: CipherRequest): Promise<CipherResponse> {
    return this.createLocalCipherResponse(request);
  }

  override async postCipherCreate(request: CipherCreateRequest): Promise<CipherResponse> {
    return this.createLocalCipherResponse(request.cipher);
  }

  override async postCipherAdmin(request: CipherCreateRequest): Promise<CipherResponse> {
    return this.createLocalCipherResponse(request.cipher);
  }

  override async putCipher(id: string, request: CipherRequest): Promise<CipherResponse> {
    return this.createLocalCipherResponse(request, id);
  }

  override async putPartialCipher(
    id: string,
    request: CipherPartialRequest,
  ): Promise<CipherResponse> {
    // Return a minimal response for partial updates
    const response = new CipherResponse({
      Id: id,
      FolderId: request.folderId,
      Favorite: request.favorite,
      RevisionDate: new Date().toISOString(),
      Edit: true,
      ViewPassword: true,
    });
    return response;
  }

  override async putCipherAdmin(id: string, request: CipherRequest): Promise<CipherResponse> {
    return this.createLocalCipherResponse(request, id);
  }

  override async deleteCipher(_id: string): Promise<any> {
    return; // No-op, local state handles deletion
  }

  override async deleteCipherAdmin(_id: string): Promise<any> {
    return;
  }

  override async deleteManyCiphers(): Promise<any> {
    return;
  }

  override async deleteManyCiphersAdmin(): Promise<any> {
    return;
  }

  override async putMoveCiphers(): Promise<any> {
    return;
  }

  override async putDeleteCipher(): Promise<any> {
    return;
  }

  override async putDeleteCipherAdmin(): Promise<any> {
    return;
  }

  override async putDeleteManyCiphers(): Promise<any> {
    return;
  }

  override async putDeleteManyCiphersAdmin(): Promise<any> {
    return;
  }

  override async putRestoreCipher(id: string): Promise<CipherResponse> {
    return new CipherResponse({
      Id: id,
      RevisionDate: new Date().toISOString(),
      DeletedDate: null,
      Edit: true,
      ViewPassword: true,
    });
  }

  override async putRestoreCipherAdmin(id: string): Promise<CipherResponse> {
    return this.putRestoreCipher(id);
  }

  // Block event collection - no telemetry
  override async postEventsCollect(): Promise<any> {
    return; // No-op: events are never sent
  }

  // Block prelogin (server feature discovery)
  override async postPrelogin(): Promise<any> {
    throw new Error("[VaultBox Offline] Prelogin is disabled in offline mode.");
  }

  /**
   * Create a synthetic CipherResponse from a CipherRequest, as if the server accepted it.
   */
  private createLocalCipherResponse(request: CipherRequest, existingId?: string): CipherResponse {
    const id = existingId || Utils.newGuid();
    const now = new Date().toISOString();

    return new CipherResponse({
      Id: id,
      Type: request.type,
      Name: request.name,
      Notes: request.notes,
      Fields: request.fields,
      Login: request.login,
      SecureNote: request.secureNote,
      Card: request.card,
      Identity: request.identity,
      SshKey: (request as any).sshKey,
      Favorite: request.favorite,
      FolderId: request.folderId,
      OrganizationId: request.organizationId,
      Reprompt: request.reprompt,
      CreationDate: existingId ? undefined : now,
      RevisionDate: now,
      DeletedDate: null,
      Edit: true,
      ViewPassword: true,
      OrganizationUseTotp: false,
      Attachments: null,
      CollectionIds: null,
      Key: (request as any).key,
    });
  }
}
