import { TestBed } from "@angular/core/testing";
import { By } from "@angular/platform-browser";
import { Router } from "@angular/router";
import { mock } from "jest-mock-extended";
import { of } from "rxjs";

import { LoginEmailServiceAbstraction } from "@bitwarden/auth/common";
import { ApiService } from "@bitwarden/common/abstractions/api.service";
import { ClientType } from "@bitwarden/common/enums";
import { I18nService } from "@bitwarden/common/platform/abstractions/i18n.service";
import { PlatformUtilsService } from "@bitwarden/common/platform/abstractions/platform-utils.service";
// eslint-disable-next-line no-restricted-imports
import { ToastService } from "@bitwarden/components";

import { PasswordHintComponent } from "./password-hint.component";

describe("PasswordHintComponent", () => {
  async function createComponent(clientType: ClientType) {
    const router = mock<Router>();
    router.navigate.mockResolvedValue(true);

    const loginEmailService = mock<LoginEmailServiceAbstraction>();
    loginEmailService.loginEmail$ = of("saved@example.com");
    loginEmailService.setLoginEmail.mockResolvedValue(undefined);

    const platformUtilsService = mock<PlatformUtilsService>();
    platformUtilsService.getClientType.mockReturnValue(clientType);

    await TestBed.configureTestingModule({
      imports: [PasswordHintComponent],
      providers: [
        { provide: ApiService, useValue: mock<ApiService>() },
        { provide: I18nService, useValue: { t: (key: string) => key } },
        { provide: LoginEmailServiceAbstraction, useValue: loginEmailService },
        { provide: PlatformUtilsService, useValue: platformUtilsService },
        { provide: ToastService, useValue: { showToast: jest.fn() } },
        { provide: Router, useValue: router },
      ],
    }).compileComponents();

    const fixture = TestBed.createComponent(PasswordHintComponent);
    fixture.detectChanges();
    await fixture.whenStable();
    fixture.detectChanges();

    return { fixture, router, loginEmailService };
  }

  it("renders local guidance instead of the email form in the browser popup", async () => {
    const { fixture } = await createComponent(ClientType.Browser);

    expect(fixture.nativeElement.textContent).toContain("Master Password Help Stays Local");
    expect(fixture.nativeElement.textContent).toContain(
      "This offline VaultBox build cannot email password hints from the extension.",
    );
    expect(fixture.debugElement.query(By.css('input[type="email"]'))).toBeNull();
  });

  it("keeps the email form for non-browser clients", async () => {
    const { fixture } = await createComponent(ClientType.Web);

    const input = fixture.debugElement.query(By.css('input[type="email"]'))
      .nativeElement as HTMLInputElement;

    expect(input.autocomplete).toBe("email");
    expect(input.value).toBe("saved@example.com");
  });

  it("returns directly to login in the browser without persisting an email", async () => {
    const { fixture, router, loginEmailService } = await createComponent(ClientType.Browser);

    await (fixture.componentInstance as any).cancel();

    expect(loginEmailService.setLoginEmail).not.toHaveBeenCalled();
    expect(router.navigate).toHaveBeenCalledWith(["login"]);
  });
});
