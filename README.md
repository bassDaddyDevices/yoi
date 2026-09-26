# YOI

Source for YOI, a monophonic heavy vowel-bass instrument by Bass Daddy Devices: AUv3 on macOS, VST3 on macOS and Windows.

- It's built like Graphite: an Xcode AUv3 app with a portable C++ DSP kernel, then a CMake VST3 build (Steinberg VST3 SDK + choc web view) that runs the same kernel. There's no JUCE and no paid dependencies.
- Docs, roadmap and decisions live outside this repo in `../YOI_DOCS` (an OpenKnowledge knowledge base).
- The RNBO exports in `../YOI_PLANNING/DSP` are reference material from the Max device. None of that code is used here.

Build instructions will be added with the first project scaffold.
