export class InsecureUrlNotAllowedError extends Error {
  constructor(url?: string) {
    if (url === undefined) {
      super("Insecure URL not allowed. All URLs must use HTTPS.");
    } else {
      super(`Insecure URL not allowed: ${url}. All URLs must use HTTPS.`);
    }
  }
}

/**
 * VaultBox: HTTP loopback exception.
 *
 * Bitwarden's URL validators throw `InsecureUrlNotAllowedError` for any
 * non-`https://` request unless `platformUtilsService.isDev()` returns true.
 * VaultBox's local server binds to `127.0.0.1:8787` over plain HTTP by design
 * (loopback only, never reachable from the network, never speaks TLS), so
 * call sites use this helper to whitelist that one specific case while still
 * rejecting any other plain-HTTP URL.
 *
 * Allowed loopback hosts:
 *   - 127.0.0.1   (IPv4 loopback)
 *   - ::1         (IPv6 loopback, bracketed in URL form: `[::1]`)
 *   - localhost   (DNS-resolved loopback)
 *
 * Anything else over `http://` is still rejected — including private-range
 * IPs (10.x, 192.168.x, etc.) so the extension can't be coaxed into talking
 * to a LAN-hosted impostor.
 */
export function isLocalhostHttpUrl(url: string): boolean {
  if (typeof url !== "string" || url.length === 0) {
    return false;
  }
  // Anchor on the scheme + host start. The trailing group requires either a
  // port-separator, path-separator, or end-of-string so that e.g.
  // `http://127.0.0.1.evil.example/` does not match.
  return /^http:\/\/(127\.0\.0\.1|localhost|\[::1\])(:[0-9]+)?(\/|$)/i.test(url);
}
