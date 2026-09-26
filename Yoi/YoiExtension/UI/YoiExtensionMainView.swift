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

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 12) {
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
