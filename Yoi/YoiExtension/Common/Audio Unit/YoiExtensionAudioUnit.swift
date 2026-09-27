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
		
		// The kernel takes the host's tempo and transport blocks itself and keeps them alive;
		// passing them from Swift would hand it temporaries that are freed straight away.
		kernel.captureHostBlocks(self)
		kernel.initialize(Int32(outputChannelCount), outputBus!.format.sampleRate)

        processHelper?.setChannelCount(0, self.outputBusses[0].format.channelCount)

		try super.allocateRenderResources()
	}

    // Deallocate resources allocated in allocateRenderResourcesAndReturnError:
    // Subclassers should call the superclass implementation.
    public override func deallocateRenderResources() {
        
        // Deallocate your resources.
        kernel.releaseHostBlocks()
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
            case .octaves:
                return String(format: "%.1f oct", value)
            case .rate:
                return String(format: "%.2f\u{00D7}", value)
            case .boolean:
                return value >= 0.5 ? "On" : "Off"
            case .ratio:
                return String(format: "\u{00D7}%.1f", value)
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

    // MARK: - Drawn envelope

    /// Serialises everyone who edits the drawing (the editor, state restore). The kernel accepts
    /// one publisher at a time; the render thread never takes this lock.
    private let curveLock = NSLock()

    /// Names of the built-in drawings, in order.
    var factoryShapeNames: [String] {
        (0..<Int(YoiExtensionDSPKernel.factoryShapeCount())).map {
            String(cString: YoiExtensionDSPKernel.factoryShapeName(Int32($0)))
        }
    }

    /// Replaces the drawing with a built-in one.
    func loadFactoryShape(_ index: Int) {
        curveLock.lock()
        defer { curveLock.unlock() }
        kernel.loadFactoryShape(Int32(index))
        curveRevision &+= 1
    }

    /// Goes up by one whenever the drawing changes, from any source (editor, factory shape, state
    /// restore), so an editor can tell when to redraw.
    private(set) var curveRevision = 0

    /// The drawing as the envelope plays it, sampled at `count` points across 0...1. Editors draw
    /// this rather than working out the curve themselves.
    func envelopeTable(count: Int) -> [Float] {
        var table = [Float](repeating: 0, count: max(2, count))
        curveLock.lock()
        defer { curveLock.unlock() }
        table.withUnsafeMutableBufferPointer { buffer in
            kernel.copyEnvelopeTable(buffer.baseAddress, Int32(buffer.count))
        }
        return table
    }

    /// Where the envelope is reading in the drawing (0...1) and what it read, for a playhead.
    var envelopeDisplay: (position: Float, value: Float) {
        (kernel.envelopeDisplayPosition(), kernel.envelopeDisplayValue())
    }

    /// The drawing as points of `[x, y, bend]`: x and y are 0...1, bend is -1...1. Setting it
    /// accepts anything; the kernel cleans the points up before using them.
    var envelopeCurve: [[Float]] {
        get {
            curveLock.lock()
            defer { curveLock.unlock() }
            return (0..<Int(kernel.envelopePointCount())).map { index in
                let i = Int32(index)
                return [kernel.envelopePointX(i), kernel.envelopePointY(i), kernel.envelopePointBend(i)]
            }
        }
        set {
            let xs = newValue.map { $0.count > 0 ? $0[0] : 0 }
            let ys = newValue.map { $0.count > 1 ? $0[1] : 0.5 }
            let bends = newValue.map { $0.count > 2 ? $0[2] : 0 }
            curveLock.lock()
            defer { curveLock.unlock() }
            kernel.setEnvelopeCurve(xs, ys, bends, Int32(newValue.count))
            curveRevision &+= 1
        }
    }

    // MARK: - State

    private static let envelopeCurveStateKey = "yoiEnvelopeCurve"

    /// The parameter tree covers the knobs; the drawing is saved alongside them by hand.
    public override var fullState: [String : Any]? {
        get {
            var state = super.fullState ?? [:]
            state[Self.envelopeCurveStateKey] = envelopeCurve
            return state
        }
        set {
            super.fullState = newValue
            if let points = newValue?[Self.envelopeCurveStateKey] as? [[NSNumber]] {
                envelopeCurve = points.map { point in point.map { $0.floatValue } }
            }
        }
    }
}
