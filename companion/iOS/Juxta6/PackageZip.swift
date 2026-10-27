import Foundation

enum PackageZipError: LocalizedError {
    case noFiles
    case unreadable

    var errorDescription: String? {
        switch self {
        case .noFiles:
            return "Nothing to transfer."
        case .unreadable:
            return "Could not read the transfer archive."
        }
    }
}

/// Store-method zip written at share time. The archive has one top-level folder, the device id.
enum PackageZip {
    static func write(deviceID: String, packages: [DailyPackage]) throws -> URL {
        var entries: [(name: String, data: Data)] = []
        let sorted = packages.sorted { lhs, rhs in
            if lhs.dateKey != rhs.dateKey { return lhs.dateKey < rhs.dateKey }
            return lhs.deviceID < rhs.deviceID
        }
        for pkg in sorted {
            for (filename, url) in pkg.files.sorted(by: { $0.key < $1.key }) {
                let data = try Data(contentsOf: url)
                entries.append(("\(deviceID)/\(filename)", data))
            }
        }
        guard !entries.isEmpty else { throw PackageZipError.noFiles }

        let folder = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let url = folder.appendingPathComponent("\(deviceID).zip")
        try storeZip(entries: entries).write(to: url, options: .atomic)
        return url
    }

    /// Central-directory names, in archive order. Used to check the export layout.
    static func entryNames(at url: URL) throws -> [String] {
        let data = try Data(contentsOf: url)
        guard data.count >= 22 else { throw PackageZipError.unreadable }
        let eocd = data.count - 22
        guard readUInt32(data, eocd) == 0x06054b50 else { throw PackageZipError.unreadable }
        let count = Int(readUInt16(data, eocd + 8))
        var cursor = Int(readUInt32(data, eocd + 16))
        var names: [String] = []
        names.reserveCapacity(count)
        for _ in 0..<count {
            guard cursor + 46 <= data.count, readUInt32(data, cursor) == 0x02014b50 else {
                throw PackageZipError.unreadable
            }
            let nameLength = Int(readUInt16(data, cursor + 28))
            let extraLength = Int(readUInt16(data, cursor + 30))
            let commentLength = Int(readUInt16(data, cursor + 32))
            let nameStart = cursor + 46
            let nameEnd = nameStart + nameLength
            guard nameEnd <= data.count else { throw PackageZipError.unreadable }
            names.append(String(decoding: data[nameStart..<nameEnd], as: UTF8.self))
            cursor = nameEnd + extraLength + commentLength
        }
        return names
    }

    private static func storeZip(entries: [(name: String, data: Data)]) -> Data {
        let (dosTime, dosDate) = dosTimestamp(Date())
        var local = Data()
        var central = Data()
        var offsets: [UInt32] = []

        for entry in entries {
            let nameBytes = Data(entry.name.utf8)
            let crc = ZipCRC32.checksum(entry.data)
            let size = UInt32(entry.data.count)
            offsets.append(UInt32(local.count))

            appendUInt32(0x04034b50, to: &local)
            appendUInt16(20, to: &local)
            appendUInt16(0x0800, to: &local)
            appendUInt16(0, to: &local)
            appendUInt16(dosTime, to: &local)
            appendUInt16(dosDate, to: &local)
            appendUInt32(crc, to: &local)
            appendUInt32(size, to: &local)
            appendUInt32(size, to: &local)
            appendUInt16(UInt16(nameBytes.count), to: &local)
            appendUInt16(0, to: &local)
            local.append(nameBytes)
            local.append(entry.data)

            appendUInt32(0x02014b50, to: &central)
            appendUInt16(20, to: &central)
            appendUInt16(20, to: &central)
            appendUInt16(0x0800, to: &central)
            appendUInt16(0, to: &central)
            appendUInt16(dosTime, to: &central)
            appendUInt16(dosDate, to: &central)
            appendUInt32(crc, to: &central)
            appendUInt32(size, to: &central)
            appendUInt32(size, to: &central)
            appendUInt16(UInt16(nameBytes.count), to: &central)
            appendUInt16(0, to: &central)
            appendUInt16(0, to: &central)
            appendUInt16(0, to: &central)
            appendUInt16(0, to: &central)
            appendUInt32(0, to: &central)
            appendUInt32(offsets[offsets.count - 1], to: &central)
            central.append(nameBytes)
        }

        let centralOffset = UInt32(local.count)
        var archive = local
        archive.append(central)
        appendUInt32(0x06054b50, to: &archive)
        appendUInt16(0, to: &archive)
        appendUInt16(0, to: &archive)
        appendUInt16(UInt16(entries.count), to: &archive)
        appendUInt16(UInt16(entries.count), to: &archive)
        appendUInt32(UInt32(central.count), to: &archive)
        appendUInt32(centralOffset, to: &archive)
        appendUInt16(0, to: &archive)
        return archive
    }

    private static func dosTimestamp(_ date: Date) -> (UInt16, UInt16) {
        var calendar = Calendar(identifier: .gregorian)
        calendar.timeZone = TimeZone(secondsFromGMT: 0) ?? .gmt
        let parts = calendar.dateComponents([.year, .month, .day, .hour, .minute, .second], from: date)
        let year = UInt16(max(0, (parts.year ?? 1980) - 1980))
        let time = (UInt16(parts.hour ?? 0) << 11)
            | (UInt16(parts.minute ?? 0) << 5)
            | (UInt16(parts.second ?? 0) / 2)
        let day = (year << 9)
            | (UInt16(parts.month ?? 1) << 5)
            | UInt16(parts.day ?? 1)
        return (time, day)
    }

    private static func appendUInt16(_ value: UInt16, to data: inout Data) {
        data.append(UInt8(value & 0xFF))
        data.append(UInt8((value >> 8) & 0xFF))
    }

    private static func appendUInt32(_ value: UInt32, to data: inout Data) {
        data.append(UInt8(value & 0xFF))
        data.append(UInt8((value >> 8) & 0xFF))
        data.append(UInt8((value >> 16) & 0xFF))
        data.append(UInt8((value >> 24) & 0xFF))
    }

    private static func readUInt16(_ data: Data, _ offset: Int) -> UInt16 {
        UInt16(data[offset]) | (UInt16(data[offset + 1]) << 8)
    }

    private static func readUInt32(_ data: Data, _ offset: Int) -> UInt32 {
        UInt32(data[offset])
            | (UInt32(data[offset + 1]) << 8)
            | (UInt32(data[offset + 2]) << 16)
            | (UInt32(data[offset + 3]) << 24)
    }
}

private enum ZipCRC32 {
    private static let table: [UInt32] = (0..<256).map { index in
        var crc = UInt32(index)
        for _ in 0..<8 {
            if crc & 1 == 1 {
                crc = (crc >> 1) ^ 0xEDB88320
            } else {
                crc >>= 1
            }
        }
        return crc
    }

    static func checksum(_ data: Data) -> UInt32 {
        var crc: UInt32 = 0xFFFFFFFF
        for byte in data {
            let index = Int((crc ^ UInt32(byte)) & 0xFF)
            crc = (crc >> 8) ^ table[index]
        }
        return crc ^ 0xFFFFFFFF
    }
}
