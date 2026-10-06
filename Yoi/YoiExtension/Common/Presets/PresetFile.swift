//
//  PresetFile.swift
//  YoiExtension
//
//  The portable preset format, read by the Audio Unit and the VST3 alike. Shared by the whole
//  mini-synth family: nothing here knows about YOI. The format is specified in
//  YOI_DOCS/specs/preset-format.md:
//
//      { "format": "bdd-preset", "version": 1, "synth": "YOI", "number": 2, "name": "Basic Growl",
//        "parameters": { "cutoff": 408.7088, ... }, "drawing": [[x, y, bend], ...] }
//
//  Parameters are keyed by identifier and hold plain values in the parameter's own units (the
//  AUParameter's value, the VST3's plain value). A parameter the file doesn't mention takes its
//  default, so presets written before a parameter existed load with that parameter off.
//
//  Factory presets are bundle resources. The bundle copies them in flat, so they are found by
//  their contents (`format`), not by folder.
//

import Foundation
import os

private let log = Logger(subsystem: "com.bassdaddydevices.YoiExtension", category: "PresetFile")

struct PresetFile: Decodable, Sendable {
    static let formatName = "bdd-preset"
    static let formatVersion = 1

    var format: String
    var version: Int
    var synth: String
    /// Stable for the life of the product: hosts can remember a factory preset by number.
    /// 0 is Init, which is built in; files start at 1.
    var number: Int
    var name: String
    var parameters: [String: Double]
    /// `[[x, y, bend]]`, as the plug-in saves the drawing in its state. Absent means Init's.
    var drawing: [[Float]]?

    /// Every valid factory preset for `synth` in `bundle`, ordered by number. Files that are
    /// damaged, newer than this version, or for another synth are skipped and logged, so one bad
    /// file never hides the rest.
    static func factoryPresets(for synth: String, in bundle: Bundle) -> [PresetFile] {
        let urls = bundle.urls(forResourcesWithExtension: "json", subdirectory: nil) ?? []
        var byNumber: [Int: PresetFile] = [:]
        for url in urls {
            guard let data = try? Data(contentsOf: url),
                  (try? JSONDecoder().decode(FormatProbe.self, from: data))?.format == formatName else {
                continue   // some other JSON resource
            }
            let preset: PresetFile
            do {
                preset = try JSONDecoder().decode(PresetFile.self, from: data)
            } catch {
                log.error("Skipping \(url.lastPathComponent, privacy: .public): \(error.localizedDescription, privacy: .public)")
                continue
            }
            guard preset.version <= formatVersion else {
                log.error("Skipping \(url.lastPathComponent, privacy: .public): format version \(preset.version) is newer than \(formatVersion)")
                continue
            }
            guard preset.synth == synth else { continue }
            guard preset.number > 0, byNumber[preset.number] == nil else {
                log.error("Skipping \(url.lastPathComponent, privacy: .public): preset number \(preset.number) is 0 or already taken")
                continue
            }
            byNumber[preset.number] = preset
        }
        return byNumber.keys.sorted().compactMap { byNumber[$0] }
    }

    /// Just enough to tell a preset from any other JSON in the bundle.
    private struct FormatProbe: Decodable {
        var format: String?
    }
}
