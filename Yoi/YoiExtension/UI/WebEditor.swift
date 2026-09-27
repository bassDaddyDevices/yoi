//
//  WebEditor.swift
//  YoiExtension
//
//  The plug-in's editor: an HTML/CSS/JS page (the WebUI folder, bundled with the extension) shown
//  in a WKWebView, and the bridge between that page and the audio unit.
//
//  The same page is used by the VST3 build (through choc's web view), so everything the page
//  needs goes through the small message protocol below rather than anything Audio Unit specific.
//
//  Page -> plug-in, via window.webkit.messageHandlers.bdd.postMessage({ type, ... }):
//      hello                      the page is ready: send the full state
//      beginEdit { id }           a gesture starts on parameter `id` (its address)
//      edit { id, value }         the gesture moves it
//      endEdit { id }             the gesture ends
//      setCurve { points }        a new drawing, as [[x, y, bend]]
//      loadShape { index }        load a factory drawing
//
//  Plug-in -> page, by calling window.bdd.receive(state), where `state` has any of:
//      descriptor                 every parameter's address, name, group, range, unit and options,
//                                 and the factory drawing names (sent once, after hello)
//      params { id: value }       parameter values: all of them after hello, then only changes
//      curve { points, table }    the drawing's points and the curve as the envelope plays it
//      display { position, value } the envelope's playhead, about 30 times a second
//

import AudioToolbox
import WebKit
import os

private let log = Logger(subsystem: "com.bassdaddydevices.YoiExtension", category: "WebEditor")

@MainActor
final class WebEditor: NSObject, WKScriptMessageHandler, WKNavigationDelegate {
    /// Points the page draws the curve with.
    private static let tableSize = 256
    private static let updateInterval: TimeInterval = 1.0 / 30.0

    let webView: WKWebView
    private weak var audioUnit: YoiExtensionAudioUnit?
    private var observerToken: AUParameterObserverToken?
    private var changedAddresses = Set<AUParameterAddress>()
    private var timer: Timer?
    private var pageReady = false
    private var sentCurveRevision = -1

    init(audioUnit: YoiExtensionAudioUnit) {
        self.audioUnit = audioUnit

        let configuration = WKWebViewConfiguration()
        let contentController = WKUserContentController()
        configuration.userContentController = contentController
        webView = WKWebView(frame: .zero, configuration: configuration)
        super.init()

        // The content controller keeps its handlers alive, so hand it a weak stand-in to avoid a
        // cycle that would keep this editor (and the page) around after the window closes.
        contentController.add(WeakMessageHandler(self), name: "bdd")
        webView.navigationDelegate = self
        webView.underPageBackgroundColor = .black
        #if DEBUG
        webView.isInspectable = true   // Safari > Develop to inspect the page inside a host
        #endif

        observeParameters()
        loadPage()
    }

    /// Stops the updates and detaches from the audio unit. Call when the view goes away.
    func invalidate() {
        timer?.invalidate()
        timer = nil
        if let token = observerToken {
            audioUnit?.parameterTree?.removeParameterObserver(token)
            observerToken = nil
        }
        webView.configuration.userContentController.removeScriptMessageHandler(forName: "bdd")
    }

    /// Stops the regular updates while the editor is hidden, and restarts them when it's shown.
    func pause() {
        timer?.invalidate()
        timer = nil
    }

    func resume() {
        if pageReady {
            sendFullState()
            startUpdates()
        }
    }

    // MARK: - Loading

    private func loadPage() {
        let bundle = Bundle(for: WebEditor.self)
        guard let page = bundle.url(forResource: "index", withExtension: "html") else {
            log.error("index.html is missing from the extension's resources")
            return
        }
        webView.loadFileURL(page, allowingReadAccessTo: page.deletingLastPathComponent())
    }

    func webView(_ webView: WKWebView, didFail navigation: WKNavigation!, withError error: Error) {
        log.error("Editor page failed: \(error.localizedDescription, privacy: .public)")
    }

    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        log.error("Editor page failed to load: \(error.localizedDescription, privacy: .public)")
    }

    // MARK: - Page -> plug-in

    func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
        guard let body = message.body as? [String: Any], let type = body["type"] as? String,
              let audioUnit else {
            return
        }

        switch type {
        case "hello":
            log.info("Editor page connected")
            pageReady = true
            sendFullState()
            startUpdates()

        case "beginEdit", "edit", "endEdit":
            guard let parameter = parameter(for: body) else { return }
            let value = (body["value"] as? NSNumber)?.floatValue ?? parameter.value
            let eventType: AUParameterAutomationEventType =
                type == "beginEdit" ? .touch : (type == "endEdit" ? .release : .value)
            parameter.setValue(value, originator: observerToken, atHostTime: 0, eventType: eventType)

        case "setCurve":
            guard let points = body["points"] as? [[NSNumber]] else { return }
            audioUnit.envelopeCurve = points.map { point in point.map(\.floatValue) }
            sendCurve()

        case "loadShape":
            guard let index = (body["index"] as? NSNumber)?.intValue else { return }
            audioUnit.loadFactoryShape(index)
            sendCurve()

        default:
            log.debug("Unknown editor message \(type, privacy: .public)")
        }
    }

    private func parameter(for body: [String: Any]) -> AUParameter? {
        guard let id = (body["id"] as? NSNumber)?.uint64Value else { return nil }
        return audioUnit?.parameterTree?.parameter(withAddress: AUParameterAddress(id))
    }

    // MARK: - Plug-in -> page

    private func observeParameters() {
        // Changes the page made itself carry this token, so they don't echo back.
        observerToken = audioUnit?.parameterTree?.token(byAddingParameterObserver: { [weak self] address, _ in
            DispatchQueue.main.async {
                self?.changedAddresses.insert(address)
            }
        })
    }

    private func startUpdates() {
        guard timer == nil else { return }
        timer = Timer.scheduledTimer(withTimeInterval: Self.updateInterval, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated {
                self?.sendUpdates()
            }
        }
    }

    private func sendFullState() {
        guard let audioUnit, let tree = audioUnit.parameterTree else { return }
        var values: [String: Any] = [:]
        let parameters: [[String: Any]] = tree.allParameters.map { parameter in
            values[String(parameter.address)] = parameter.value
            var entry: [String: Any] = [
                "id": parameter.address,
                "identifier": parameter.identifier,
                "name": parameter.displayName,
                "group": parameter.keyPath.split(separator: ".").first.map(String.init) ?? "",
                "min": parameter.minValue,
                "max": parameter.maxValue,
                "unit": Self.unitName(parameter.unit),
                "log": parameter.flags.contains(.flag_DisplayLogarithmic),
            ]
            if let strings = parameter.valueStrings {
                entry["options"] = strings
            }
            return entry
        }
        changedAddresses.removeAll()
        send([
            "descriptor": ["parameters": parameters, "shapes": audioUnit.factoryShapeNames],
            "params": values,
            "curve": curveState(),
        ])
        sentCurveRevision = audioUnit.curveRevision
    }

    private func sendCurve() {
        guard let audioUnit else { return }
        send(["curve": curveState()])
        sentCurveRevision = audioUnit.curveRevision
    }

    private func sendUpdates() {
        guard pageReady, let audioUnit else { return }
        var state: [String: Any] = [:]

        if !changedAddresses.isEmpty, let tree = audioUnit.parameterTree {
            var values: [String: Any] = [:]
            for address in changedAddresses {
                if let parameter = tree.parameter(withAddress: address) {
                    values[String(address)] = parameter.value
                }
            }
            changedAddresses.removeAll()
            state["params"] = values
        }
        if audioUnit.curveRevision != sentCurveRevision {
            state["curve"] = curveState()
            sentCurveRevision = audioUnit.curveRevision
        }
        let display = audioUnit.envelopeDisplay
        state["display"] = ["position": display.position, "value": display.value]
        send(state)
    }

    private func curveState() -> [String: Any] {
        guard let audioUnit else { return [:] }
        return ["points": audioUnit.envelopeCurve, "table": audioUnit.envelopeTable(count: Self.tableSize)]
    }

    private func send(_ state: [String: Any]) {
        guard pageReady,
              let data = try? JSONSerialization.data(withJSONObject: state),
              let json = String(data: data, encoding: .utf8) else {
            return
        }
        webView.evaluateJavaScript("window.bdd && window.bdd.receive(\(json))", completionHandler: nil)
    }

    private static func unitName(_ unit: AudioUnitParameterUnit) -> String {
        switch unit {
        case .hertz: return "hz"
        case .milliseconds: return "ms"
        case .percent: return "percent"
        case .decibels: return "db"
        case .octaves: return "octaves"
        case .ratio: return "ratio"
        case .rate: return "rate"
        case .relativeSemiTones: return "semitones"
        case .indexed: return "indexed"
        case .boolean: return "boolean"
        default: return "generic"
        }
    }
}

/// Forwards script messages to an editor without keeping it alive.
private final class WeakMessageHandler: NSObject, WKScriptMessageHandler {
    weak var target: WKScriptMessageHandler?

    init(_ target: WKScriptMessageHandler) {
        self.target = target
    }

    func userContentController(_ userContentController: WKUserContentController, didReceive message: WKScriptMessage) {
        target?.userContentController(userContentController, didReceive: message)
    }
}
