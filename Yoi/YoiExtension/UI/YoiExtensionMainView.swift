//
//  YoiExtensionMainView.swift
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//

import SwiftUI

struct YoiExtensionMainView: View {
    var parameterTree: ObservableAUParameterGroup
    
    var body: some View {
        ParameterSlider(param: parameterTree.global.gain)
    }
}
