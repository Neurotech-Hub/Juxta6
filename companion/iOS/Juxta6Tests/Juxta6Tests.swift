//
//  Juxta6Tests.swift
//  Juxta6Tests
//

import Foundation
import Testing
@testable import Juxta6

struct Juxta6Tests {

    @Test func packageZipUsesDeviceDirectory() throws {
        let directory = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }

        func csv(_ name: String, _ text: String) throws -> URL {
            let url = directory.appendingPathComponent(name)
            try text.write(to: url, atomically: true, encoding: .utf8)
            return url
        }

        let day1 = DailyPackage(
            dateKey: "20260507",
            deviceID: "JX_A1B2C3",
            files: [
                "JXV20260507.csv": try csv("JXV20260507.csv", "sec,motion\n"),
                "JXS20260507.csv": try csv("JXS20260507.csv", "unix,event\n")
            ]
        )
        let day2 = DailyPackage(
            dateKey: "20260508",
            deviceID: "JX_A1B2C3",
            files: [
                "JXV20260508.csv": try csv("JXV20260508.csv", "sec,motion\n1,0\n")
            ]
        )

        let zip = try PackageZip.write(deviceID: "JX_A1B2C3", packages: [day2, day1])
        defer { try? FileManager.default.removeItem(at: zip.deletingLastPathComponent()) }

        #expect(zip.lastPathComponent == "JX_A1B2C3.zip")

        let names = try PackageZip.entryNames(at: zip)
        #expect(names == [
            "JX_A1B2C3/JXS20260507.csv",
            "JX_A1B2C3/JXV20260507.csv",
            "JX_A1B2C3/JXV20260508.csv"
        ])
        let stored = try Data(contentsOf: zip)
        #expect(stored.range(of: Data("sec,motion\n1,0\n".utf8)) != nil)
    }

}
