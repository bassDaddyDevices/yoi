//
//  TypeAliases.swift
//  Yoi
//
//  Created by Chris Connelly on 2026-09-26.
//

import CoreMIDI
import AudioToolbox

#if os(iOS) || os(visionOS)
import UIKit

public typealias ViewController = UIViewController
#elseif os(macOS)
import AppKit

public typealias KitView = NSView
public typealias ViewController = NSViewController
#endif
