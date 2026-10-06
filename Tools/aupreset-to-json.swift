//
//  aupreset-to-json.swift
//  Tools
//
//  Turns a user preset saved by the YOI Audio Unit (.aupreset) into a factory preset in the
//  portable bdd-preset format (YOI_DOCS/specs/preset-format.md), printed to standard output.
//
//      swiftc -O Tools/aupreset-to-json.swift -o /tmp/aupreset-to-json
//      /tmp/aupreset-to-json --number 3 --name "Basic Growl" path/to/Basic%20Growl_5.aupreset \
//          > Yoi/YoiExtension/Presets/Factory/basic-growl.json
//
//  The Audio Unit decodes the preset itself: the tool instantiates the installed YOI (the copy
//  macOS has registered, normally Xcode's latest build), hands it the preset's full state, and
//  reads back every host parameter by identifier and the drawing. Nothing here depends on how
//  AUParameterTree serialises its values.
//

import AVFoundation
import AudioToolbox
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
    exit(1)
}

// MARK: - Arguments

var number: Int?
var name: String?
var path: String?
var arguments = CommandLine.arguments.dropFirst()
while let argument = arguments.popFirst() {
    switch argument {
    case "--number": number = arguments.popFirst().flatMap { Int($0) }
    case "--name": name = arguments.popFirst()
    default: path = argument
    }
}
guard let number, number > 0 else { fail("--number must be 1 or more (0 is Init)") }
guard let path else { fail("usage: aupreset-to-json --number N [--name NAME] file.aupreset") }

// MARK: - Read the preset

let classes: [AnyClass] = [NSDictionary.self, NSArray.self, NSNumber.self, NSString.self, NSData.self]
guard let raw = try? Data(contentsOf: URL(fileURLWithPath: path)),
      let state = try? NSKeyedUnarchiver.unarchivedObject(ofClasses: classes, from: raw) as? [String: Any] else {
    fail("could not read \(path)")
}
let presetName = name ?? (state["name"] as? String) ?? "Untitled"

// MARK: - Let the Audio Unit decode it

let description = AudioComponentDescription(componentType: kAudioUnitType_MusicDevice,
                                            componentSubType: 0x796f6931,      // 'yoi1'
                                            componentManufacturer: 0x42736464, // 'Bsdd'
                                            componentFlags: 0, componentFlagsMask: 0)
var unit: AUAudioUnit?
var finished = false
// The completion arrives on the main thread, so spin the run loop rather than block it.
AUAudioUnit.instantiate(with: description, options: [.loadOutOfProcess]) { audioUnit, error in
    if let error { FileHandle.standardError.write("instantiate failed: \(error)\n".data(using: .utf8)!) }
    unit = audioUnit
    finished = true
}
let deadline = Date().addingTimeInterval(20)
while !finished && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.05)) }
guard let audioUnit = unit, let tree = audioUnit.parameterTree else { fail("could not load YOI (aumu yoi1 Bsdd)") }

audioUnit.fullState = state
RunLoop.main.run(until: Date().addingTimeInterval(0.1))

let parameters = tree.allParameters.map { ($0.identifier, $0.value) }.sorted { $0.0 < $1.0 }
guard let drawing = audioUnit.fullState?["yoiEnvelopeCurve"] as? [[NSNumber]] else { fail("the preset has no drawing") }

// MARK: - Write it out

/// The shortest text that reads back as the same Float, without a trailing ".0".
func formatted(_ value: Float) -> String {
    let text = value.description
    return text.hasSuffix(".0") ? String(text.dropLast(2)) : text
}

func quoted(_ text: String) -> String {
    let data = try! JSONSerialization.data(withJSONObject: text, options: [.fragmentsAllowed, .withoutEscapingSlashes])
    return String(data: data, encoding: .utf8)!
}

var lines: [String] = []
lines.append("{")
lines.append("  \"format\": \"bdd-preset\",")
lines.append("  \"version\": 1,")
lines.append("  \"synth\": \"YOI\",")
lines.append("  \"number\": \(number),")
lines.append("  \"name\": \(quoted(presetName)),")
lines.append("  \"parameters\": {")
for (index, parameter) in parameters.enumerated() {
    let comma = index < parameters.count - 1 ? "," : ""
    lines.append("    \(quoted(parameter.0)): \(formatted(parameter.1))\(comma)")
}
lines.append("  },")
lines.append("  \"drawing\": [")
for (index, point) in drawing.enumerated() {
    let comma = index < drawing.count - 1 ? "," : ""
    lines.append("    [\(point.map { formatted($0.floatValue) }.joined(separator: ", "))]\(comma)")
}
lines.append("  ]")
lines.append("}")
print(lines.joined(separator: "\n"))
