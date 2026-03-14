export const BadgeIcon = {
  Berry: {
    19: "/images/berry19.png",
    38: "/images/berry38.png",
  },
  LoggedOut: {
    16: "/images/icon16_gray.png",
    19: "/images/icon19_gray.png",
    32: "/images/icon32_gray.png",
    38: "/images/icon38_gray.png",
    48: "/images/icon48_gray.png",
    96: "/images/icon96_gray.png",
    128: "/images/icon128_gray.png",
  } as IconPaths,
  Locked: {
    19: "/images/icon19_locked.png",
    38: "/images/icon38_locked.png",
  } as IconPaths,
  Unlocked: {
    16: "/images/icon16.png",
    19: "/images/icon19.png",
    32: "/images/icon32.png",
    38: "/images/icon38.png",
    48: "/images/icon48.png",
    96: "/images/icon96.png",
    128: "/images/icon128.png",
  } as IconPaths,
} as const satisfies Record<string, IconPaths>;

export type BadgeIcon = (typeof BadgeIcon)[keyof typeof BadgeIcon];

export type IconPaths = {
  [key: number]: string;
};
