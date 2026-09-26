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
    │   └── Shared/                     Bass Daddy Devices building blocks, no YOI knowledge:
    │                                   oscillators, filter, envelope, note stack, glide
    ├── Parameters/           parameter addresses (C) and the host-facing tree (Swift)
    └── UI/                   temporary SwiftUI slider panel, until the HTML editor
Tests/                        C++ render tests for the kernel (CMake)
```

## Building and trying it

Open `Yoi/Yoi.xcodeproj` and run the **Yoi** scheme. Running the app registers the Audio Unit with macOS. Play it from a MIDI keyboard in the app, or load **Bass Daddy Devices: YOI** as an instrument in Logic or Live (Live may need a plug-in rescan).

From the command line:

```sh
xcodebuild -project Yoi/Yoi.xcodeproj -scheme Yoi build   # build, signed with the team in the project
auval -v aumu yoi1 Bsdd                                  # validate once the app has run or been built
```

## Tests

The DSP tests render audio through the kernel exactly as the VST3 build will compile it, with no plug-in format involved:

```sh
cmake -S Tests -B Tests/build && cmake --build Tests/build && ctest --test-dir Tests/build --output-on-failure
```
