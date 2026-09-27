//
//  YoiExtensionMainView.swift
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//
//  A plain development panel: every parameter as a slider or picker, grouped by section. It is a
//  stand-in so the sound can be played with while the DSP is built; the HTML editor replaces it.
//

import SwiftUI

struct YoiExtensionMainView: View {
    var parameterTree: ObservableAUParameterGroup
    var audioUnit: YoiExtensionAudioUnit?

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
                section("Drawn Envelope") {
                    if let audioUnit {
                        Menu("Load Drawing") {
                            ForEach(Array(audioUnit.factoryShapeNames.enumerated()), id: \.offset) { index, name in
                                Button(name) { audioUnit.loadFactoryShape(index) }
                            }
                        }
                        .padding()
                    }
                    ParameterSlider(param: parameterTree.envelope.envAmount)
                    ParameterPicker(param: parameterTree.envelope.envTimeMode, options: ["Sync", "Free"])
                    ParameterPicker(param: parameterTree.envelope.envSyncLength, options: syncLengthNames, menu: true)
                    ParameterSlider(param: parameterTree.envelope.envFreeTime, logarithmic: true)
                    ParameterPicker(param: parameterTree.envelope.envDirection, options: directionNames, menu: true)
                    ParameterPicker(param: parameterTree.envelope.envRetrigger, options: ["Off", "On"])
                    ParameterSlider(param: parameterTree.envelope.accelStart, logarithmic: true)
                    ParameterSlider(param: parameterTree.envelope.accelEnd, logarithmic: true)
                    ParameterSlider(param: parameterTree.envelope.accelCurve)
                }
                section("Downsampler") {
                    ParameterPicker(param: parameterTree.grit.dsMode, options: ["Off", "S&H", "Downsample"])
                    ParameterSlider(param: parameterTree.grit.dsRate, logarithmic: true)
                    ParameterSlider(param: parameterTree.grit.dsAmount)
                }
                section("Wavefolder") {
                    ParameterSlider(param: parameterTree.grit.foldAmount)
                    ParameterPicker(param: parameterTree.grit.foldPosition, options: ["Pre-filter", "Pre-downsample"])
                }
                section("Clean-up Filter") {
                    ParameterPicker(param: parameterTree.grit.cleanupMode, options: ["Off", "On"])
                    ParameterSlider(param: parameterTree.grit.cleanupMultiple, logarithmic: true)
                }
                section("Oscillators") {
                    ParameterSlider(param: parameterTree.oscillators.oscShape)
                    ParameterSlider(param: parameterTree.oscillators.subLevel)
                    ParameterSlider(param: parameterTree.oscillators.subShape)
                    ParameterPicker(param: parameterTree.oscillators.subOctave, options: ["-1 Oct", "-2 Oct"])
                }
                section("Filter") {
                    ParameterPicker(param: parameterTree.filter.filterMode, options: ["LP", "BP"])
                    ParameterSlider(param: parameterTree.filter.cutoff, logarithmic: true)
                    ParameterSlider(param: parameterTree.filter.resonance)
                }
                section("Amp Envelope") {
                    ParameterSlider(param: parameterTree.amp.ampAttack, logarithmic: true)
                    ParameterSlider(param: parameterTree.amp.ampDecay, logarithmic: true)
                    ParameterSlider(param: parameterTree.amp.ampSustain)
                    ParameterSlider(param: parameterTree.amp.ampRelease, logarithmic: true)
                }
                section("Voice") {
                    ParameterSlider(param: parameterTree.voice.glideTime)
                    ParameterPicker(param: parameterTree.voice.glideMode, options: ["Legato", "Always"])
                    ParameterSlider(param: parameterTree.voice.bendRange)
                }
                section("Output") {
                    ParameterSlider(param: parameterTree.output.outputLevel)
                }
            }
            .padding()
        }
        .frame(minWidth: 520, minHeight: 400)
    }

    private func section<Content: View>(_ title: String, @ViewBuilder content: () -> Content) -> some View {
        GroupBox(title) {
            LazyVGrid(columns: [GridItem(.flexible()), GridItem(.flexible())], alignment: .leading) {
                content()
            }
        }
    }
}
