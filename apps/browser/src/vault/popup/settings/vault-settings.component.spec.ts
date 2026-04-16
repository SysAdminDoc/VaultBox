import { ChangeDetectionStrategy, Component, DebugElement, input } from "@angular/core";
import { ComponentFixture, TestBed } from "@angular/core/testing";
import { By } from "@angular/platform-browser";
import { provideRouter } from "@angular/router";
import { mock } from "jest-mock-extended";
import { BehaviorSubject } from "rxjs";

import { NudgesService } from "@bitwarden/angular/vault";
import { AccountService } from "@bitwarden/common/auth/abstractions/account.service";
import { ConfigService } from "@bitwarden/common/platform/abstractions/config/config.service";
import { I18nService } from "@bitwarden/common/platform/abstractions/i18n.service";
import { CipherArchiveService } from "@bitwarden/common/vault/abstractions/cipher-archive.service";
import { SyncService } from "@bitwarden/common/vault/abstractions/sync/sync.service.abstraction";
import { DialogService, ToastService } from "@bitwarden/components";

import { PopOutComponent } from "../../../platform/popup/components/pop-out.component";
import { PopupHeaderComponent } from "../../../platform/popup/layout/popup-header.component";
import { PopupPageComponent } from "../../../platform/popup/layout/popup-page.component";

import { VaultSettingsComponent } from "./vault-settings.component";

@Component({
  selector: "popup-header",
  template: `<ng-content></ng-content>`,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
class MockPopupHeaderComponent {
  readonly pageTitle = input<string>();
  readonly showBackButton = input<boolean>();
}

@Component({
  selector: "popup-page",
  template: `<ng-content></ng-content>`,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
class MockPopupPageComponent {}

@Component({
  selector: "app-pop-out",
  template: ``,
  changeDetection: ChangeDetectionStrategy.OnPush,
})
class MockPopOutComponent {
  readonly show = input(true);
}

describe("VaultSettingsComponent", () => {
  let component: VaultSettingsComponent;
  let fixture: ComponentFixture<VaultSettingsComponent>;
  let mockCipherArchiveService: jest.Mocked<CipherArchiveService>;

  const mockActiveAccount$ = new BehaviorSubject<{ id: string }>({
    id: "user-id",
  });
  const mockHasArchiveFlagEnabled$ = new BehaviorSubject<boolean>(true);
  const mockShowNudgeBadge$ = new BehaviorSubject<boolean>(false);

  const queryByTestId = (testId: string): DebugElement | null => {
    return fixture.debugElement.query(By.css(`[data-test-id="${testId}"]`));
  };

  const setArchiveState = (flagEnabled = true) => {
    mockHasArchiveFlagEnabled$.next(flagEnabled);
    fixture.detectChanges();
  };

  beforeEach(async () => {
    // Reset BehaviorSubjects to initial values
    mockHasArchiveFlagEnabled$.next(true);
    mockShowNudgeBadge$.next(false);

    mockCipherArchiveService = mock<CipherArchiveService>({});
    mockCipherArchiveService.hasArchiveFlagEnabled$ = mockHasArchiveFlagEnabled$.asObservable();

    await TestBed.configureTestingModule({
      imports: [VaultSettingsComponent],
      providers: [
        provideRouter([
          { path: "archive", component: VaultSettingsComponent },
          { path: "premium", component: VaultSettingsComponent },
        ]),
        { provide: SyncService, useValue: mock<SyncService>() },
        { provide: ToastService, useValue: mock<ToastService>() },
        { provide: ConfigService, useValue: mock<ConfigService>() },
        { provide: DialogService, useValue: mock<DialogService>() },
        { provide: I18nService, useValue: { t: (key: string) => key } },
        { provide: CipherArchiveService, useValue: mockCipherArchiveService },
        {
          provide: NudgesService,
          useValue: { showNudgeBadge$: jest.fn().mockReturnValue(mockShowNudgeBadge$) },
        },
        {
          provide: AccountService,
          useValue: { activeAccount$: mockActiveAccount$ },
        },
      ],
    })
      .overrideComponent(VaultSettingsComponent, {
        remove: {
          imports: [PopupHeaderComponent, PopupPageComponent, PopOutComponent],
        },
        add: {
          imports: [MockPopupHeaderComponent, MockPopupPageComponent, MockPopOutComponent],
        },
      })
      .compileComponents();

    fixture = TestBed.createComponent(VaultSettingsComponent);
    component = fixture.componentInstance;
  });

  describe("archive visibility", () => {
    it("shows a direct archive link when the archive feature is enabled", () => {
      setArchiveState(true);

      const archiveLink = queryByTestId("archive-link");

      expect(archiveLink?.nativeElement.getAttribute("routerLink")).toBe("/archive");
    });

    it("hides archive link when feature flag is disabled", () => {
      setArchiveState(false);

      const archiveLink = queryByTestId("archive-link");

      expect(archiveLink).toBeNull();
      expect(component["showArchiveItem"]()).toBe(false);
    });

    it("does not render legacy premium archive gating controls", () => {
      setArchiveState(true);

      expect(queryByTestId("premium-archive-link")).toBeNull();
    });
  });
});
