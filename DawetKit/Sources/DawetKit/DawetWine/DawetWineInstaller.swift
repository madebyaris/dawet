//
//  DawetWineInstaller.swift
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
import SemanticVersion

public class DawetWineInstaller {
    /// The Dawet application folder
    public static let applicationFolder = FileManager.default.urls(
        for: .applicationSupportDirectory, in: .userDomainMask
        )[0].appending(path: Bundle.dawetBundleIdentifier)

    /// The folder of all the library files
    public static let libraryFolder = applicationFolder.appending(path: "Libraries")

    /// URL to the installed `wine` `bin` directory
    public static let binFolder: URL = libraryFolder.appending(path: "Wine").appending(path: "bin")

    /// Path to locally built Wine (from source)
    /// Checks for DAWET_SOURCE_ROOT environment variable first, then looks in common locations
    private static var localWinePath: URL {
        // Check environment variable (set by build scripts or Xcode)
        if let sourceRoot = ProcessInfo.processInfo.environment["DAWET_SOURCE_ROOT"],
           !sourceRoot.isEmpty {
            let path = URL(fileURLWithPath: sourceRoot).appending(path: "Libraries/wine/install")
            if FileManager.default.fileExists(atPath: path.appending(path: "bin/wine64").path) ||
               FileManager.default.fileExists(atPath: path.appending(path: "bin/wine").path) {
                return path
            }
        }

        // Try absolute path from project root (for Xcode builds)
        // Get the source root from the source file location
        let sourceFile = #file
        let sourceURL = URL(fileURLWithPath: sourceFile)
        // Navigate from DawetKit/Sources/DawetKit/DawetWine/DawetWineInstaller.swift to project root
        var projectRoot = sourceURL
            .deletingLastPathComponent() // DawetWine
            .deletingLastPathComponent() // DawetKit
            .deletingLastPathComponent() // Sources
            .deletingLastPathComponent() // DawetKit
            .deletingLastPathComponent() // project root

        var librariesPath = projectRoot.appending(path: "Libraries/wine/install")
        if FileManager.default.fileExists(atPath: librariesPath.appending(path: "bin/wine64").path) ||
           FileManager.default.fileExists(atPath: librariesPath.appending(path: "bin/wine").path) {
            return librariesPath
        }

        // Try relative to app bundle (for development builds)
        let bundlePath = Bundle.main.bundlePath
        let appURL = URL(fileURLWithPath: bundlePath)

        // Navigate up from .app/Contents/MacOS/Dawet to find project root
        var currentPath = appURL.deletingLastPathComponent() // MacOS
        currentPath = currentPath.deletingLastPathComponent() // Contents
        currentPath = currentPath.deletingLastPathComponent() // .app
        currentPath = currentPath.deletingLastPathComponent() // Build/Products or project root

        // Check if Libraries exists here
        librariesPath = currentPath.appending(path: "Libraries/wine/install")
        if FileManager.default.fileExists(atPath: librariesPath.appending(path: "bin/wine64").path) ||
           FileManager.default.fileExists(atPath: librariesPath.appending(path: "bin/wine").path) {
            return librariesPath
        }

        // Try one more level up (for Xcode DerivedData structure)
        currentPath = currentPath.deletingLastPathComponent()
        librariesPath = currentPath.appending(path: "Libraries/wine/install")
        if FileManager.default.fileExists(atPath: librariesPath.appending(path: "bin/wine64").path) ||
           FileManager.default.fileExists(atPath: librariesPath.appending(path: "bin/wine").path) {
            return librariesPath
        }

        // Try current working directory (for command-line usage)
        let cwd = FileManager.default.currentDirectoryPath
        if !cwd.isEmpty {
            let cwdURL = URL(fileURLWithPath: cwd)
            librariesPath = cwdURL.appending(path: "Libraries/wine/install")
            if FileManager.default.fileExists(atPath: librariesPath.appending(path: "bin/wine64").path) ||
               FileManager.default.fileExists(atPath: librariesPath.appending(path: "bin/wine").path) {
                return librariesPath
            }
        }

        // Default fallback (will be checked by hasLocalBuild)
        return URL(fileURLWithPath: "/tmp/Libraries/wine/install")
    }

    /// Check if locally built Wine is available
    public static var hasLocalBuild: Bool {
        // Check for wine64 first (preferred), then wine
        let wine64Binary = localWinePath.appending(path: "bin/wine64")
        let wineBinary = localWinePath.appending(path: "bin/wine")
        return FileManager.default.fileExists(atPath: wine64Binary.path) ||
               FileManager.default.fileExists(atPath: wineBinary.path)
    }

    /// Get the appropriate Wine bin folder (local build or installed)
    public static var effectiveBinFolder: URL {
        if hasLocalBuild {
            return localWinePath.appending(path: "bin")
        }
        return binFolder
    }

    public static func isDawetWineInstalled() -> Bool {
        // Check local build first
        if hasLocalBuild {
            return true
        }
        // Fall back to checking installed version
        return dawetWineVersion() != nil
    }

    public static func install(from: URL) {
        do {
            if !FileManager.default.fileExists(atPath: applicationFolder.path) {
                try FileManager.default.createDirectory(at: applicationFolder, withIntermediateDirectories: true)
            } else {
                // Recreate it
                try FileManager.default.removeItem(at: applicationFolder)
                try FileManager.default.createDirectory(at: applicationFolder, withIntermediateDirectories: true)
            }

            try Tar.untar(tarBall: from, toURL: applicationFolder)
            try FileManager.default.removeItem(at: from)
        } catch {
            print("Failed to install DawetWine: \(error)")
        }
    }

    public static func uninstall() {
        do {
            try FileManager.default.removeItem(at: libraryFolder)
        } catch {
            print("Failed to uninstall DawetWine: \(error)")
        }
    }

    public static func shouldUpdateDawetWine() async -> (Bool, SemanticVersion) {
        let versionPlistURL = "https://data.getwhisky.app/Wine/DawetWineVersion.plist"
        let localVersion = dawetWineVersion()

        var remoteVersion: SemanticVersion?

        if let remoteUrl = URL(string: versionPlistURL) {
            remoteVersion = await withCheckedContinuation { continuation in
                URLSession(configuration: .ephemeral).dataTask(with: URLRequest(url: remoteUrl)) { data, _, error in
                    do {
                        if error == nil, let data = data {
                            let decoder = PropertyListDecoder()
                            let remoteInfo = try decoder.decode(DawetWineVersion.self, from: data)
                            let remoteVersion = remoteInfo.version

                            continuation.resume(returning: remoteVersion)
                            return
                        }
                        if let error = error {
                            print(error)
                        }
                    } catch {
                        print(error)
                    }

                    continuation.resume(returning: nil)
                }.resume()
            }
        }

        if let localVersion = localVersion, let remoteVersion = remoteVersion {
            if localVersion < remoteVersion {
                return (true, remoteVersion)
            }
        }

        return (false, SemanticVersion(0, 0, 0))
    }

    public static func dawetWineVersion() -> SemanticVersion? {
        // Check local build first
        if hasLocalBuild {
            let localVersionPlist = localWinePath
                .appending(path: "DawetWineVersion")
                .appendingPathExtension("plist")

            if FileManager.default.fileExists(atPath: localVersionPlist.path) {
                do {
                    let decoder = PropertyListDecoder()
                    let data = try Data(contentsOf: localVersionPlist)
                    let info = try decoder.decode(DawetWineVersion.self, from: data)
                    return info.version
                } catch {
                    print("Failed to read local version: \(error)")
                }
            }
            // If no version plist, assume Wine 10.0 from local build
            return SemanticVersion(10, 0, 0)
        }

        // Fall back to installed version
        do {
            let versionPlist = libraryFolder
                .appending(path: "DawetWineVersion")
                .appendingPathExtension("plist")

            let decoder = PropertyListDecoder()
            let data = try Data(contentsOf: versionPlist)
            let info = try decoder.decode(DawetWineVersion.self, from: data)
            return info.version
        } catch {
            print(error)
            return nil
        }
    }

    /// Copy locally built Wine to runtime location if available
    public static func installLocalBuild() throws {
        guard hasLocalBuild else {
            throw NSError(domain: "DawetWineInstaller", code: 1,
                         userInfo: [NSLocalizedDescriptionKey: "Local build not available"])
        }

        // Create library folder if needed
        if !FileManager.default.fileExists(atPath: libraryFolder.path) {
            try FileManager.default.createDirectory(at: libraryFolder, withIntermediateDirectories: true)
        }

        // Copy Wine binaries and libraries
        // Wine installs to prefix/bin, prefix/lib, etc.
        // We need to copy the entire installation structure
        let wineDest = libraryFolder.appending(path: "Wine")
        if FileManager.default.fileExists(atPath: wineDest.path) {
            try FileManager.default.removeItem(at: wineDest)
        }

        // Create Wine directory structure
        try FileManager.default.createDirectory(at: wineDest, withIntermediateDirectories: true)

        // Copy bin directory
        let binSource = localWinePath.appending(path: "bin")
        let binDest = wineDest.appending(path: "bin")
        if FileManager.default.fileExists(atPath: binSource.path) {
            try FileManager.default.copyItem(at: binSource, to: binDest)
        }

        // Copy lib directory if it exists
        let libSource = localWinePath.appending(path: "lib")
        let libDest = wineDest.appending(path: "lib")
        if FileManager.default.fileExists(atPath: libSource.path) {
            try FileManager.default.copyItem(at: libSource, to: libDest)
        }

        // Copy share directory if it exists (for Wine data files)
        let shareSource = localWinePath.appending(path: "share")
        let shareDest = wineDest.appending(path: "share")
        if FileManager.default.fileExists(atPath: shareSource.path) {
            try FileManager.default.copyItem(at: shareSource, to: shareDest)
        }

        // Copy version plist
        let versionSource = localWinePath.appending(path: "DawetWineVersion.plist")
        let versionDest = libraryFolder.appending(path: "DawetWineVersion.plist")
        if FileManager.default.fileExists(atPath: versionSource.path) {
            if FileManager.default.fileExists(atPath: versionDest.path) {
                try FileManager.default.removeItem(at: versionDest)
            }
            try FileManager.default.copyItem(at: versionSource, to: versionDest)
        }
    }
}

struct DawetWineVersion: Codable {
    var version: SemanticVersion = SemanticVersion(10, 0, 0) // Default to Wine 10.0
}

// Version constants for CrossOver components
public struct DawetComponentVersions {
    public static let wineVersion = SemanticVersion(10, 0, 0)
    public static let dxvkVersion = SemanticVersion(1, 10, 3)
    public static let moltenVKVersion = SemanticVersion(1, 2, 0) // Approximate, check actual version
}
