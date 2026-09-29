//
//  Parameters.swift
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//
//  The parameters hosts see. Defaults must match the kernel's (YoiExtensionDSPKernel.hpp),
//  which is also what the VST3 build and the DSP tests start from.
//

import Foundation
import AudioToolbox

private let logarithmic: AudioUnitParameterOptions = [.flag_IsWritable, .flag_IsReadable, .flag_DisplayLogarithmic]

let YoiExtensionParameterSpecs = ParameterTreeSpec {
    ParameterGroupSpec(identifier: "output", name: "Output") {
        ParameterSpec(
            address: .outputLevel,
            identifier: "outputLevel",
            name: "Output Level",
            units: .decibels,
            valueRange: -48.0...6.0,
            defaultValue: 0.0
        )
    }
    ParameterGroupSpec(identifier: "voice", name: "Voice") {
        ParameterSpec(
            address: .glideTime,
            identifier: "glideTime",
            name: "Glide Time",
            units: .milliseconds,
            valueRange: 0.0...2000.0,
            defaultValue: 60.0
        )
        ParameterSpec(
            address: .glideMode,
            identifier: "glideMode",
            name: "Glide Mode",
            units: .indexed,
            valueRange: 0...1,
            defaultValue: 0,
            valueStrings: ["Legato", "Always"]
        )
        ParameterSpec(
            address: .bendRange,
            identifier: "bendRange",
            name: "Bend Range",
            units: .relativeSemiTones,
            valueRange: 0.0...24.0,
            defaultValue: 2.0
        )
    }
    ParameterGroupSpec(identifier: "oscillators", name: "Oscillators") {
        ParameterSpec(
            address: .oscShape,
            identifier: "oscShape",
            name: "Osc Shape",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 0.0
        )
        ParameterSpec(
            address: .subLevel,
            identifier: "subLevel",
            name: "Sub Level",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 75.0
        )
        ParameterSpec(
            address: .subShape,
            identifier: "subShape",
            name: "Sub Shape",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 0.0
        )
        ParameterSpec(
            address: .subOctave,
            identifier: "subOctave",
            name: "Sub Octave",
            units: .indexed,
            valueRange: 0...1,
            defaultValue: 0,
            valueStrings: ["-1 Oct", "-2 Oct"]
        )
        ParameterSpec(
            address: .subCrossover,
            identifier: "subCrossover",
            name: "Sub Crossover",
            units: .hertz,
            valueRange: 50.0...700.0,
            defaultValue: 130.0,
            flags: logarithmic
        )
    }
    ParameterGroupSpec(identifier: "filter", name: "Filter") {
        ParameterSpec(
            address: .filterMode,
            identifier: "filterMode",
            name: "Filter Mode",
            units: .indexed,
            valueRange: 0...1,
            defaultValue: 0,
            valueStrings: ["LP", "BP"]
        )
        ParameterSpec(
            address: .cutoff,
            identifier: "cutoff",
            name: "Cutoff",
            units: .hertz,
            valueRange: 20.0...2500.0,
            defaultValue: 800.0,
            flags: logarithmic
        )
        ParameterSpec(
            address: .resonance,
            identifier: "resonance",
            name: "Resonance",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 30.0
        )
        ParameterSpec(
            address: .filterMirror,
            identifier: "filterMirror",
            name: "Mirror",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 0.0
        )
        ParameterSpec(
            address: .filterDrive,
            identifier: "filterDrive",
            name: "Filter Drive",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 0.0
        )
    }
    ParameterGroupSpec(identifier: "amp", name: "Amp Envelope") {
        ParameterSpec(
            address: .ampAttack,
            identifier: "ampAttack",
            name: "Attack",
            units: .milliseconds,
            valueRange: 0.1...5000.0,
            defaultValue: 3.0,
            flags: logarithmic
        )
        ParameterSpec(
            address: .ampDecay,
            identifier: "ampDecay",
            name: "Decay",
            units: .milliseconds,
            valueRange: 1.0...5000.0,
            defaultValue: 300.0,
            flags: logarithmic
        )
        ParameterSpec(
            address: .ampSustain,
            identifier: "ampSustain",
            name: "Sustain",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 100.0
        )
        ParameterSpec(
            address: .ampRelease,
            identifier: "ampRelease",
            name: "Release",
            units: .milliseconds,
            valueRange: 1.0...10000.0,
            defaultValue: 150.0,
            flags: logarithmic
        )
    }
    ParameterGroupSpec(identifier: "envelope", name: "Drawn Envelope") {
        ParameterSpec(
            address: .envAmount,
            identifier: "envAmount",
            name: "Env Amount",
            units: .octaves,
            valueRange: 0.0...8.0,
            defaultValue: 3.0
        )
        ParameterSpec(
            address: .envTimeMode,
            identifier: "envTimeMode",
            name: "Env Time Mode",
            units: .indexed,
            valueRange: 0...1,
            defaultValue: 0,
            valueStrings: ["Sync", "Free"]
        )
        ParameterSpec(
            address: .envSyncLength,
            identifier: "envSyncLength",
            name: "Env Sync",
            units: .indexed,
            valueRange: 0...AUValue(syncLengthNames.count - 1),
            defaultValue: 6,
            valueStrings: syncLengthNames
        )
        ParameterSpec(
            address: .envFreeTime,
            identifier: "envFreeTime",
            name: "Env Free Time",
            units: .milliseconds,
            valueRange: 10.0...30000.0,
            defaultValue: 500.0,
            flags: logarithmic
        )
        ParameterSpec(
            address: .envDirection,
            identifier: "envDirection",
            name: "Env Direction",
            units: .indexed,
            valueRange: 0...AUValue(directionNames.count - 1),
            defaultValue: 0,
            valueStrings: directionNames
        )
        ParameterSpec(
            address: .envRetrigger,
            identifier: "envRetrigger",
            name: "Env Re-Trigger",
            units: .boolean,
            valueRange: 0...1,
            defaultValue: 0
        )
        ParameterSpec(
            address: .accelStart,
            identifier: "accelStart",
            name: "Accel Start",
            units: .rate,
            valueRange: 0.1...4.0,
            defaultValue: 0.25,
            flags: logarithmic
        )
        ParameterSpec(
            address: .accelEnd,
            identifier: "accelEnd",
            name: "Accel End",
            units: .rate,
            valueRange: 0.1...4.0,
            defaultValue: 2.0,
            flags: logarithmic
        )
        ParameterSpec(
            address: .accelCurve,
            identifier: "accelCurve",
            name: "Accel Curve",
            units: .generic,
            valueRange: -1.0...1.0,
            defaultValue: 0.0
        )
    }
    ParameterGroupSpec(identifier: "grit", name: "Grit") {
        ParameterSpec(
            address: .dsMode,
            identifier: "dsMode",
            name: "Downsampler",
            units: .indexed,
            valueRange: 0...2,
            defaultValue: 1,
            valueStrings: ["Off", "S&H", "Downsample"]
        )
        ParameterSpec(
            address: .dsRate,
            identifier: "dsRate",
            name: "S&H Rate",
            units: .hertz,
            valueRange: 1300.0...6000.0,
            defaultValue: 1400.0,
            flags: logarithmic
        )
        ParameterSpec(
            address: .dsAmount,
            identifier: "dsAmount",
            name: "Downsample Amount",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 45.0
        )
        ParameterSpec(
            address: .foldAmount,
            identifier: "foldAmount",
            name: "Fold",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 0.0
        )
        ParameterSpec(
            address: .foldPosition,
            identifier: "foldPosition",
            name: "Fold Position",
            units: .indexed,
            valueRange: 0...2,
            defaultValue: 2,
            valueStrings: ["Pre-filter", "Pre-downsample", "Post-downsample"]
        )
        ParameterSpec(
            address: .cleanupMode,
            identifier: "cleanupMode",
            name: "Clean-up",
            units: .indexed,
            valueRange: 0...1,
            defaultValue: 1,
            valueStrings: ["Off", "On"]
        )
        ParameterSpec(
            address: .cleanupMultiple,
            identifier: "cleanupMultiple",
            name: "Clean-up Multiple",
            units: .ratio,
            valueRange: 1.0...16.0,
            defaultValue: 5.0,
            flags: logarithmic
        )
        ParameterSpec(
            address: .dsLock,
            identifier: "dsLock",
            name: "S&H Lock",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 0.0
        )
    }
    ParameterGroupSpec(identifier: "finish", name: "Finish") {
        ParameterSpec(
            address: .boostAmount,
            identifier: "boostAmount",
            name: "Harmonic Boost",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 0.0
        )
        ParameterSpec(
            address: .ottDepth,
            identifier: "ottDepth",
            name: "OTT",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 0.0
        )
        ParameterSpec(
            address: .widthAmount,
            identifier: "widthAmount",
            name: "Width",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 0.0
        )
        ParameterSpec(
            address: .ottTime,
            identifier: "ottTime",
            name: "OTT Time",
            units: .percent,
            valueRange: 0.0...100.0,
            defaultValue: 50.0
        )
        ParameterSpec(
            address: .ottUpward,
            identifier: "ottUpward",
            name: "OTT Upward",
            units: .percent,
            valueRange: 0.0...200.0,
            defaultValue: 100.0
        )
    }
}

/// Option names come from the kernel, so the host and the DSP always agree on them.
let syncLengthNames: [String] = (0..<Int(YoiExtensionDSPKernel.syncLengthCount())).map {
    String(cString: YoiExtensionDSPKernel.syncLengthName(Int32($0)))
}
let directionNames: [String] = (0..<Int(YoiExtensionDSPKernel.directionCount())).map {
    String(cString: YoiExtensionDSPKernel.directionName(Int32($0)))
}

extension ParameterSpec {
    init(
        address: YoiExtensionParameterAddress,
        identifier: String,
        name: String,
        units: AudioUnitParameterUnit,
        valueRange: ClosedRange<AUValue>,
        defaultValue: AUValue,
        unitName: String? = nil,
        flags: AudioUnitParameterOptions = [AudioUnitParameterOptions.flag_IsWritable, AudioUnitParameterOptions.flag_IsReadable],
        valueStrings: [String]? = nil,
        dependentParameters: [NSNumber]? = nil
    ) {
        self.init(address: address.rawValue,
                  identifier: identifier,
                  name: name,
                  units: units,
                  valueRange: valueRange,
                  defaultValue: defaultValue,
                  unitName: unitName,
                  flags: flags,
                  valueStrings: valueStrings,
                  dependentParameters: dependentParameters)
    }
}
