//
//  ParameterSlider.swift
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//

import SwiftUI

/// A SwiftUI Slider container which is bound to an ObservableAUParameter
///
/// This view wraps a SwiftUI Slider, and provides it relevant data from the Parameter, like the minimum and maximum values.
/// With `logarithmic` set, equal slider travel covers equal ratios (octaves of cutoff, doublings of time), which is how
/// frequency and time controls need to feel. It needs a minimum above zero.
struct ParameterSlider: View {
    @State var param: ObservableAUParameter
    var logarithmic = false

    var specifier: String {
        switch param.unit {
        case .midiNoteNumber, .indexed, .hertz, .milliseconds, .relativeSemiTones:
            return "%.0f"
        default:
            return "%.2f"
        }
    }

    private var usesLogScale: Bool {
        logarithmic && param.min > 0
    }

    /// The slider's position, 0...1 on a log scale, or the plain value otherwise.
    private var position: Binding<AUValue> {
        guard usesLogScale else {
            return $param.value
        }
        let ratio = param.max / param.min
        return Binding(
            get: { log(param.value / param.min) / log(ratio) },
            set: { param.value = param.min * pow(ratio, $0) }
        )
    }

    var body: some View {
        VStack {
            Slider(
                value: position,
                in: usesLogScale ? 0...1 : param.min...param.max,
                onEditingChanged: param.onEditingChanged,
                minimumValueLabel: Text("\(param.min, specifier: specifier)"),
                maximumValueLabel: Text("\(param.max, specifier: specifier)")
            ) {
                EmptyView()
            }
            .accessibility(identifier: param.displayName)
            Text("\(param.displayName): \(param.value, specifier: specifier)")
        }
        .padding()
    }
}

/// A segmented picker for an indexed parameter (a choice between named options).
struct ParameterPicker: View {
    @State var param: ObservableAUParameter
    let options: [String]

    private var selection: Binding<Int> {
        Binding(
            get: { Int(param.value.rounded()) },
            set: { newValue in
                param.onEditingChanged(true)
                param.value = AUValue(newValue)
                param.onEditingChanged(false)
            }
        )
    }

    var body: some View {
        Picker(param.displayName, selection: selection) {
            ForEach(options.indices, id: \.self) { index in
                Text(options[index]).tag(index)
            }
        }
        .pickerStyle(.segmented)
        .accessibility(identifier: param.displayName)
        .padding()
    }
}
