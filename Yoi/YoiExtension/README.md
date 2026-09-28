# YoiExtension

The YOI Audio Unit extension: "Bass Daddy Devices: YOI" (`aumu` / `yoi1` / `Bsdd`). It started from Apple's Audio Unit Extension App template; this note replaces the template's README.

- Build, test and layout: the workspace `README.md`, one folder up from `Yoi/`.
- How the code works: the codebase wiki in `YOI_DOCS/wiki/` (start at `OVERVIEW.md`).
- Why it's built this way: the decisions in `YOI_DOCS/decisions/`.

| Folder | What's there |
| --- | --- |
| `DSP/` | The portable C++ kernel (`YoiExtensionDSPKernel.hpp`), the factory drawings, and `Shared/`, the reusable `bdd` blocks |
| `Parameters/` | Parameter addresses (C header, shared with the kernel) and the host-facing tree (`Parameters.swift`) |
| `Common/` | Template plumbing: the `AUAudioUnit` subclass, the render-block process helper, the view controller |
| `UI/` | `WebEditor.swift` (hosts the web editor), and the SwiftUI panel kept as a fallback |
| `WebUI/` | The editor page: shared `bdd-*.js` parts and YOI's `yoi-*` files |

## Adding a parameter

Addresses, identifiers and the saved drawing format are persistent: add parameters, never renumber or rename one.

1. Add the address to `YoiExtensionParameterAddress` in `Parameters/YoiExtensionParameterAddresses.h`, in its section's block of ten, in **both** copies of the enum (Audio Unit and portable).
2. Add a `ParameterSpec` to `Parameters/Parameters.swift` with the same default the kernel will use.
3. In `DSP/YoiExtensionDSPKernel.hpp`, add the member and its default, a clamped case in `setParameter` and `getParameter`, and any smoother (in `initialize` and `snapSmoothers`). Store only the target; derive anything else on the render thread.
4. Add the default to `testDefaultsMatchParameterTree` and a value to `testParametersRoundTrip` in `Tests/yoi_dsp_tests.cpp`, plus a test of what it does.
5. Put it on the panel: an entry in `PAGES` in `WebUI/yoi-panel.js` (and a line in the SwiftUI fallback, `UI/YoiExtensionMainView.swift`).
6. Build and run the app, then run `Tools/update-standin.sh` so the browser stand-in knows it.
7. Add it to the parameter map, `YOI_DOCS/specs/parameters.md`.
