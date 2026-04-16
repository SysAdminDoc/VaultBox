import { of } from "rxjs";

import { RegistrationFinishComponent } from "./registration-finish.component";

describe("RegistrationFinishComponent", () => {
  it("defaults the local email/token values and sets local-first wrapper copy", async () => {
    const anonLayoutWrapperDataService = { setAnonLayoutWrapperData: jest.fn() };
    const component = new RegistrationFinishComponent(
      { queryParams: of({}) } as any,
      {} as any,
      {} as any,
      { t: (key: string) => key } as any,
      {} as any,
      {} as any,
      {} as any,
      {} as any,
      { error: jest.fn() } as any,
      anonLayoutWrapperDataService as any,
      {} as any,
      {} as any,
    );

    await component.ngOnInit();

    expect((component as any).email).toBe("vault@localhost");
    expect((component as any).emailVerificationToken).toBe("vaultbox-local");
    expect(anonLayoutWrapperDataService.setAnonLayoutWrapperData).toHaveBeenCalledWith({
      pageTitle: "Create Your Local Vault",
      pageSubtitle: "Choose a master password you will use to unlock VaultBox on this device.",
    });
  });
});
