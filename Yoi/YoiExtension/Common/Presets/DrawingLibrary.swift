//
//  DrawingLibrary.swift
//  YoiExtension
//
//  The user's own drawings, kept apart from presets so one can be dropped into any patch without
//  touching anything else. Shared by the whole mini-synth family: nothing here knows about YOI.
//
//  Stored as one JSON file in the extension's Application Support folder. In the sandbox that is
//  the extension's own container, so every host on this Mac shares it (a VST3 build can't see it):
//
//      { "version": 1, "drawings": [ { "name": "Wobble", "points": [[x, y, bend], ...] } ] }
//
//  Points are the same [[x, y, bend]] the plug-in saves in its state. The layout is a persistent
//  format: whatever changes later, keep reading version 1.
//

import Foundation
import os

private let log = Logger(subsystem: "com.bassdaddydevices.YoiExtension", category: "DrawingLibrary")

@MainActor
final class DrawingLibrary {
    struct Drawing: Codable, Equatable {
        var name: String
        var points: [[Float]]
    }

    enum LibraryError: LocalizedError {
        case emptyName
        case duplicateName
        case notFound
        case newerFormat
        case unreadable

        var errorDescription: String? {
            switch self {
            case .emptyName: "Enter a name for the drawing."
            case .duplicateName: "A drawing with that name already exists."
            case .notFound: "That drawing is no longer available."
            case .newerFormat: "Your drawings were saved by a newer version, so they can't be changed here."
            case .unreadable: "Your drawings file couldn't be read, so it wasn't changed."
            }
        }
    }

    private struct File: Codable {
        var version: Int
        var drawings: [Drawing]
    }

    static let formatVersion = 1
    static let maximumNameLength = 80

    private(set) var drawings: [Drawing] = []
    let url: URL
    /// Set when the file on disk can't safely be rewritten (a newer version, or damaged), so
    /// saving never destroys drawings this version doesn't understand.
    private var writeBlocked: LibraryError?

    /// `product` names the synth's folder: Application Support/Bass Daddy Devices/<product>.
    convenience init(product: String) {
        let base = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        self.init(url: base.appending(path: "Bass Daddy Devices/\(product)/Drawings.json"))
    }

    init(url: URL) {
        self.url = url
        reload()
    }

    /// Reads the file again. Called before every change, because other instances of the plug-in
    /// (in this host or another) may have saved since.
    func reload() {
        writeBlocked = nil
        guard let data = try? Data(contentsOf: url) else {
            drawings = []   // no file yet: an empty library
            return
        }
        do {
            let file = try JSONDecoder().decode(File.self, from: data)
            drawings = file.drawings
            if file.version > Self.formatVersion {
                writeBlocked = .newerFormat
            }
        } catch {
            log.error("Drawings file unreadable: \(error.localizedDescription, privacy: .public)")
            drawings = []
            writeBlocked = .unreadable
        }
    }

    func drawing(named name: String) -> Drawing? {
        drawings.first { $0.name.caseInsensitiveCompare(name) == .orderedSame }
    }

    /// Adds a drawing under a new name. Names are unique regardless of case.
    @discardableResult
    func save(name: String, points: [[Float]]) throws -> Drawing {
        reload()
        if let writeBlocked { throw writeBlocked }
        let trimmed = String(name.trimmingCharacters(in: .whitespacesAndNewlines).prefix(Self.maximumNameLength))
        guard !trimmed.isEmpty else { throw LibraryError.emptyName }
        guard drawing(named: trimmed) == nil else { throw LibraryError.duplicateName }
        let drawing = Drawing(name: trimmed, points: points)
        drawings.append(drawing)
        try write()
        return drawing
    }

    func delete(name: String) throws {
        reload()
        if let writeBlocked { throw writeBlocked }
        guard let index = drawings.firstIndex(where: { $0.name.caseInsensitiveCompare(name) == .orderedSame }) else {
            throw LibraryError.notFound
        }
        drawings.remove(at: index)
        try write()
    }

    private func write() throws {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(File(version: Self.formatVersion, drawings: drawings))
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        try data.write(to: url, options: .atomic)
    }
}
