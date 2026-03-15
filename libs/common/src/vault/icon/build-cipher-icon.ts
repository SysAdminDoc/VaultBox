import { Utils } from "../../platform/misc/utils";
import { CipherType } from "../enums/cipher-type";
import { CipherViewLike, CipherViewLikeUtils } from "../utils/cipher-view-like-utils";

export interface CipherIconDetails {
  imageEnabled: boolean;
  image: string | null;
  /**
   * @deprecated Fallback to `icon` instead which will default to "bwi-globe" if no other icon is applicable.
   */
  fallbackImage: string;
  icon: string;
}

export function buildCipherIcon(
  iconsServerUrl: string | null,
  cipher: CipherViewLike,
  showFavicon: boolean,
): CipherIconDetails {
  let icon: string = "bwi-globe";
  let image: string | null = null;
  let fallbackImage = "";
  const cardIcons: Record<string, string> = {
    Visa: "card-visa",
    Mastercard: "card-mastercard",
    Amex: "card-amex",
    Discover: "card-discover",
    "Diners Club": "card-diners-club",
    JCB: "card-jcb",
    Maestro: "card-maestro",
    UnionPay: "card-union-pay",
    RuPay: "card-ru-pay",
  };

  // VaultBox: Always allow favicons via DuckDuckGo (no Bitwarden icons server needed)
  if (iconsServerUrl == null) {
    iconsServerUrl = "https://icons.duckduckgo.com/ip3";
  }

  const cipherType = CipherViewLikeUtils.getType(cipher);
  const uri = CipherViewLikeUtils.uri(cipher);
  const card = CipherViewLikeUtils.getCard(cipher);

  switch (cipherType) {
    case CipherType.Login:
      icon = "bwi-globe";

      if (uri) {
        let hostnameUri = uri;
        let isWebsite = false;

        if (hostnameUri.indexOf("androidapp://") === 0) {
          // @TODO Re-add once we have Android icon https://bitwarden.atlassian.net/browse/PM-29028
          // icon = "bwi-android";
          image = null;
        } else if (hostnameUri.indexOf("iosapp://") === 0) {
          // @TODO Re-add once we have iOS icon https://bitwarden.atlassian.net/browse/PM-29028
          // icon = "bwi-apple";
          image = null;
        } else if (
          showFavicon &&
          hostnameUri.indexOf("://") === -1 &&
          hostnameUri.indexOf(".") > -1
        ) {
          hostnameUri = `http://${hostnameUri}`;
          isWebsite = true;
        } else if (showFavicon) {
          isWebsite = hostnameUri.indexOf("http") === 0 && hostnameUri.indexOf(".") > -1;
        }

        if (isWebsite && (hostnameUri.endsWith(".onion") || hostnameUri.endsWith(".i2p"))) {
          image = null;
          fallbackImage = "images/bwi-globe.png";
          break;
        }

        if (showFavicon && isWebsite) {
          try {
            const hostname = Utils.getHostname(hostnameUri);
            // VaultBox: Use DuckDuckGo favicon service instead of Bitwarden icons server
            image = `https://icons.duckduckgo.com/ip3/${hostname}.ico`;
            fallbackImage = "images/bwi-globe.png";
          } catch {
            // Ignore error since the fallback icon will be shown if image is null.
          }
        }
      } else {
        image = null;
      }
      break;
    case CipherType.SecureNote:
      icon = "bwi-sticky-note";
      break;
    case CipherType.Card:
      icon = "bwi-credit-card";
      if (showFavicon && card?.brand && card.brand in cardIcons) {
        icon = `credit-card-icon ${cardIcons[card.brand]}`;
      }
      break;
    case CipherType.Identity:
      icon = "bwi-id-card";
      break;
    case CipherType.SshKey:
      icon = "bwi-key";
      break;
    default:
      break;
  }

  return {
    imageEnabled: showFavicon,
    image,
    fallbackImage,
    icon,
  };
}
