//
//  YoiExtensionAudioUnit.swift
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//

import AVFoundation

public class YoiExtensionAudioUnit: AUAudioUnit, @unchecked Sendable
{
	// C++ Objects
	var kernel = YoiExtensionDSPKernel()
    var processHelper: AUProcessHelper?

	private var outputBus: AUAudioUnitBus?
	private var _outputBusses: AUAudioUnitBusArray!

	private var format:AVAudioFormat

	@objc override init(componentDescription: AudioComponentDescription, options: AudioComponentInstantiationOptions) throws {
		self.format = AVAudioFormat(standardFormatWithSampleRate: 44_100, channels: 2)!
		try super.init(componentDescription: componentDescription, options: options)
		outputBus = try AUAudioUnitBus(format: self.format)
        outputBus?.maximumChannelCount = 2
		_outputBusses = AUAudioUnitBusArray(audioUnit: self, busType: AUAudioUnitBusType.output, busses: [outputBus!])
        processHelper = AUProcessHelper(&kernel)
	}

	public override var outputBusses: AUAudioUnitBusArray {
		return _outputBusses
	}

    /// No inputs; mono or stereo out. The voice is mono and is copied to every output channel.
    public override var channelCapabilities: [NSNumber] {
        return [0, 1, 0, 2]
    }
    
    public override var  maximumFramesToRender: AUAudioFrameCount {
        get {
            return kernel.maximumFramesToRender()
        }

        set {
            kernel.setMaximumFramesToRender(newValue)
        }
    }

    public override var  shouldBypassEffect: Bool {
        get {
            return kernel.isBypassed()
        }

        set {
            kernel.setBypass(newValue)
        }
    }

    // MARK: - MIDI
    public override var audioUnitMIDIProtocol: MIDIProtocolID {
        return kernel.AudioUnitMIDIProtocol()
    }

    // MARK: - Rendering
    public override var internalRenderBlock: AUInternalRenderBlock {
        return processHelper!.internalRenderBlock()
    }

    // Allocate resources required to render.
    // Subclassers should call the superclass implementation.
    public override func allocateRenderResources() throws {
		let outputChannelCount = self.outputBusses[0].format.channelCount
		
		kernel.setMusicalContextBlock(self.musicalContextBlock)
		kernel.initialize(Int32(outputChannelCount), outputBus!.format.sampleRate)

        processHelper?.setChannelCount(0, self.outputBusses[0].format.channelCount)

		try super.allocateRenderResources()
	}

    // Deallocate resources allocated in allocateRenderResourcesAndReturnError:
    // Subclassers should call the superclass implementation.
    public override func deallocateRenderResources() {
        
        // Deallocate your resources.
        kernel.deInitialize()
        
        super.deallocateRenderResources()
    }

	public func setupParameterTree(_ parameterTree: AUParameterTree) {
		self.parameterTree = parameterTree

		// Set the Parameter default values before setting up the parameter callbacks
		for param in parameterTree.allParameters {
            kernel.setParameter(param.address, param.value)
		}

		setupParameterCallbacks()
	}

	private func setupParameterCallbacks() {
		// implementorValueObserver is called when a parameter changes value.
		parameterTree?.implementorValueObserver = { [weak self] param, value -> Void in
            self?.kernel.setParameter(param.address, value)
		}

		// implementorValueProvider is called when the value needs to be refreshed.
		parameterTree?.implementorValueProvider = { [weak self] param in
            return self!.kernel.getParameter(param.address)
		}

		// A function to provide string representations of parameter values.
		parameterTree?.implementorStringFromValueCallback = { param, valuePtr in
			guard let value = valuePtr?.pointee else {
				return "-"
			}

            switch param.unit {
            case .decibels:
                return String(format: "%.1f dB", value)
            case .percent:
                return String(format: "%.0f%%", value)
            case .hertz:
                return value >= 1000 ? String(format: "%.2f kHz", value / 1000) : String(format: "%.0f Hz", value)
            case .milliseconds:
                if value >= 1000 {
                    return String(format: "%.2f s", value / 1000)
                }
                return String(format: value < 10 ? "%.1f ms" : "%.0f ms", value)
            case .relativeSemiTones:
                return String(format: "%.0f st", value)
            case .indexed:
                let index = Int(value.rounded())
                if let strings = param.valueStrings, strings.indices.contains(index) {
                    return strings[index]
                }
                return String(index)
            default:
                return String(format: "%.2f", value)
            }
		}
	}
}
