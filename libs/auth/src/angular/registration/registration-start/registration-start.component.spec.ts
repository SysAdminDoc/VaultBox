import { ComponentFixture, TestBed } from "@angular/core/testing";
import { ActivatedRoute, Router } from "@angular/router";
import { mock } from "jest-mock-extended";

import { AccountApiService } from "@bitwarden/common/auth/abstractions/account-api.service";
import { I18nService } from "@bitwarden/common/platform/abstractions/i18n.service";
import { PlatformUtilsService } from "@bitwarden/common/platform/abstractions/platform-utils.service";
// eslint-disable-next-line no-restricted-imports
import { AnonLayoutWrapperDataService } from "@bitwarden/components";

import { LoginEmailService } from "../../../common";

import { RegistrationStartComponent } from "./registration-start.component";

describe("RegistrationStartComponent", () => {
  let fixture: ComponentFixture<RegistrationStartComponent>;
  let router: { navigate: jest.Mock };

  beforeEach(async () => {
    router = { navigate: jest.fn().mockResolvedValue(true) };

    await TestBed.configureTestingModule({
      imports: [RegistrationStartComponent],
      providers: [
        { provide: Router, useValue: router },
        { provide: ActivatedRoute, useValue: { queryParams: { subscribe: () => {} } } },
        { provide: PlatformUtilsService, useValue: { isSelfHost: () => false } },
        { provide: I18nService, useValue: { t: (key: string) => key } },
        { provide: AccountApiService, useValue: mock<AccountApiService>() },
        { provide: LoginEmailService, useValue: mock<LoginEmailService>() },
        {
          provide: AnonLayoutWrapperDataService,
          useValue: { setAnonLayoutWrapperData: jest.fn() },
        },
      ],
    }).compileComponents();

    fixture = TestBed.createComponent(RegistrationStartComponent);
    fixture.detectChanges();
    await fixture.whenStable();
    fixture.detectChanges();
  });

  it("shows a local-first redirect splash while routing to password setup", () => {
    expect(fixture.nativeElement.textContent).toContain("Preparing Your Local Vault…");
    expect(fixture.nativeElement.textContent).toContain(
      "VaultBox is opening password setup next so you can start with a master password right away.",
    );
  });

  it("redirects directly to finish signup with local placeholder values", () => {
    expect(router.navigate).toHaveBeenCalledWith(["/finish-signup"], {
      queryParams: { token: "vaultbox-local", email: "vault@localhost" },
    });
  });
});
