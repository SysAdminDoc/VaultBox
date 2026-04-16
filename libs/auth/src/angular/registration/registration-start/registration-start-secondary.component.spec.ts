import { ComponentFixture, TestBed } from "@angular/core/testing";
import { ActivatedRoute } from "@angular/router";
import { of } from "rxjs";

import { RegistrationStartSecondaryComponent } from "./registration-start-secondary.component";

describe("RegistrationStartSecondaryComponent", () => {
  let fixture: ComponentFixture<RegistrationStartSecondaryComponent>;

  beforeEach(async () => {
    await TestBed.configureTestingModule({
      imports: [RegistrationStartSecondaryComponent],
      providers: [
        {
          provide: ActivatedRoute,
          useValue: {
            data: of({ loginRoute: "/login" }),
          },
        },
      ],
    }).compileComponents();

    fixture = TestBed.createComponent(RegistrationStartSecondaryComponent);
    fixture.detectChanges();
    await fixture.whenStable();
    fixture.detectChanges();
  });

  it("renders the calmer sign-in alternative copy", () => {
    expect(fixture.nativeElement.textContent).toContain("Already have a vault?");
    expect(fixture.nativeElement.textContent).toContain("Sign in instead");
  });
});
