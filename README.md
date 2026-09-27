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
    └── WebUI/                the editor: index.html, style.css, bridge.js (reusable plug-in
                              bridge), panel.js, standin.js (browser stand-in for the plug-in)
Tests/                        C++ render tests for the kernel (CMake)
```

## Building and trying it

Open `Yoi/Yoi.xcodeproj` and run the **Yoi** scheme. Running the app registers the Audio Unit with macOS. Play it from a MIDI keyboard in the app, or load **Bass Daddy Devices: YOI** as an instrument in Logic or Live (Live may need a plug-in rescan).

From the command line:

```sh
xcodebuild -project Yoi/Yoi.xcodeproj -scheme Yoi build   # build, signed with the team in the project
auval -v aumu yoi1 Bsdd                                  # validate once the app has run or been built
```

## Working on the editor

The editor is a plain web page in `Yoi/YoiExtension/WebUI`. To work on it without a host, serve that folder and open it in a browser; `standin.js` plays the plug-in's part:

```sh
python3 -m http.server 8765 --bind 127.0.0.1 --directory Yoi/YoiExtension/WebUI
```

The stand-in's parameter list is a snapshot of the real plug-in's; regenerate it when parameters change. It joins drawing points with straight lines, because the real curve is worked out by the plug-in's C++. Inside a host, debug builds let Safari's Develop menu inspect the page.

## Tests

The DSP tests render audio through the kernel exactly as the VST3 build will compile it, with no plug-in format involved:

```sh
cmake -S Tests -B Tests/build && cmake --build Tests/build && ctest --test-dir Tests/build --output-on-failure
```
