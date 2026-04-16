import { CommonModule } from "@angular/common";
import { Component } from "@angular/core";
import { RouterModule } from "@angular/router";

import { JslibModule } from "@bitwarden/angular/jslib.module";
import {
  CenterPositionStrategy,
  DialogService,
  ItemModule,
  TypographyModule,
} from "@bitwarden/components";

import { BrowserApi } from "../../../../platform/browser/browser-api";
import { PopOutComponent } from "../../../../platform/popup/components/pop-out.component";
import { PopupHeaderComponent } from "../../../../platform/popup/layout/popup-header.component";
import { PopupPageComponent } from "../../../../platform/popup/layout/popup-page.component";
import { AboutDialogComponent } from "../about-dialog/about-dialog.component";

// FIXME(https://bitwarden.atlassian.net/browse/CL-764): Migrate to OnPush
// eslint-disable-next-line @angular-eslint/prefer-on-push-component-change-detection
@Component({
  templateUrl: "about-page-v2.component.html",
  imports: [
    CommonModule,
    JslibModule,
    RouterModule,
    PopupPageComponent,
    PopupHeaderComponent,
    PopOutComponent,
    ItemModule,
    TypographyModule,
  ],
})
export class AboutPageV2Component {
  constructor(private dialogService: DialogService) {}

  about() {
    this.dialogService.open(AboutDialogComponent, {
      positionStrategy: new CenterPositionStrategy(),
    });
  }

  async openGitHub() {
    await BrowserApi.createNewTab("https://github.com/SysAdminDoc/VaultBox");
  }
}
