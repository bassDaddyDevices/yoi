//
//  AudioUnitViewController.swift
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//

import CoreAudioKit
import os
import SwiftUI

private let log = Logger(subsystem: "com.bassdaddydevices.YoiExtension", category: "AudioUnitViewController")

@MainActor
public class AudioUnitViewController: AUViewController, AUAudioUnitFactory {
    var audioUnit: AUAudioUnit?
    
    var hostingController: HostingController<YoiExtensionMainView>?

    /// The HTML editor. The SwiftUI slider panel is kept as a fallback: set `useWebEditor` to
    /// false to get it back, for example if a host can't show the web view.
    var webEditor: WebEditor?
    static let useWebEditor = true
    static let editorSize = NSSize(width: 1040, height: 640)   // the page's design size; it scales to fit
    
    private var observation: NSKeyValueObservation?

	deinit {
        let editor = webEditor
        Task { @MainActor in
            editor?.invalidate()
        }
	}

    public override func viewWillAppear() {
        super.viewWillAppear()
        webEditor?.resume()
    }

    public override func viewDidDisappear() {
        super.viewDidDisappear()
        webEditor?.pause()
    }

    public override func viewDidLoad() {
        super.viewDidLoad()
        preferredContentSize = Self.editorSize
        
        // Accessing the `audioUnit` parameter prompts the AU to be created via createAudioUnit(with:)
        guard let audioUnit = self.audioUnit else {
            return
        }
        configureSwiftUIView(audioUnit: audioUnit)
    }
    
	nonisolated public func createAudioUnit(with componentDescription: AudioComponentDescription) throws -> AUAudioUnit {
		return try DispatchQueue.main.sync {
			
			audioUnit = try YoiExtensionAudioUnit(componentDescription: componentDescription, options: [])
			
			guard let audioUnit = self.audioUnit as? YoiExtensionAudioUnit else {
				log.error("Unable to create YoiExtensionAudioUnit")
				return audioUnit!
			}
			
			defer {
				// Configure the SwiftUI view after creating the AU, instead of in viewDidLoad,
				// so that the parameter tree is set up before we build our @AUParameterUI properties
				DispatchQueue.main.async {
					self.configureSwiftUIView(audioUnit: audioUnit)
				}
			}
			
			audioUnit.setupParameterTree(YoiExtensionParameterSpecs.createAUParameterTree())
			
			self.observation = audioUnit.observe(\.allParameterValues, options: [.new]) { object, change in
				guard let tree = audioUnit.parameterTree else { return }
				
				// This insures the Audio Unit gets initial values from the host.
				for param in tree.allParameters { param.value = param.value }
			}
			
			guard audioUnit.parameterTree != nil else {
				log.error("Unable to access AU ParameterTree")
				return audioUnit
			}
			
			return audioUnit
		}
	}
    
    private func configureSwiftUIView(audioUnit: AUAudioUnit) {
        if let host = hostingController {
            host.removeFromParent()
            host.view.removeFromSuperview()
        }
        if Self.useWebEditor, let yoi = audioUnit as? YoiExtensionAudioUnit {
            configureWebEditor(audioUnit: yoi)
            return
        }
        
        guard let observableParameterTree = audioUnit.observableParameterTree else {
            return
        }
        let content = YoiExtensionMainView(parameterTree: observableParameterTree,
                                           audioUnit: audioUnit as? YoiExtensionAudioUnit)
        let host = HostingController(rootView: content)
        self.addChild(host)
        host.view.frame = self.view.bounds
        self.view.addSubview(host.view)
        hostingController = host
        
        // Make sure the SwiftUI view fills the full area provided by the view controller
        host.view.translatesAutoresizingMaskIntoConstraints = false
        host.view.topAnchor.constraint(equalTo: self.view.topAnchor).isActive = true
        host.view.leadingAnchor.constraint(equalTo: self.view.leadingAnchor).isActive = true
        host.view.trailingAnchor.constraint(equalTo: self.view.trailingAnchor).isActive = true
        host.view.bottomAnchor.constraint(equalTo: self.view.bottomAnchor).isActive = true
        self.view.bringSubviewToFront(host.view)
    }

    private func configureWebEditor(audioUnit: YoiExtensionAudioUnit) {
        webEditor?.invalidate()
        webEditor?.view.removeFromSuperview()

        let editor = WebEditor(audioUnit: audioUnit)
        let editorView = editor.view
        editorView.translatesAutoresizingMaskIntoConstraints = false
        view.addSubview(editorView)
        NSLayoutConstraint.activate([
            editorView.topAnchor.constraint(equalTo: view.topAnchor),
            editorView.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            editorView.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            editorView.bottomAnchor.constraint(equalTo: view.bottomAnchor),
        ])
        webEditor = editor
        preferredContentSize = Self.editorSize
    }
}
