# Non-Functional Requirements — `protracker2-agent-assist`

**ID:** `PT2AA-NFR-001`  
**Related Vision:** [docs/vision.md](vision.md) (no functional spec ID created yet)

---

## Coverage Table (ISO/IEC 25010:2023)

| Quality Area / Constraint | Status | Notes |
|---|---|---|
| Functional suitability | Not relevant | Covered in functional specs |
| Performance efficiency | Open | Real-time audio / Rust DSP targets not yet specified |
| Compatibility | Open | SDL2 vs SDL3, macOS/Linux/Windows targets |
| Interaction capability | Not relevant | — |
| Reliability | Open | — |
| Security | Not relevant | Local desktop app; API key handling open if LLM calls are added |
| **Maintainability** | **Covered** | Automated CI build on check-in (`PT2AA-NFR-001`) |
| Flexibility (portability) | Covered | Cross-platform CMake build on macOS & Linux |
| Safety | Not relevant | — |
| Operations (CI/CD) | **Covered** | GitHub Actions workflow on push/PR |

---

## Priorities
- **Open:** Top 3 quality areas and trade-off ranking not yet specified by user.

---

## Requirements

### `PT2AA-NFR-001` — Automated CI Build on Check-In
- **Quality Area:** Maintainability / Operations (CI/CD)
- **Source:** User request ("use CI/CD so it builds automatically on check-in using GitHub Actions")
- **Measure (Planguage):**
  - **Scale:** Build pass/fail status per commit push / pull request
  - **Meter:** GitHub Actions workflow run (`.github/workflows/ci.yml`)
  - **Must:** 100% of pushes and pull requests trigger an automated CMake build on Linux and macOS
  - **Goal:** Needs input from user (e.g., max build duration target)

```gherkin
Feature: Automated CI build on check-in
  Scenario: Developer pushes code or opens a pull request
    Given the repository contains a valid CMake configuration
    When a commit is pushed or a pull request is opened
    Then GitHub Actions SHALL run the CMake build on Ubuntu and macOS
    And report a pass or fail check status on the commit
```

---

## Constraints
- Use **CMake** (not Xcode `xcodebuild`, since the repository has no `.xcodeproj` and uses `CMakeLists.txt`).
- Hosted on **GitHub Actions**.

## Open Questions
1. **Build time target:** What is the maximum acceptable CI build duration? *(Needs input from user)*
2. **Static analysis / warnings-as-errors:** Should CI fail on compiler warnings (`-Werror`) or run `clang` static analysis? *(Needs input from user)*
3. **Target platforms:** Should Windows (`windows-latest`) also be included in the CI matrix? *(Needs input from user)*
