import { CommonModule } from "@angular/common";
import { ChangeDetectionStrategy, Component } from "@angular/core";
import { RouterModule } from "@angular/router";
import { filter, firstValueFrom, Observable, shareReplay, switchMap } from "rxjs";

import { JslibModule } from "@bitwarden/angular/jslib.module";
import { NudgesService, NudgeType } from "@bitwarden/angular/vault";
import { Account, AccountService } from "@bitwarden/common/auth/abstractions/account.service";
import { UserId } from "@bitwarden/common/types/guid";
import { BadgeComponent, ItemModule, LinkModule, TypographyModule } from "@bitwarden/components";

import { CurrentAccountComponent } from "../../../auth/popup/account-switching/current-account.component";
import { PopOutComponent } from "../../../platform/popup/components/pop-out.component";
import { PopupHeaderComponent } from "../../../platform/popup/layout/popup-header.component";
import { PopupPageComponent } from "../../../platform/popup/layout/popup-page.component";

@Component({
  templateUrl: "settings-v2.component.html",
  imports: [
    CommonModule,
    JslibModule,
    RouterModule,
    PopupPageComponent,
    PopupHeaderComponent,
    PopOutComponent,
    ItemModule,
    CurrentAccountComponent,
    BadgeComponent,
    TypographyModule,
    LinkModule,
  ],
  changeDetection: ChangeDetectionStrategy.OnPush,
})
export class SettingsV2Component {
  readonly NudgeType = NudgeType;

  private readonly authenticatedAccount$: Observable<Account> =
    this.accountService.activeAccount$.pipe(
      filter((account): account is Account => account !== null),
      shareReplay({ bufferSize: 1, refCount: true }),
    );

  readonly showVaultBadge$: Observable<boolean> = this.authenticatedAccount$.pipe(
    switchMap((account) =>
      this.nudgesService.showNudgeBadge$(NudgeType.EmptyVaultNudge, account.id),
    ),
  );

  readonly showAutofillBadge$: Observable<boolean> = this.authenticatedAccount$.pipe(
    switchMap((account) => this.nudgesService.showNudgeBadge$(NudgeType.AutofillNudge, account.id)),
  );

  constructor(
    private readonly nudgesService: NudgesService,
    private readonly accountService: AccountService,
  ) {}

  async dismissBadge(type: NudgeType) {
    if (await firstValueFrom(this.showVaultBadge$)) {
      const account = await firstValueFrom(this.authenticatedAccount$);
      await this.nudgesService.dismissNudge(type, account.id as UserId, true);
    }
  }
}
