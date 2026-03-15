import { Injectable, NgModule } from "@angular/core";
import { ActivatedRouteSnapshot, RouteReuseStrategy, RouterModule, Routes } from "@angular/router";

import { AuthenticationTimeoutComponent } from "@bitwarden/angular/auth/components/authentication-timeout.component";
import { AuthRoute } from "@bitwarden/angular/auth/constants";
import { EnvironmentSelectorComponent } from "@bitwarden/angular/auth/environment-selector/environment-selector.component";
import { authGuard, lockGuard, redirectGuard, unauthGuardFn } from "@bitwarden/angular/auth/guards";
import { ChangePasswordComponent } from "@bitwarden/angular/auth/password-management/change-password";
import { SetInitialPasswordComponent } from "@bitwarden/angular/auth/password-management/set-initial-password/set-initial-password.component";
import {
  RegistrationUserAddIcon,
  TwoFactorTimeoutIcon,
  UserLockIcon,
  VaultIcon,
  LockIcon,
} from "@bitwarden/assets/svg";
import {
  LoginComponent,
  LoginSecondaryContentComponent,
  PasswordHintComponent,
  RegistrationFinishComponent,
  RegistrationStartComponent,
  RegistrationStartSecondaryComponent,
  RegistrationStartSecondaryComponentData,
  TwoFactorAuthComponent,
  TwoFactorAuthGuard,
} from "@bitwarden/auth/angular";
import { AnonLayoutWrapperData } from "@bitwarden/components";
import { LockComponent } from "@bitwarden/key-management-ui";

import { AccountSwitcherComponent } from "../auth/popup/account-switching/account-switcher.component";
import { AuthExtensionRoute } from "../auth/popup/constants/auth-extension-route.constant";
import { AccountSecurityComponent } from "../auth/popup/settings/account-security.component";
import { AutofillComponent } from "../autofill/popup/settings/autofill.component";
import { BlockedDomainsComponent } from "../autofill/popup/settings/blocked-domains.component";
import { ExcludedDomainsComponent } from "../autofill/popup/settings/excluded-domains.component";
import { NotificationsSettingsComponent } from "../autofill/popup/settings/notifications.component";
import BrowserPopupUtils from "../platform/browser/browser-popup-utils";
import { popupRouterCacheGuard } from "../platform/popup/view-cache/popup-router-cache.service";
import { RouteCacheOptions } from "../platform/services/popup-view-cache-background.service";
import { CredentialGeneratorHistoryComponent } from "../tools/popup/generator/credential-generator-history.component";
import { CredentialGeneratorComponent } from "../tools/popup/generator/credential-generator.component";
import { filePickerPopoutGuard } from "../tools/popup/guards/file-picker-popout.guard";
import { AboutPageV2Component } from "../tools/popup/settings/about-page/about-page-v2.component";
import { ExportBrowserV2Component } from "../tools/popup/settings/export/export-browser-v2.component";
import { ImportBrowserV2Component } from "../tools/popup/settings/import/import-browser-v2.component";
import { SettingsV2Component } from "../tools/popup/settings/settings-v2.component";
import { AddEditComponent } from "../vault/popup/components/vault/add-edit/add-edit.component";
import { AttachmentsComponent } from "../vault/popup/components/vault/attachments/attachments.component";
import { IntroCarouselComponent } from "../vault/popup/components/vault/intro-carousel/intro-carousel.component";
import { PasswordHistoryComponent } from "../vault/popup/components/vault/vault-password-history/vault-password-history.component";
import { VaultComponent } from "../vault/popup/components/vault/vault.component";
import { ViewComponent } from "../vault/popup/components/vault/view/view.component";
import { clearVaultStateGuard } from "../vault/popup/guards/clear-vault-state.guard";
import { IntroCarouselGuard } from "../vault/popup/guards/intro-carousel.guard";
import { AppearanceComponent } from "../vault/popup/settings/appearance.component";
import { ArchiveComponent } from "../vault/popup/settings/archive.component";
import { FoldersComponent } from "../vault/popup/settings/folders.component";
import { TrashComponent } from "../vault/popup/settings/trash.component";
import { VaultSettingsComponent } from "../vault/popup/settings/vault-settings.component";

import { RouteElevation } from "./app-routing.animations";
import {
  ExtensionAnonLayoutWrapperComponent,
  ExtensionAnonLayoutWrapperData,
} from "./components/extension-anon-layout-wrapper/extension-anon-layout-wrapper.component";
import { debounceNavigationGuard } from "./services/debounce-navigation.service";
import { TabsV2Component } from "./tabs-v2.component";

/**
 * Data properties acceptable for use in extension route objects
 */
export interface RouteDataProperties extends RouteCacheOptions {
  elevation: RouteElevation;

  /**
   * A boolean to indicate that the URL should not be saved in memory in the BrowserRouterService.
   */
  doNotSaveUrl?: boolean;
}

const unauthRouteOverrides = {
  homepage: () => {
    return BrowserPopupUtils.inPopout(window) ? "/tabs/vault" : "/tabs/current";
  },
};

const routes: Routes = [
  {
    path: "",
    pathMatch: "full",
    children: [], // Children lets us have an empty component.
    canActivate: [
      popupRouterCacheGuard,
      redirectGuard({ loggedIn: "/tabs/current", loggedOut: "/login", locked: "/lock" }),
    ],
  },
  {
    path: "home",
    redirectTo: "login",
    pathMatch: "full",
  },
  {
    path: "vault",
    redirectTo: "/tabs/vault",
    pathMatch: "full",
  },
  {
    path: "",
    component: ExtensionAnonLayoutWrapperComponent,
    children: [
      {
        path: AuthRoute.AuthenticationTimeout,
        canActivate: [unauthGuardFn(unauthRouteOverrides)],
        children: [
          {
            path: "",
            component: AuthenticationTimeoutComponent,
          },
        ],
        data: {
          pageTitle: {
            key: "authenticationTimeout",
          },
          pageIcon: TwoFactorTimeoutIcon,
          elevation: 1,
        } satisfies RouteDataProperties & AnonLayoutWrapperData,
      },
    ],
  },
  {
    path: "view-cipher",
    component: ViewComponent,
    canActivate: [authGuard],
    data: {
      // Above "trash"
      elevation: 3,
    } satisfies RouteDataProperties,
  },
  {
    path: "cipher-password-history",
    component: PasswordHistoryComponent,
    canActivate: [authGuard],
    data: { elevation: 4 } satisfies RouteDataProperties,
  },
  {
    path: "add-cipher",
    component: AddEditComponent,
    canActivate: [authGuard, debounceNavigationGuard()],
    data: { elevation: 1, resetRouterCacheOnTabChange: true } satisfies RouteDataProperties,
    runGuardsAndResolvers: "always",
  },
  {
    path: "edit-cipher",
    component: AddEditComponent,
    canActivate: [authGuard, debounceNavigationGuard()],
    data: {
      // Above "trash"
      elevation: 3,
      resetRouterCacheOnTabChange: true,
    } satisfies RouteDataProperties,
    runGuardsAndResolvers: "always",
  },
  {
    path: "attachments",
    component: AttachmentsComponent,
    canActivate: [authGuard, filePickerPopoutGuard()],
    data: { elevation: 4 } satisfies RouteDataProperties,
  },
  {
    path: "generator",
    component: CredentialGeneratorComponent,
    canActivate: [authGuard],
    data: { elevation: 0 } satisfies RouteDataProperties,
  },
  {
    path: "generator-history",
    component: CredentialGeneratorHistoryComponent,
    canActivate: [authGuard],
    data: { elevation: 1 } satisfies RouteDataProperties,
  },
  {
    path: "import",
    component: ImportBrowserV2Component,
    canActivate: [authGuard, filePickerPopoutGuard()],
    data: { elevation: 1 } satisfies RouteDataProperties,
  },
  {
    path: "export",
    component: ExportBrowserV2Component,
    canActivate: [authGuard],
    data: { elevation: 2 } satisfies RouteDataProperties,
  },
  {
    path: "autofill",
    component: AutofillComponent,
    canActivate: [authGuard],
    data: { elevation: 1 } satisfies RouteDataProperties,
  },
  {
    path: AuthExtensionRoute.AccountSecurity,
    component: AccountSecurityComponent,
    canActivate: [authGuard],
    data: { elevation: 1 } satisfies RouteDataProperties,
  },
  {
    path: "notifications",
    component: NotificationsSettingsComponent,
    canActivate: [authGuard],
    data: { elevation: 1 } satisfies RouteDataProperties,
  },
  {
    path: "vault-settings",
    component: VaultSettingsComponent,
    canActivate: [authGuard],
    data: { elevation: 1 } satisfies RouteDataProperties,
  },
  {
    path: "folders",
    component: FoldersComponent,
    canActivate: [authGuard],
    data: { elevation: 2 } satisfies RouteDataProperties,
  },
  {
    path: "blocked-domains",
    component: BlockedDomainsComponent,
    canActivate: [authGuard],
    data: { elevation: 2 } satisfies RouteDataProperties,
  },
  {
    path: "excluded-domains",
    component: ExcludedDomainsComponent,
    canActivate: [authGuard],
    data: { elevation: 2 } satisfies RouteDataProperties,
  },
  {
    path: "appearance",
    component: AppearanceComponent,
    canActivate: [authGuard],
    data: { elevation: 1 } satisfies RouteDataProperties,
  },
  {
    path: "clone-cipher",
    component: AddEditComponent,
    canActivate: [authGuard],
    data: { elevation: 1 } satisfies RouteDataProperties,
  },
  {
    path: "",
    component: ExtensionAnonLayoutWrapperComponent,
    children: [
      {
        path: AuthRoute.SignUp,
        canActivate: [unauthGuardFn()],
        data: {
          elevation: 1,
          pageIcon: RegistrationUserAddIcon,
          pageTitle: {
            key: "createAccount",
          },
          showBackButton: true,
        } satisfies RouteDataProperties & ExtensionAnonLayoutWrapperData,
        children: [
          {
            path: "",
            component: RegistrationStartComponent,
          },
          {
            path: "",
            component: RegistrationStartSecondaryComponent,
            outlet: "secondary",
            data: {
              loginRoute: `/${AuthRoute.Login}`,
            } satisfies RegistrationStartSecondaryComponentData,
          },
        ],
      },
      {
        path: AuthRoute.FinishSignUp,
        canActivate: [unauthGuardFn()],
        data: {
          pageIcon: LockIcon,
          elevation: 1,
          showBackButton: true,
        } satisfies RouteDataProperties & ExtensionAnonLayoutWrapperData,
        children: [
          {
            path: "",
            component: RegistrationFinishComponent,
          },
        ],
      },
      {
        path: AuthRoute.SetInitialPassword,
        canActivate: [authGuard],
        component: SetInitialPasswordComponent,
        data: {
          elevation: 1,
        } satisfies RouteDataProperties,
      },
      {
        path: AuthRoute.Login,
        canActivate: [unauthGuardFn(unauthRouteOverrides), IntroCarouselGuard],
        data: {
          pageIcon: VaultIcon,
          pageTitle: {
            key: "logInToBitwarden",
          },
          elevation: 1,
          showAcctSwitcher: false,
        } satisfies RouteDataProperties & ExtensionAnonLayoutWrapperData,
        children: [
          { path: "", component: LoginComponent },
          { path: "", component: LoginSecondaryContentComponent, outlet: "secondary" },
        ],
      },
      {
        path: AuthRoute.PasswordHint,
        canActivate: [unauthGuardFn(unauthRouteOverrides)],
        data: {
          pageTitle: {
            key: "requestPasswordHint",
          },
          pageSubtitle: {
            key: "enterYourAccountEmailAddressAndYourPasswordHintWillBeSentToYou",
          },
          pageIcon: UserLockIcon,
          showBackButton: true,
          elevation: 1,
        } satisfies RouteDataProperties & ExtensionAnonLayoutWrapperData,
        children: [
          { path: "", component: PasswordHintComponent },
          {
            path: "",
            component: EnvironmentSelectorComponent,
            outlet: "environment-selector",
          },
        ],
      },
      {
        path: "lock",
        canActivate: [lockGuard()],
        data: {
          pageIcon: LockIcon,
          pageTitle: {
            key: "yourVaultIsLockedV2",
          },
          showReadonlyHostname: true,
          showAcctSwitcher: true,
          elevation: 1,
          /**
           * This ensures that in a passkey flow the `/fido2?<queryParams>` URL does not get
           * overwritten in the `BrowserRouterService` by the `/lock` route. This way, after
           * unlocking, the user can be redirected back to the `/fido2?<queryParams>` URL.
           *
           * Also, this prevents a routing loop when using biometrics to unlock the vault in MV2 (Firefox),
           * locking up the browser (https://bitwarden.atlassian.net/browse/PM-16116). This involves the
           * `popup-router-cache.service` pushing the `lock` route to the history.
           */
          doNotSaveUrl: true,
        } satisfies ExtensionAnonLayoutWrapperData & RouteDataProperties,
        children: [
          {
            path: "",
            component: LockComponent,
          },
        ],
      },
      {
        path: AuthRoute.TwoFactor,
        canActivate: [unauthGuardFn(unauthRouteOverrides), TwoFactorAuthGuard],
        children: [
          {
            path: "",
            component: TwoFactorAuthComponent,
          },
        ],
        data: {
          elevation: 1,
          pageTitle: {
            key: "verifyYourIdentity",
          },
          showBackButton: true,
          // `TwoFactorAuthComponent` manually sets its icon based on the 2fa type
          pageIcon: null,
        } satisfies RouteDataProperties & ExtensionAnonLayoutWrapperData,
      },
      {
        path: AuthRoute.ChangePassword,
        data: {
          elevation: 1,
          hideFooter: true,
          pageIcon: LockIcon,
        } satisfies RouteDataProperties & ExtensionAnonLayoutWrapperData,
        children: [
          {
            path: "",
            component: ChangePasswordComponent,
          },
        ],
        canActivate: [authGuard],
      },
    ],
  },
  {
    path: "about",
    component: AboutPageV2Component,
    canActivate: [authGuard],
    data: { elevation: 1 } satisfies RouteDataProperties,
  },
  {
    path: "intro-carousel",
    component: ExtensionAnonLayoutWrapperComponent,
    canActivate: [],
    data: { elevation: 0, doNotSaveUrl: true } satisfies RouteDataProperties,
    children: [
      {
        path: "",
        component: IntroCarouselComponent,
        data: {
          pageIcon: null,
          hideFooter: true,
        } satisfies ExtensionAnonLayoutWrapperData,
      },
    ],
  },
  {
    path: "tabs",
    component: TabsV2Component,
    data: { elevation: 0 } satisfies RouteDataProperties,
    children: [
      {
        path: "",
        redirectTo: "/tabs/vault",
        pathMatch: "full",
      },
      {
        path: "current",
        redirectTo: "/tabs/vault",
      },
      {
        path: "vault",
        component: VaultComponent,
        canActivate: [authGuard],
        canDeactivate: [clearVaultStateGuard],
        data: { elevation: 0 } satisfies RouteDataProperties,
      },
      {
        path: "generator",
        component: CredentialGeneratorComponent,
        canActivate: [authGuard],
        data: { elevation: 0 } satisfies RouteDataProperties,
      },
      {
        path: "settings",
        component: SettingsV2Component,
        canActivate: [authGuard],
        data: { elevation: 0 } satisfies RouteDataProperties,
      },
    ],
  },
  {
    path: AuthExtensionRoute.AccountSwitcher,
    component: AccountSwitcherComponent,
    data: { elevation: 4, doNotSaveUrl: true } satisfies RouteDataProperties,
  },
  {
    path: "trash",
    component: TrashComponent,
    canActivate: [authGuard],
    data: { elevation: 2 } satisfies RouteDataProperties,
  },
  {
    path: "archive",
    component: ArchiveComponent,
    canActivate: [authGuard],
    data: { elevation: 2 } satisfies RouteDataProperties,
  },
];

@Injectable()
export class NoRouteReuseStrategy implements RouteReuseStrategy {
  shouldDetach(route: ActivatedRouteSnapshot) {
    return false;
  }

  // eslint-disable-next-line
  store(route: ActivatedRouteSnapshot, handle: {}) {
    /* Nothing */
  }

  shouldAttach(route: ActivatedRouteSnapshot) {
    return false;
  }

  retrieve(route: ActivatedRouteSnapshot): any {
    return null;
  }

  shouldReuseRoute(future: ActivatedRouteSnapshot, curr: ActivatedRouteSnapshot) {
    return false;
  }
}

@NgModule({
  imports: [
    RouterModule.forRoot(routes, {
      useHash: true,
      onSameUrlNavigation: "reload",
      /*enableTracing: true,*/
    }),
  ],
  exports: [RouterModule],
  providers: [{ provide: RouteReuseStrategy, useClass: NoRouteReuseStrategy }],
})
export class AppRoutingModule {}
