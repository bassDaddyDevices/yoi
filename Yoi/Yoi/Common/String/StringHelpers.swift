//
//  StringHelpers.swift
//  Yoi
//
//  Created by Chris Connelly on 2026-09-26.
//

import Foundation

extension String {
    var fourCharCode: FourCharCode? {
		guard self.count == 4 && self.utf8.count == 4 else {
			return nil
		}

		var code: FourCharCode = 0
		for character in self.utf8 {
			code = code << 8 + FourCharCode(character)
		}
		return code
    }
}
