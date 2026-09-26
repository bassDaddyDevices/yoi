//
//  YoiApp.swift
//  Yoi
//
//  Created by Chris Connelly on 2026-09-26.
//

import SwiftUI

@main
struct YoiApp: App {
    private let hostModel = AudioUnitHostModel()

    var body: some Scene {
        WindowGroup {
            ContentView(hostModel: hostModel)
        }
    }
}
