//
//  YoiExtensionAudioUnit.swift
//  YoiExtension
//
//  Created by Chris Connelly on 2026-09-26.
//

import AVFoundation
import CxxStdlib

public class YoiExtensionAudioUnit: AUAudioUnit, @unchecked Sendable
{
	// C++ objects. Both live on the heap, allocated once, so their addresses never move: the
	// helper keeps a reference to the kernel and the render block keeps the helper's `this`.
	// Reaching them through pointers also keeps Swift's exclusivity rules for a stored value out
	// of calls that come from several threads at once (the host, the UI, the render thread).
	let kernel: UnsafeMutablePointer<YoiExtensionDSPKernel>
    private let processHelper: UnsafeMutablePointer<AUProcessHelper>

	private var outputBus: AUAudioUnitBus?
	private var _outputBusses: AUAudioUnitBusArray!

	private var format:AVAudioFormat

	@objc override init(componentDescription: AudioComponentDescription, options: AudioComponentInstantiationOptions) throws {
		self.format = AVAudioFormat(standardFormatWithSampleRate: 44_100, channels: 2)!
        kernel = UnsafeMutablePointer<YoiExtensionDSPKernel>.allocate(capacity: 1)
        kernel.initialize(to: YoiExtensionDSPKernel())
        processHelper = UnsafeMutablePointer<AUProcessHelper>.allocate(capacity: 1)
        processHelper.initialize(to: AUProcessHelper(&kernel.pointee))
		try super.init(componentDescription: componentDescription, options: options)
		outputBus = try AUAudioUnitBus(format: self.format)
        outputBus?.maximumChannelCount = 2
		_outputBusses = AUAudioUnitBusArray(audioUnit: self, busType: AUAudioUnitBusType.output, busses: [outputBus!])
        reloadLicense()
	}

    deinit {
        // The helper refers to the kernel, so it goes first.
        processHelper.deinitialize(count: 1)
        processHelper.deallocate()
        kernel.deinitialize(count: 1)
        kernel.deallocate()
    }

	public override var outputBusses: AUAudioUnitBusArray {
		return _outputBusses
	}

    /// No inputs; mono or stereo out. The voice is mono and is copied to every output channel.
    public override var channelCapabilities: [NSNumber] {
        return [0, 1, 0, 2]
    }
    
    /// The process helper's scratch buffers are sized from this value during allocation; it cannot
    /// be increased while resources are live. Clamp our storage to the superclass's accepted value.
    public override var maximumFramesToRender: AUAudioFrameCount {
        get { kernel.pointee.maximumFramesToRender() }
        set {
            guard !renderResourcesAllocated else { return }
            super.maximumFramesToRender = newValue
            kernel.pointee.setMaximumFramesToRender(super.maximumFramesToRender)
        }
    }

    public override var  shouldBypassEffect: Bool {
        get {
            return kernel.pointee.isBypassed()
        }

        set {
            kernel.pointee.setBypass(newValue)
        }
    }

    // MARK: - MIDI
    public override var audioUnitMIDIProtocol: MIDIProtocolID {
        return kernel.pointee.AudioUnitMIDIProtocol()
    }

    // MARK: - Rendering
    public override var internalRenderBlock: AUInternalRenderBlock {
        return processHelper.pointee.internalRenderBlock()
    }

    /// Hosts call this on transport stops and jumps and before a bounce. The render thread does the
    /// silencing at its next block, so nothing it is using changes under it.
    public override func reset() {
        super.reset()
        kernel.pointee.requestReset()
    }

    /// Hosts may cache this property, while Release is automatable. Report the supported maximum
    /// release (10 s) plus the kernel's tail margin so offline bounces never use a stale shorter tail.
    public override var tailTime: TimeInterval {
        10.5
    }

    // Allocate resources required to render.
    // Subclassers should call the superclass implementation.
    public override func allocateRenderResources() throws {
		let outputChannelCount = self.outputBusses[0].format.channelCount
		
		// The kernel takes the host's tempo and transport blocks itself and keeps them alive;
		// passing them from Swift would hand it temporaries that are freed straight away.
		kernel.pointee.captureHostBlocks(self)
		kernel.pointee.initialize(Int32(outputChannelCount), outputBus!.format.sampleRate)

        processHelper.pointee.setChannelCount(0, outputChannelCount, maximumFramesToRender)

		try super.allocateRenderResources()
	}

    // Deallocate resources allocated in allocateRenderResourcesAndReturnError:
    // Subclassers should call the superclass implementation.
    public override func deallocateRenderResources() {
        
        // Deallocate your resources.
        kernel.pointee.releaseHostBlocks()
        kernel.pointee.deInitialize()
        
        super.deallocateRenderResources()
    }

	public func setupParameterTree(_ parameterTree: AUParameterTree) {
		self.parameterTree = parameterTree

		// Set the Parameter default values before setting up the parameter callbacks
		for param in parameterTree.allParameters {
            kernel.pointee.setParameter(param.address, param.value)
		}

        setupParameterCallbacks()
	}

	private func setupParameterCallbacks() {
		// implementorValueObserver is called when a parameter changes value.
		parameterTree?.implementorValueObserver = { [weak self] param, value -> Void in
            self?.kernel.pointee.setParameter(param.address, value)
		}

		// implementorValueProvider is called when the value needs to be refreshed.
		parameterTree?.implementorValueProvider = { [weak self] param in
            // The tree can outlive the audio unit (a host or KVO still holding it).
            return self?.kernel.pointee.getParameter(param.address) ?? param.value
		}

		// A function to provide string representations of parameter values.
		parameterTree?.implementorStringFromValueCallback = { param, valuePtr in
			guard let value = valuePtr?.pointee else {
				return "-"
			}

            if let derived = YoiExtensionAudioUnit.derivedDisplay(identifier: param.identifier, value: value) {
                return derived
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

    // MARK: - Derived readings

    /// Parameters whose reading the plug-in owns, by identifier. OTT TIME is stored as a percent,
    /// but what it means is the compressor's release in milliseconds, and only the kernel knows
    /// that mapping. Hosts and the editor both take their text from here, so the relation is
    /// written down once (in the kernel) instead of being repeated in the page.
    static let derivedDisplayIdentifiers: Set<String> = ["ottTime"]

    static func derivedDisplay(identifier: String, value: AUValue) -> String? {
        switch identifier {
        case "ottTime":
            return "\(Int(YoiExtensionDSPKernel.ottReleaseMilliseconds(value).rounded())) ms"
        default:
            return nil
        }
    }

    /// The filter as the last rendered block set it: the cutoff in hertz with the drawing applied,
    /// its Q, and the two ends of the range the drawing sweeps. Editors draw these instead of
    /// re-deriving the cutoff relation or the RES remap.
    var filterDisplay: (cutoffHertz: Float, q: Float, topHertz: Float, bottomHertz: Float) {
        (kernel.pointee.filterDisplayCutoffHertz(), kernel.pointee.filterDisplayQ(),
         kernel.pointee.filterDisplayTopHertz(), kernel.pointee.filterDisplayBottomHertz())
    }

    /// The output meters: each channel's held peak after the limiter, linear (1 is 0 dBFS).
    var outputMeters: (left: Float, right: Float) {
        (kernel.pointee.outputMeterLeft(), kernel.pointee.outputMeterRight())
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
        kernel.pointee.loadFactoryShape(Int32(index))
        _curveRevision &+= 1
    }

    /// Goes up by one whenever the drawing changes, from any source (editor, factory shape, state
    /// restore), so an editor can tell when to redraw.
    private var _curveRevision = 0
    var curveRevision: Int {
        curveLock.lock()
        defer { curveLock.unlock() }
        return _curveRevision
    }

    /// The drawing as the envelope plays it, sampled at `count` points across 0...1. Editors draw
    /// this rather than working out the curve themselves.
    func envelopeTable(count: Int) -> [Float] {
        var table = [Float](repeating: 0, count: max(2, count))
        curveLock.lock()
        defer { curveLock.unlock() }
        table.withUnsafeMutableBufferPointer { buffer in
            kernel.pointee.copyEnvelopeTable(buffer.baseAddress, Int32(buffer.count))
        }
        return table
    }

    /// Where the envelope is reading in the drawing (0...1) and what it read, for a playhead.
    var envelopeDisplay: (position: Float, value: Float) {
        (kernel.pointee.envelopeDisplayPosition(), kernel.pointee.envelopeDisplayValue())
    }

    /// The drawing as points of `[x, y, bend]`: x and y are 0...1, bend is -1...1. Setting it
    /// accepts anything; the kernel cleans the points up before using them.
    var envelopeCurve: [[Float]] {
        get {
            curveLock.lock()
            defer { curveLock.unlock() }
            return (0..<Int(kernel.pointee.envelopePointCount())).map { index in
                let i = Int32(index)
                return [kernel.pointee.envelopePointX(i), kernel.pointee.envelopePointY(i), kernel.pointee.envelopePointBend(i)]
            }
        }
        set {
            let xs = newValue.map { $0.count > 0 ? $0[0] : 0 }
            let ys = newValue.map { $0.count > 1 ? $0[1] : 0.5 }
            let bends = newValue.map { $0.count > 2 ? $0[2] : 0 }
            curveLock.lock()
            defer { curveLock.unlock() }
            kernel.pointee.setEnvelopeCurve(xs, ys, bends, Int32(newValue.count))
            _curveRevision &+= 1
        }
    }

    // MARK: - Licensing
    // YOI_DOCS/decisions/licensing.md. The shared C++ store checks and keeps licenses, the same as
    // the VST3; this only says where (the extension's own container) and tells the kernel.

    static let productName = "YOI"
    static let productMajor: Int32 = 1

    /// Application Support/Bass Daddy Devices/Licenses, inside the extension's sandbox container.
    private static var licensesFolder: String {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        return base.appendingPathComponent("Bass Daddy Devices/Licenses", isDirectory: true).path
    }

    /// The license this copy runs under.
    private(set) var license = bdd.license.License()

    func reloadLicense() {
        license = bdd.license.loadFromFolder(std.string(Self.licensesFolder), std.string(Self.productName), Self.productMajor)
        kernel.pointee.setLicensed(bdd.license.unlocks(license))
    }

    /// Checks a pasted license and, if it's valid, keeps it and plays in full. Returns whether it
    /// worked and what to tell the user.
    func installLicense(_ token: String) -> (valid: Bool, message: String) {
        let result = bdd.license.installInFolder(std.string(Self.licensesFolder), std.string(token),
                                                 std.string(Self.productName), Self.productMajor)
        guard result.isValid() else {
            return (false, String(cString: bdd.license.describe(result.status)))
        }
        reloadLicense()
        let name = String(result.licensee).isEmpty ? String(result.email) : String(result.licensee)
        return (true, "Licensed to \(name). Thank you!")
    }

    /// What the editor's LICENSE page shows.
    var licenseState: [String: Any] {
        [
            "product": Self.productName,
            "enforced": bdd.license.kLicensingEnforced,
            "licensed": license.isValid(),
            "message": String(cString: bdd.license.describe(license.status)),
            "licensee": String(license.licensee),
            "email": String(license.email),
        ]
    }

    // MARK: - State

    private static let envelopeCurveStateKey = "yoiEnvelopeCurve"
    private static let stateVersionKey = "yoiStateVersion"

    public override var currentPreset: AUAudioUnitPreset? {
        get { super.currentPreset }
        set {
            guard let preset = newValue else {
                super.currentPreset = nil
                return
            }
            if preset.number >= 0 {
                guard (factoryPresets ?? []).contains(where: { $0.number == preset.number }) else { return }
                // Only a change of selection loads it: a host re-applying the selection it already
                // has (after restoring fullState, say) must not throw away the user's edits.
                if super.currentPreset?.number != preset.number {
                    loadFactoryPreset(number: preset.number)
                }
                super.currentPreset = preset
                return
            }
            guard let state = try? presetState(for: preset) else { return }
            super.fullState = state
            restoreEnvelopeCurve(from: state)
            super.currentPreset = preset
        }
    }

    /// Choosing a factory preset in the editor always reloads it, even if it was already selected,
    /// so it doubles as "revert to the factory version".
    func selectPresetFromEditor(_ preset: AUAudioUnitPreset) {
        guard preset.number >= 0,
              (factoryPresets ?? []).contains(where: { $0.number == preset.number }) else {
            currentPreset = preset
            return
        }
        loadFactoryPreset(number: preset.number)
        super.currentPreset = preset
    }

    public override var supportsUserPresets: Bool { true }

    /// The factory presets shipped in the extension (Presets/Factory), read once per process.
    /// `Bundle(for:)` rather than `.main`, which is the host's bundle when a host loads the
    /// extension in-process.
    private static let factoryPresetFiles = PresetFile.factoryPresets(for: "YOI", in: Bundle(for: YoiExtensionAudioUnit.self))

    /// Init (number 0, built in: every default and Init's drawing), then the shipped presets.
    public override var factoryPresets: [AUAudioUnitPreset]? {
        let initPreset = AUAudioUnitPreset()
        initPreset.number = 0
        initPreset.name = "Init"
        return [initPreset] + Self.factoryPresetFiles.map { file in
            let preset = AUAudioUnitPreset()
            preset.number = file.number
            preset.name = file.name
            return preset
        }
    }

    /// Defaults first, so anything the file doesn't mention (a parameter newer than the preset)
    /// is at its default; then the file's values, clamped to each parameter's range.
    private func loadFactoryPreset(number: Int) {
        resetToDefaults()
        guard number != 0, let file = Self.factoryPresetFiles.first(where: { $0.number == number }) else { return }
        for parameter in parameterTree?.allParameters ?? [] {
            guard let value = file.parameters[parameter.identifier], value.isFinite else { continue }
            parameter.value = min(max(AUValue(value), parameter.minValue), parameter.maxValue)
        }
        if let drawing = file.drawing {
            envelopeCurve = drawing
        }
    }

    /// The parameter tree covers the knobs; the drawn envelope is saved alongside them.
    public override var fullState: [String : Any]? {
        get {
            var state = super.fullState ?? [:]
            state[Self.envelopeCurveStateKey] = envelopeCurve
            state[Self.stateVersionKey] = 1
            return state
        }
        set {
            super.fullState = newValue
            guard let newValue else {
                loadFactoryShape(0)
                return
            }
            restoreEnvelopeCurve(from: newValue)
        }
    }

    /// Restore only the persistent curve format we understand. A missing curve in an older
    /// state starts from Init; a present but malformed or newer-version curve leaves the current
    /// drawing intact rather than silently destroying it.
    private func restoreEnvelopeCurve(from state: [String: Any]) {
        let rawVersion = state[Self.stateVersionKey] as? NSNumber
        let version = rawVersion?.intValue ?? 1
        guard version == 1 else { return }
        guard let rawPoints = state[Self.envelopeCurveStateKey] else {
            loadFactoryShape(0)
            return
        }
        if let rows = rawPoints as? [[NSNumber]], rows.allSatisfy({ $0.count == 3 }) {
            envelopeCurve = rows.map { $0.map(\.floatValue) }
            return
        }
        guard let rows = rawPoints as? [[Any]],
              rows.allSatisfy({ $0.count == 3 && $0.allSatisfy { $0 is NSNumber } }) else {
            return
        }
        envelopeCurve = rows.map { row in row.map { ($0 as! NSNumber).floatValue } }
    }

    // MARK: - Presets

    /// Guards `lastUserPresetNumber`, so two instances saving a preset at the same time can't
    /// claim the same number.
    private static let presetNumberLock = NSLock()

    /// The next user preset number to hand out. User preset numbers are negative and count
    /// downwards; -1 is the first one.
    nonisolated(unsafe) private static var lastUserPresetNumber = -1

    public var presetUserPresets: [[String: Any]] {
        userPresets.map { ["number": $0.number, "name": $0.name, "kind": "user"] }
    }

    public var presetFactoryPresets: [[String: Any]] {
        (factoryPresets ?? []).map { ["number": $0.number, "name": $0.name, "kind": "factory"] }
    }

    public var presetCurrentPreset: [String: Any]? {
        guard let preset = currentPreset else { return nil }
        return ["number": preset.number, "name": preset.name, "kind": preset.number < 0 ? "user" : "factory"]
    }

    public func createUserPreset(named name: String) throws -> AUAudioUnitPreset {
        let trimmedName = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmedName.isEmpty else { throw PresetError.emptyName }
        guard !userPresets.contains(where: { $0.name.caseInsensitiveCompare(trimmedName) == .orderedSame }) else {
            throw PresetError.duplicateName
        }
        Self.presetNumberLock.lock()
        let minimumNumber = userPresets.map(\.number).filter { $0 < 0 }.min() ?? 0
        let number = min(Self.lastUserPresetNumber, minimumNumber - 1)
        let previousNextNumber = Self.lastUserPresetNumber
        Self.lastUserPresetNumber = number - 1
        Self.presetNumberLock.unlock()

        let preset = AUAudioUnitPreset()
        preset.number = number
        preset.name = trimmedName
        let previousPreset = super.currentPreset
        super.currentPreset = preset
        do {
            try saveUserPreset(preset)
            super.currentPreset = preset
            return preset
        } catch {
            super.currentPreset = previousPreset
            Self.presetNumberLock.lock()
            Self.lastUserPresetNumber = previousNextNumber
            Self.presetNumberLock.unlock()
            throw error
        }
    }

    public func removeUserPreset(number: Int) throws {
        guard let preset = userPresets.first(where: { $0.number == number }) else { throw PresetError.notFound }
        try deleteUserPreset(preset)
        if currentPreset?.number == number { currentPreset = nil }
    }

    private func resetToDefaults() {
        guard let parameterTree else { return }
        for parameter in parameterTree.allParameters {
            if let value = YoiExtensionParameterSpecs.defaultValues[parameter.address] {
                parameter.value = value
            }
        }
        loadFactoryShape(0)
    }

    public enum PresetError: Error {
        case emptyName
        case duplicateName
        case notFound
    }
}
