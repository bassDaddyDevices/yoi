# YOI

Source for YOI, a monophonic heavy vowel-bass instrument by Bass Daddy Devices: AUv3 on macOS, VST3 on macOS and Windows.

- It's built like Graphite: an Xcode AUv3 app with a portable C++ DSP kernel, then a CMake VST3 build (Steinberg VST3 SDK + choc web view) that runs the same kernel. There's no JUCE and no paid dependencies.
- Docs, roadmap and decisions live outside this repo in `../YOI_DOCS` (an OpenKnowledge knowledge base).
- The RNBO exports and Max device in `../YOI_PLANNING` are reference material. None of that code is used here.

## Layout

```
Yoi/                          Xcode project (Audio Unit Extension App template)
├── Yoi/                      host app: loads the plug-in, forwards any MIDI keyboard to it
└── YoiExtension/
    ├── DSP/
    │   ├── YoiExtensionDSPKernel.hpp   YOI's voice (portable C++, shared with VST3 and tests)
    │   ├── YoiFactoryShapes.hpp        built-in drawings for the envelope
    │   └── Shared/                     Bass Daddy Devices building blocks, no YOI knowledge:
    │                                   oscillators, filter, ADSR, note stack, glide, the drawn
    │                                   curve and its looping playback, the downsamplers, the
    │                                   wavefolder, and a holder for host blocks
    ├── Parameters/           parameter addresses (C) and the host-facing tree (Swift)
    ├── UI/                   WebEditor.swift (hosts the HTML editor and bridges it to the audio
    │                         unit), plus the SwiftUI slider panel kept as a fallback
    └── WebUI/                the editor page (plain HTML/CSS/JS, no build step):
                              bdd-*.js   shared by every Bass Daddy Devices synth: the bridge
                                         to the plug-in, pixel font, knobs/switches/readouts/
                                         menus, and the curve editor
                              yoi-*      YOI's own: panel layout, look, and browser stand-in
Tests/                        C++ render tests for the kernel (CMake)
Tools/                        update-standin.sh (refreshes the browser stand-in's snapshot)
```

## Building and trying it

Open `Yoi/Yoi.xcodeproj` and run the **Yoi** scheme. Running the app registers the Audio Unit with macOS. Play it from a MIDI keyboard in the app, or load **Bass Daddy Devices: YOI** as an instrument in Logic or Live (Live may need a plug-in rescan).

From the command line:

```sh
xcodebuild -project Yoi/Yoi.xcodeproj -scheme Yoi build   # build, signed with the team in the project
auval -v aumu yoi1 Bsdd                                  # validate once the app has run or been built
```

## Working on the editor

The editor is a plain web page in `Yoi/YoiExtension/WebUI`, drawn at 940×410 and scaled to fit the window: the drawing screen, and beside it three pages of controls (YOI, OSC, AMP) picked with tabs. To work on it without a host, serve that folder and open it in a browser; `yoi-standin.js` plays the plug-in's part:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory Yoi/YoiExtension/WebUI
```

The stand-in holds a snapshot of the plug-in's parameters, defaults and factory drawings. After changing any of those, build and run the app, then refresh it:

```sh
Tools/update-standin.sh
```

The stand-in only approximates bent segments, because the real curve is worked out by the plug-in's C++; inside a host the screen always shows the plug-in's own curve. Debug builds let Safari's Develop menu inspect the page inside a host, and script errors on the page are written to the extension's log (subsystem `com.bassdaddydevices.YoiExtension`, category `WebEditor`).

Drawing on the screen works like Max's `function`: click to add a point, drag to move, shift-click (or double-click) to delete, option-drag to bend a segment, and hold ⌘ while dragging to snap. Knobs and the amp faders drag up and down (shift for fine), scroll, take the arrow keys, and reset on double-click. The yellow values under the screen drag like Max number boxes; click one to flip it or pick from its menu.

## Tests

The DSP tests render audio through the kernel exactly as the VST3 build will compile it, with no plug-in format involved:

```sh
cmake -S Tests -B Tests/build && cmake --build Tests/build && ctest --test-dir Tests/build --output-on-failure
```
