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
            defaultValue: 50.0
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
            valueRange: 20.0...20000.0,
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
