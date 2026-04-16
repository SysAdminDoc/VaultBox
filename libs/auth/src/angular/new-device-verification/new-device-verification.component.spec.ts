import { Location } from "@angular/common";
import { ComponentFixture, TestBed } from "@angular/core/testing";
import { By } from "@angular/platform-browser";
import { Router } from "@angular/router";
import { mock, MockProxy } from "jest-mock-extended";
import { of } from "rxjs";

import { LoginSuccessHandlerService } from "@bitwarden/auth/common";
import { ApiService } from "@bitwarden/common/abstractions/api.service";
import { AccountService } from "@bitwarden/common/auth/abstractions/account.service";
import { MasterPasswordServiceAbstraction } from "@bitwarden/common/key-management/master-password/abstractions/master-password.service.abstraction";
import { I18nService } from "@bitwarden/common/platform/abstractions/i18n.service";
import { LogService } from "@bitwarden/common/platform/abstractions/log.service";
// eslint-disable-next-line no-restricted-imports
import { ToastService } from "@bitwarden/components";

import { LoginStrategyServiceAbstraction } from "../../common/abstractions/login-strategy.service";

import { NewDeviceVerificationComponentService } from "./new-device-verification-component.service";
import { NewDeviceVerificationComponent } from "./new-device-verification.component";

describe("NewDeviceVerificationComponent", () => {
  let fixture: ComponentFixture<NewDeviceVerificationComponent>;
  let apiService: MockProxy<ApiService>;
  let loginStrategyService: MockProxy<LoginStrategyServiceAbstraction>;
  let toastService: { showToast: jest.Mock };

  beforeEach(async () => {
    apiService = mock<ApiService>();
    loginStrategyService = mock<LoginStrategyServiceAbstraction>();
    loginStrategyService.authenticationSessionTimeout$ = of(false);
    loginStrategyService.getEmail.mockResolvedValue("vault@localhost");
    loginStrategyService.getMasterPasswordHash.mockResolvedValue("hash");

    toastService = { showToast: jest.fn() };

    await TestBed.configureTestingModule({
      imports: [NewDeviceVerificationComponent],
      providers: [
        { provide: Router, useValue: mock<Router>() },
        { provide: ApiService, useValue: apiService },
        { provide: LoginStrategyServiceAbstraction, useValue: loginStrategyService },
        { provide: LogService, useValue: { error: jest.fn() } },
        { provide: I18nService, useValue: { t: (key: string) => key } },
        { provide: LoginSuccessHandlerService, useValue: { run: jest.fn() } },
        { provide: AccountService, useValue: { activeAccount$: of(null) } },
        {
          provide: MasterPasswordServiceAbstraction,
          useValue: { forceSetPasswordReason$: jest.fn().mockReturnValue(of(null)) },
        },
        { provide: Location, useValue: { back: jest.fn() } },
        {
          provide: NewDeviceVerificationComponentService,
          useValue: { showBackButton: () => true },
        },
        { provide: ToastService, useValue: toastService },
      ],
    }).compileComponents();

    fixture = TestBed.createComponent(NewDeviceVerificationComponent);
    fixture.detectChanges();
    await fixture.whenStable();
    fixture.detectChanges();
  });

  it("renders calmer device confirmation guidance and one-time-code autofill hints", () => {
    expect(fixture.nativeElement.textContent).toContain("Confirm This Device");
    expect(fixture.nativeElement.textContent).toContain(
      "Enter the latest verification code from your sign-in message to finish this session.",
    );

    const input = fixture.debugElement.query(By.css('input[formControlName="code"]'))
      .nativeElement as HTMLInputElement;
    expect(input.autocomplete).toBe("one-time-code");
    expect(input.inputMode).toBe("numeric");
  });

  it("shows success feedback when another verification code is requested", async () => {
    apiService.send.mockResolvedValue(undefined);

    await fixture.componentInstance.resendOTP();

    expect(apiService.send).toHaveBeenCalledWith(
      "POST",
      "/accounts/resend-new-device-otp",
      {
        email: "vault@localhost",
        masterPasswordHash: "hash",
      },
      false,
      false,
    );
    expect(toastService.showToast).toHaveBeenCalledWith(
      expect.objectContaining({
        variant: "success",
        message: "A new verification code is on the way.",
      }),
    );
  });
});
