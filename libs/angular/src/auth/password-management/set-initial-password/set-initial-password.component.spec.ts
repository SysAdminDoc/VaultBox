import { SetInitialPasswordComponent } from "./set-initial-password.component";
import { SetInitialPasswordUserType } from "./set-initial-password.service.abstraction";

describe("SetInitialPasswordComponent premium helper copy", () => {
  function createComponent(userType: SetInitialPasswordUserType) {
    const component = Object.create(
      SetInitialPasswordComponent.prototype,
    ) as SetInitialPasswordComponent;
    (component as any).userType = userType;
    return component as any;
  }

  it("surfaces a join-focused helper for jit provisioned users", () => {
    const component = createComponent(SetInitialPasswordUserType.JIT_PROVISIONED_MP_ORG_USER);

    expect(component.setupPanelTitle).toBe("Finish Creating Your Master Password");
    expect(component.setupPanelBody).toContain("organization finishes setup");
  });

  it("surfaces a recovery-focused helper for offboarded tde users", () => {
    const component = createComponent(SetInitialPasswordUserType.OFFBOARDED_TDE_ORG_USER);

    expect(component.setupPanelTitle).toBe("Restore Local Vault Access");
    expect(component.setupPanelBody).toContain("direct local unlock");
  });
});
