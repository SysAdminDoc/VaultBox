import { ComponentFixture, TestBed } from "@angular/core/testing";
import { FormBuilder } from "@angular/forms";
import { By } from "@angular/platform-browser";
import { ActivatedRoute, provideRouter } from "@angular/router";
import { mock } from "jest-mock-extended";
import { of } from "rxjs";

import { LoginStrategyServiceAbstraction , LoginEmailServiceAbstraction , LoginSuccessHandlerService } from "@bitwarden/auth/common";
import { InternalPolicyService } from "@bitwarden/common/admin-console/abstractions/policy/policy.service.abstraction";
import { DevicesApiServiceAbstraction } from "@bitwarden/common/auth/abstractions/devices-api.service.abstraction";
import { SsoLoginServiceAbstraction } from "@bitwarden/common/auth/abstractions/sso-login.service.abstraction";
import { ClientType } from "@bitwarden/common/enums";
import { AppIdService } from "@bitwarden/common/platform/abstractions/app-id.service";
import { BroadcasterService } from "@bitwarden/common/platform/abstractions/broadcaster.service";
import { ConfigService } from "@bitwarden/common/platform/abstractions/config/config.service";
import { EnvironmentService } from "@bitwarden/common/platform/abstractions/environment.service";
import { I18nService } from "@bitwarden/common/platform/abstractions/i18n.service";
import { LogService } from "@bitwarden/common/platform/abstractions/log.service";
import { MessagingService } from "@bitwarden/common/platform/abstractions/messaging.service";
import { PlatformUtilsService } from "@bitwarden/common/platform/abstractions/platform-utils.service";
import { ValidationService } from "@bitwarden/common/platform/abstractions/validation.service";
import { PasswordStrengthServiceAbstraction } from "@bitwarden/common/tools/password-strength";
// eslint-disable-next-line no-restricted-imports
import { AnonLayoutWrapperDataService, ToastService } from "@bitwarden/components";

import { LoginComponentService } from "./login-component.service";
import { LoginComponent } from "./login.component";

describe("LoginComponent continue() integration", () => {
  function createComponent({ flagEnabled }: { flagEnabled: boolean }) {
    const activatedRoute: any = { queryParams: { subscribe: () => {} } };
    const anonLayoutWrapperDataService: any = { setAnonLayoutWrapperData: jest.fn() };
    const appIdService: any = {};
    const broadcasterService: any = { subscribe: () => {}, unsubscribe: () => {} };
    const destroyRef: any = {};
    const devicesApiService: any = {};
    const formBuilder = new FormBuilder();
    const i18nService: any = { t: () => "" };
    const loginEmailService: any = {
      rememberedEmail$: { pipe: () => ({}) },
      setLoginEmail: async () => {},
      setRememberedEmailChoice: async () => {},
      clearLoginEmail: async () => {},
    };
    const loginComponentService: any = {
      showBackButton: () => {},
      isLoginWithPasskeySupported: () => false,
      redirectToSsoLogin: async () => {},
    };
    const loginStrategyService = mock<LoginStrategyServiceAbstraction>();
    const messagingService: any = { send: () => {} };
    const ngZone: any = { isStable: true, onStable: { pipe: () => ({ subscribe: () => {} }) } };
    const passwordStrengthService: any = {};
    const platformUtilsService = mock<PlatformUtilsService>();
    platformUtilsService.getClientType.mockReturnValue(ClientType.Browser);
    const policyService: any = { replace: async () => {}, evaluateMasterPassword: () => true };
    const router: any = { navigate: async () => {}, navigateByUrl: async () => {} };
    const toastService: any = { showToast: () => {} };
    const logService: any = { error: () => {} };
    const validationService: any = { showError: () => {} };
    const loginSuccessHandlerService: any = { run: async () => {} };
    const configService = mock<ConfigService>();
    configService.getFeatureFlag.mockResolvedValue(flagEnabled);
    const ssoLoginService: any = { ssoRequiredCache$: { pipe: () => ({}) } };
    const environmentService: any = { environment$: { pipe: () => ({}) } };

    const component = new LoginComponent(
      activatedRoute,
      anonLayoutWrapperDataService,
      appIdService,
      broadcasterService,
      destroyRef,
      devicesApiService,
      formBuilder,
      i18nService,
      loginEmailService,
      loginComponentService,
      loginStrategyService,
      messagingService,
      ngZone,
      passwordStrengthService,
      platformUtilsService,
      policyService,
      router,
      toastService,
      logService,
      validationService,
      loginSuccessHandlerService,
      configService,
      ssoLoginService,
      environmentService,
    );

    jest.spyOn(component as any, "toggleLoginUiState").mockResolvedValue(undefined);

    return { component, loginStrategyService, anonLayoutWrapperDataService };
  }

  it("configures welcome-back wrapper copy for the offline vault flow", async () => {
    const { component, anonLayoutWrapperDataService } = createComponent({ flagEnabled: false });

    await (component as any).defaultOnInit();

    expect(anonLayoutWrapperDataService.setAnonLayoutWrapperData).toHaveBeenCalledWith(
      expect.objectContaining({
        pageTitle: { key: "welcomeBack" },
        pageSubtitle: { key: "loginOrCreateNewAccount" },
      }),
    );
  });

  it("calls getPasswordPrelogin on continue when flag enabled and email valid", async () => {
    const { component, loginStrategyService } = createComponent({ flagEnabled: true });
    (component as any).formGroup.controls.email.setValue("user@example.com");
    (component as any).formGroup.controls.rememberEmail.setValue(false);
    (component as any).formGroup.controls.masterPassword.setValue("irrelevant");

    await (component as any).continue();

    expect(loginStrategyService.getPasswordPrelogin).toHaveBeenCalledWith("user@example.com");
  });

  it("does not call getPasswordPrelogin when flag disabled", async () => {
    const { component, loginStrategyService } = createComponent({ flagEnabled: false });
    (component as any).formGroup.controls.email.setValue("user@example.com");
    (component as any).formGroup.controls.rememberEmail.setValue(false);
    (component as any).formGroup.controls.masterPassword.setValue("irrelevant");

    await (component as any).continue();

    expect(loginStrategyService.getPasswordPrelogin).not.toHaveBeenCalled();
  });
});

describe("LoginComponent rendering", () => {
  let fixture: ComponentFixture<LoginComponent>;

  beforeEach(async () => {
    const platformUtilsService = mock<PlatformUtilsService>();
    platformUtilsService.getClientType.mockReturnValue(ClientType.Browser);

    const configService = mock<ConfigService>();
    configService.getFeatureFlag.mockResolvedValue(false);

    await TestBed.configureTestingModule({
      imports: [LoginComponent],
      providers: [
        { provide: AppIdService, useValue: {} },
        { provide: ActivatedRoute, useValue: { queryParams: of({}) } },
        { provide: BroadcasterService, useValue: { subscribe: () => {}, unsubscribe: () => {} } },
        { provide: DevicesApiServiceAbstraction, useValue: {} },
        { provide: I18nService, useValue: { t: (key: string) => key } },
        {
          provide: LoginEmailServiceAbstraction,
          useValue: {
            rememberedEmail$: of(null),
            setLoginEmail: async () => {},
            setRememberedEmailChoice: async () => {},
            clearLoginEmail: async () => {},
          },
        },
        {
          provide: LoginComponentService,
          useValue: {
            showBackButton: () => {},
            isLoginWithPasskeySupported: () => false,
            redirectToSsoLogin: async () => {},
          },
        },
        {
          provide: LoginStrategyServiceAbstraction,
          useValue: mock<LoginStrategyServiceAbstraction>(),
        },
        { provide: MessagingService, useValue: { send: () => {} } },
        { provide: PasswordStrengthServiceAbstraction, useValue: {} },
        { provide: PlatformUtilsService, useValue: platformUtilsService },
        {
          provide: InternalPolicyService,
          useValue: { replace: async () => {}, evaluateMasterPassword: () => true },
        },
        provideRouter([]),
        { provide: ToastService, useValue: { showToast: () => {} } },
        { provide: LogService, useValue: { error: () => {} } },
        { provide: ValidationService, useValue: { showError: () => {} } },
        { provide: LoginSuccessHandlerService, useValue: { run: async () => {} } },
        { provide: ConfigService, useValue: configService },
        { provide: SsoLoginServiceAbstraction, useValue: { ssoRequiredCache$: of(null) } },
        { provide: EnvironmentService, useValue: { environment$: of(null) } },
        {
          provide: AnonLayoutWrapperDataService,
          useValue: { setAnonLayoutWrapperData: jest.fn() },
        },
      ],
    }).compileComponents();

    fixture = TestBed.createComponent(LoginComponent);
    fixture.detectChanges();
    await fixture.whenStable();
    fixture.detectChanges();
  });

  it("renders the local-first helper copy and current-password field", () => {
    const text = fixture.nativeElement.textContent;

    expect(text).toContain("extDesc");
    expect(text).toContain("Open Password Help");
    expect(text).toContain("Open the local password help page before you try again.");

    const input = fixture.debugElement.query(By.css('input[formControlName="masterPassword"]'))
      .nativeElement as HTMLInputElement;
    expect(input.autocomplete).toBe("current-password");
  });
});
