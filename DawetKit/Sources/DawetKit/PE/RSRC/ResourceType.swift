//
//  ResourceType.swift
//  DawetKit
//
//  This file is part of Dawet.
//
//  Dawet is free software: you can redistribute it and/or modify it under the terms
//  of the GNU General Public License as published by the Free Software Foundation,
//  either version 3 of the License, or (at your option) any later version.
//
//  Dawet is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
//  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//  See the GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License along with Dawet.
//  If not, see https://www.gnu.org/licenses/.
//

import Foundation

/// The type of the ``ResourceDirectoryEntry``
///
/// Only applicable to ``ResourceDirectoryEntry`` with an ID
public enum ResourceType: UInt32, CaseIterable, Hashable, Equatable {
    case unknown
    // We only care about icon
    case icon = 3

    public init?(rawValue: UInt32?) {
        if let rawValue, let value = ResourceType(rawValue: rawValue) {
            self = value
        } else {
            self = .unknown
        }
    }
}
