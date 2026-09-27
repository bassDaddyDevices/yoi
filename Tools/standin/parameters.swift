// Prints the parameters of the registered YOI Audio Unit as JSON, the way the editor's
// descriptor describes them, with each one's default (a new instance's value).
// Used by Tools/update-standin.sh.

import AVFoundation
import Foundation

func fourCC(_ text: String) -> FourCharCode { text.utf8.reduce(0) { ($0 << 8) | FourCharCode($1) } }

let description = AudioComponentDescription(componentType: kAudioUnitType_MusicDevice,
                                            componentSubType: fourCC("yoi1"),
                                            componentManufacturer: fourCC("Bsdd"),
                                            componentFlags: 0, componentFlagsMask: 0)
var unit: AUAudioUnit?
var failure: Error?
AUAudioUnit.instantiate(with: description, options: [.loadOutOfProcess]) { instance, error in
    unit = instance
    failure = error
}
let deadline = Date().addingTimeInterval(20)
while unit == nil && failure == nil && Date() < deadline {
    RunLoop.main.run(until: Date().addingTimeInterval(0.01))
}
guard let unit, let tree = unit.parameterTree else {
    FileHandle.standardError.write("Couldn't load YOI (aumu yoi1 Bsdd). Build and run the Yoi app first.\n".data(using: .utf8)!)
    exit(1)
}

func unitName(_ unit: AudioUnitParameterUnit) -> String {
    switch unit {
    case .hertz: return "hz"
    case .milliseconds: return "ms"
    case .percent: return "percent"
    case .decibels: return "db"
    case .octaves: return "octaves"
    case .ratio: return "ratio"
    case .rate: return "rate"
    case .relativeSemiTones: return "semitones"
    case .indexed: return "indexed"
    case .boolean: return "boolean"
    default: return "generic"
    }
}

var parameters: [[String: Any]] = []
var values: [String: Any] = [:]
for parameter in tree.allParameters {
    var entry: [String: Any] = [
        "id": parameter.address,
        "identifier": parameter.identifier,
        "name": parameter.displayName,
        "group": parameter.keyPath.split(separator: ".").first.map(String.init) ?? "",
        "min": parameter.minValue,
        "max": parameter.maxValue,
        "default": parameter.value,
        "unit": unitName(parameter.unit),
        "log": parameter.flags.contains(.flag_DisplayLogarithmic),
    ]
    if let strings = parameter.valueStrings {
        entry["options"] = strings
    }
    parameters.append(entry)
    values[String(parameter.address)] = parameter.value
}
let data = try JSONSerialization.data(withJSONObject: ["parameters": parameters, "values": values], options: [.sortedKeys])
print(String(data: data, encoding: .utf8)!)
