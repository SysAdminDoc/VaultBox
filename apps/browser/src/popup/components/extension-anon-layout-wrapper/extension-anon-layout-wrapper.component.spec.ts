import { ChangeDetectionStrategy, Component, input } from "@angular/core";
import { TestBed } from "@angular/core/testing";
import { By } from "@angular/platform-browser";
import { RouterTestingModule } from "@angular/router/testing";
import { of } from "rxjs";

import { I18nService } from "@bitwarden/common/platform/abstractions/i18n.service";
import { AnonLayoutComponent, AnonLayoutWrapperDataService } from "@bitwarden/components";

import { AccountSwitcherService } from "../../../auth/popup/account-switching/services/account-switcher.service";
import { PopOutComponent } from "../../../platform/popup/components/pop-out.component";
import { PopupHeaderComponent } from "../../../platform/popup/layout/popup-header.component";
import { PopupPageComponent } from "../../../platform/popup/layout/popup-page.component";

import { ExtensionAnonLayoutWrapperComponent } from "./extension-anon-layout-wrapper.component";

@Component({
  selector: "auth-anon-layout",
  template: `<ng-content></ng-content>`,
  standalone: true,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
class MockAnonLayoutComponent {
  readonly title = input<string>();
  readonly subtitle = input<string>();
  readonly icon = input<unknown>();
  readonly showReadonlyHostname = input<boolean>();
  readonly hideLogo = input<boolean>();
  readonly maxWidth = input<"md" | "3xl">();
  readonly hideFooter = input<boolean>();
  readonly hideCardWrapper = input<boolean>();
}

@Component({
  selector: "popup-page",
  template: `<ng-content></ng-content>`,
  standalone: true,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
class MockPopupPageComponent {
  readonly disablePadding = input<boolean>();
}

@Component({
  selector: "popup-header",
  template: `<ng-content></ng-content>`,
  standalone: true,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
class MockPopupHeaderComponent {
  readonly background = input<"default" | "alt">();
  readonly showBackButton = input<boolean>();
  readonly pageTitle = input<string>();
}

@Component({
  selector: "app-pop-out",
  template: ``,
  standalone: true,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
class MockPopOutComponent {}

describe("ExtensionAnonLayoutWrapperComponent", () => {
  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [ExtensionAnonLayoutWrapperComponent, RouterTestingModule],
      providers: [
        {
          provide: I18nService,
          useValue: { t: (key: string) => key },
        },
        {
          provide: AnonLayoutWrapperDataService,
          useValue: { anonLayoutWrapperData$: () => of({}) },
        },
        {
          provide: AccountSwitcherService,
          useValue: { availableAccounts$: of([]) },
        },
      ],
    })
      .overrideComponent(ExtensionAnonLayoutWrapperComponent, {
        remove: {
          imports: [AnonLayoutComponent, PopupPageComponent, PopupHeaderComponent, PopOutComponent],
        },
        add: {
          imports: [
            MockAnonLayoutComponent,
            MockPopupPageComponent,
            MockPopupHeaderComponent,
            MockPopOutComponent,
          ],
        },
      })
      .compileComponents();
  });

  it("shows the brand when showLogo is true", () => {
    const fixture = TestBed.createComponent(ExtensionAnonLayoutWrapperComponent);
    const component = fixture.componentInstance;
    component["showLogo"] = true;

    fixture.detectChanges();

    const anonLayout = fixture.debugElement.query(By.directive(MockAnonLayoutComponent))
      .componentInstance as MockAnonLayoutComponent;

    expect(anonLayout.hideLogo).toBe(false);
  });

  it("hides the brand only when showLogo is false", () => {
    const fixture = TestBed.createComponent(ExtensionAnonLayoutWrapperComponent);
    const component = fixture.componentInstance;
    component["showLogo"] = false;

    fixture.detectChanges();

    const anonLayout = fixture.debugElement.query(By.directive(MockAnonLayoutComponent))
      .componentInstance as MockAnonLayoutComponent;

    expect(anonLayout.hideLogo).toBe(true);
  });
});
