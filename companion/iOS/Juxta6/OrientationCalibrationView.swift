//
//  OrientationCalibrationView.swift
//  Juxta6
//
//  Info tab → Orientation Calibration. One JX_ Tag sits still and advertises; the user walks to
//  each point of a 13-point cross (center, then N/E/S/W at 1, 5 and 10 m) and taps it to log
//  5 s of advertisement RSSI. The table can be shared as CSV and plotted as an RSSI-vs-distance image.
//

import SwiftUI
import UIKit
import CoreBluetooth
import Charts

// MARK: - Points

/// One spot on the calibration cross. +x is east, +y is north (meters from the Tag).
struct CalibrationPoint: Identifiable, Hashable {
    let id: String
    /// "C", "N", "E", "S" or "W".
    let direction: String
    let distance: Double
    let x: Double
    let y: Double

    static let ringDistances: [Double] = [1, 5, 10]

    /// -1, 0 or 1 along each axis; used to place the point on the schematic map.
    var unitX: CGFloat { x > 0 ? 1 : (x < 0 ? -1 : 0) }
    var unitY: CGFloat { y > 0 ? 1 : (y < 0 ? -1 : 0) }

    /// Center first, then the 1 m ring, 5 m ring, 10 m ring; each ring N, E, S, W.
    static let all: [CalibrationPoint] = {
        var points = [CalibrationPoint(id: "C", direction: "C", distance: 0, x: 0, y: 0)]
        for d in ringDistances {
            let label = String(Int(d))
            points.append(CalibrationPoint(id: "N\(label)", direction: "N", distance: d, x: 0, y: d))
            points.append(CalibrationPoint(id: "E\(label)", direction: "E", distance: d, x: d, y: 0))
            points.append(CalibrationPoint(id: "S\(label)", direction: "S", distance: d, x: 0, y: -d))
            points.append(CalibrationPoint(id: "W\(label)", direction: "W", distance: d, x: -d, y: 0))
        }
        return points
    }()

    static let minimumForVisualize = 4
}

struct CalibrationMeasurement {
    let pointID: String
    let deviceName: String
    let samples: Int
    /// Mean of the raw advertisement RSSI (dBm), before the attenuation factor.
    let rawMean: Double
    let standardDeviation: Double
    let date: Date

    func adjusted(by factor: Double) -> Double { rawMean * factor }
}

// MARK: - Scanner

/// Dedicated central for this screen, so the Device tab scan, connection and transfer logic are untouched.
final class CalibrationScanner: NSObject, ObservableObject, CBCentralManagerDelegate {
    struct Advertiser: Identifiable, Equatable {
        let id: UUID
        var name: String
        var rssi: Int
        var lastSeen: Date
    }

    static let sampleDuration: TimeInterval = 5.0
    /// An advertiser not heard for this long is ignored by the auto-select.
    private static let recentSeconds: TimeInterval = 10

    @Published private(set) var advertisers: [Advertiser] = []
    @Published private(set) var targetID: UUID?
    @Published private(set) var isTargetLocked = false
    @Published private(set) var bluetoothReady = false
    @Published private(set) var samplingPointID: String?
    @Published private(set) var sampleProgress: Double = 0
    @Published private(set) var sampleCount = 0

    private var central: CBCentralManager?
    private var wantsScan = false
    private var sampleValues: [Int] = []
    private var sampleStart: Date?
    private var sampleTimer: Timer?
    private var sampleCompletion: ((CalibrationMeasurement?) -> Void)?
    private var sampleDeviceName = ""

    var target: Advertiser? {
        guard let targetID else { return nil }
        return advertisers.first(where: { $0.id == targetID })
    }

    // MARK: Scan lifecycle

    func start() {
        wantsScan = true
        if central == nil {
            central = CBCentralManager(delegate: self, queue: nil)
        } else {
            beginScanIfReady()
        }
    }

    func stop() {
        wantsScan = false
        cancelSampling()
        central?.stopScan()
    }

    private func beginScanIfReady() {
        guard wantsScan, let central, central.state == .poweredOn else { return }
        // Duplicates on so every advertisement in a burst contributes to the 5 s average.
        central.scanForPeripherals(withServices: nil, options: [CBCentralManagerScanOptionAllowDuplicatesKey: true])
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        bluetoothReady = central.state == .poweredOn
        if bluetoothReady {
            beginScanIfReady()
        }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let rssi = RSSI.intValue
        guard rssi != 127 else { return }  // 127 = RSSI unavailable
        // Non-connectable production packets often have no cached `peripheral.name`.
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name
        guard let name, name.hasPrefix("JX_"), JuxtaPeerID.isDeviceFolderID(name) else { return }

        let now = Date()
        if let index = advertisers.firstIndex(where: { $0.id == peripheral.identifier }) {
            advertisers[index].name = name
            advertisers[index].rssi = rssi
            advertisers[index].lastSeen = now
        } else {
            advertisers.append(Advertiser(id: peripheral.identifier, name: name, rssi: rssi, lastSeen: now))
        }

        if samplingPointID != nil {
            // Target stays frozen for the whole window.
            if peripheral.identifier == targetID {
                sampleValues.append(rssi)
                sampleCount = sampleValues.count
            }
        } else if !isTargetLocked {
            autoSelectTarget(now: now)
        }
    }

    // MARK: Target selection

    private func autoSelectTarget(now: Date) {
        let recent = advertisers.filter { now.timeIntervalSince($0.lastSeen) < Self.recentSeconds }
        let best = recent.max(by: { $0.rssi < $1.rssi })
        if best?.id != targetID { targetID = best?.id }
    }

    /// Pins a specific advertiser as the target.
    func lockTarget(_ id: UUID) {
        guard samplingPointID == nil else { return }
        targetID = id
        isTargetLocked = true
    }

    /// Back to following the strongest recent advertiser.
    func unlockTarget() {
        guard samplingPointID == nil else { return }
        isTargetLocked = false
        autoSelectTarget(now: Date())
    }

    // MARK: Sampling

    /// Collects RSSI from the target for `sampleDuration` seconds. Completes with nil when no advertisement arrived.
    func sample(pointID: String, completion: @escaping (CalibrationMeasurement?) -> Void) {
        guard samplingPointID == nil, let target else { return }
        samplingPointID = pointID
        sampleDeviceName = target.name
        sampleValues = []
        sampleCount = 0
        sampleProgress = 0
        sampleCompletion = completion
        let start = Date()
        sampleStart = start

        sampleTimer?.invalidate()
        sampleTimer = Timer.scheduledTimer(withTimeInterval: 0.1, repeats: true) { [weak self] _ in
            guard let self, let start = self.sampleStart else { return }
            let elapsed = Date().timeIntervalSince(start)
            if elapsed >= Self.sampleDuration {
                self.finishSampling()
            } else {
                self.sampleProgress = elapsed / Self.sampleDuration
            }
        }
    }

    private func finishSampling() {
        sampleTimer?.invalidate()
        sampleTimer = nil
        let pointID = samplingPointID
        let values = sampleValues
        let name = sampleDeviceName
        let completion = sampleCompletion

        samplingPointID = nil
        sampleProgress = 0
        sampleStart = nil
        sampleCompletion = nil
        sampleValues = []

        guard let pointID else { return }
        guard !values.isEmpty else {
            completion?(nil)
            return
        }
        let n = Double(values.count)
        let mean = Double(values.reduce(0, +)) / n
        let variance = values.count > 1
            ? values.reduce(0.0) { $0 + pow(Double($1) - mean, 2) } / (n - 1)
            : 0
        completion?(CalibrationMeasurement(
            pointID: pointID,
            deviceName: name,
            samples: values.count,
            rawMean: mean,
            standardDeviation: variance.squareRoot(),
            date: Date()
        ))
    }

    private func cancelSampling() {
        sampleTimer?.invalidate()
        sampleTimer = nil
        samplingPointID = nil
        sampleProgress = 0
        sampleStart = nil
        sampleCompletion = nil
        sampleValues = []
    }
}

// MARK: - Export helpers

private enum CalibrationExport {
    static var iosModel: String {
        var info = utsname()
        uname(&info)
        let identifier = withUnsafePointer(to: &info.machine) {
            $0.withMemoryRebound(to: CChar.self, capacity: 1) { String(cString: $0) }
        }
        return identifier.isEmpty ? UIDevice.current.model : identifier
    }

    static func stamp(_ date: Date) -> String {
        let f = DateFormatter()
        f.locale = Locale(identifier: "en_US_POSIX")
        f.dateFormat = "yyyyMMdd-HHmm"
        return f.string(from: date)
    }

    static func csv(measurements: [CalibrationMeasurement], factor: Double) -> String {
        let iso = ISO8601DateFormatter()
        let model = iosModel
        var lines = ["device_name,point_id,direction,distance_m,x_m,y_m,samples,rssi_mean_raw_dbm,rssi_sd_dbm,attenuation_factor,rssi_mean_adjusted_dbm,timestamp_iso,ios_model"]
        for point in CalibrationPoint.all {
            guard let m = measurements.first(where: { $0.pointID == point.id }) else { continue }
            let fields: [String] = [
                m.deviceName,
                point.id,
                point.direction,
                String(format: "%.0f", point.distance),
                String(format: "%.0f", point.x),
                String(format: "%.0f", point.y),
                String(m.samples),
                String(format: "%.2f", m.rawMean),
                String(format: "%.2f", m.standardDeviation),
                String(format: "%.3f", factor),
                String(format: "%.2f", m.adjusted(by: factor)),
                iso.string(from: m.date),
                model
            ]
            lines.append(fields.joined(separator: ","))
        }
        return lines.joined(separator: "\n") + "\n"
    }

    /// Writes `data` into a fresh temp folder so cleanup only has to remove that folder.
    static func write(_ data: Data, fileName: String) throws -> URL {
        let folder = FileManager.default.temporaryDirectory
            .appendingPathComponent("juxta-calibration-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        let url = folder.appendingPathComponent(fileName)
        try data.write(to: url, options: .atomic)
        return url
    }

    static func discard(_ url: URL) {
        try? FileManager.default.removeItem(at: url.deletingLastPathComponent())
    }
}

private struct CalibrationSharePayload: Identifiable {
    let id = UUID()
    let urls: [URL]
}

// MARK: - Main view

struct OrientationCalibrationView: View {
    @StateObject private var scanner = CalibrationScanner()
    /// Multiplies the raw mean RSSI to account for differences between iOS devices. Subtle; set from the ••• menu.
    @AppStorage("orientationCalibrationAttenuation") private var attenuation: Double = 1.0

    @State private var measurements: [String: CalibrationMeasurement] = [:]
    @State private var noSignal: Set<String> = []
    @State private var showAttenuationAlert = false
    @State private var attenuationText = ""
    @State private var showClearConfirm = false
    @State private var showVisualize = false
    @State private var sharePayload: CalibrationSharePayload?
    @State private var exportError: String?

    private var measuredCount: Int { measurements.count }
    private var isSampling: Bool { scanner.samplingPointID != nil }

    var body: some View {
        VStack(spacing: 8) {
            targetRow
            map
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            statusLine
            bottomBar
        }
        .padding(.horizontal, 16)
        .padding(.top, 8)
        .padding(.bottom, 10)
        .background(Color(.systemGroupedBackground))
        .navigationTitle("Orientation Calibration")
        .navigationBarTitleDisplayMode(.inline)
        .toolbar {
            ToolbarItem(placement: .navigationBarTrailing) {
                Menu {
                    Button {
                        attenuationText = Self.format(attenuation)
                        showAttenuationAlert = true
                    } label: {
                        Label("Attenuation factor…", systemImage: "slider.horizontal.3")
                    }
                    Button(role: .destructive) {
                        showClearConfirm = true
                    } label: {
                        Label("Clear all", systemImage: "trash")
                    }
                    .disabled(measurements.isEmpty && noSignal.isEmpty)
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
                .disabled(isSampling)
            }
        }
        .onAppear { scanner.start() }
        .onDisappear { scanner.stop() }
        .alert("Attenuation factor", isPresented: $showAttenuationAlert) {
            TextField("1.0", text: $attenuationText)
                .keyboardType(.decimalPad)
            Button("Cancel", role: .cancel) {}
            Button("Set") { applyAttenuationText() }
        } message: {
            Text("Multiplies each averaged RSSI (dBm) to compensate for this iPhone. Default 1.0.")
        }
        .confirmationDialog("Clear all measurements?", isPresented: $showClearConfirm, titleVisibility: .visible) {
            Button("Clear all", role: .destructive) {
                measurements.removeAll()
                noSignal.removeAll()
            }
            Button("Cancel", role: .cancel) {}
        }
        .sheet(isPresented: $showVisualize) {
            CalibrationVisualizeSheet(report: makeReport())
        }
        .sheet(item: $sharePayload) { payload in
            ShareSheet(items: payload.urls.map { $0 as Any }) {
                payload.urls.forEach(CalibrationExport.discard)
                sharePayload = nil
            }
        }
        .alert("Export Failed", isPresented: Binding(
            get: { exportError != nil },
            set: { if !$0 { exportError = nil } }
        )) {
            Button("OK", role: .cancel) { exportError = nil }
        } message: {
            Text(exportError ?? "")
        }
    }

    // MARK: Target row

    private var targetRow: some View {
        Menu {
            Button {
                scanner.unlockTarget()
            } label: {
                Label("Auto (strongest)", systemImage: scanner.isTargetLocked ? "circle" : "checkmark")
            }
            let sorted = scanner.advertisers.sorted { $0.rssi > $1.rssi }
            ForEach(sorted) { adv in
                Button {
                    scanner.lockTarget(adv.id)
                } label: {
                    let selected = scanner.isTargetLocked && scanner.targetID == adv.id
                    Label("\(adv.name)  \(adv.rssi) dBm", systemImage: selected ? "checkmark" : "circle")
                }
            }
        } label: {
            HStack(spacing: 8) {
                Image(systemName: scanner.isTargetLocked ? "lock.fill" : "antenna.radiowaves.left.and.right")
                    .font(.system(size: 13, weight: .semibold))
                    .frame(width: 18)
                Text(targetTitle)
                    .font(.system(size: 14, weight: .semibold, design: .monospaced))
                    .lineLimit(1)
                Spacer(minLength: 4)
                if let target = scanner.target {
                    Text("\(target.rssi) dBm")
                        .font(.system(size: 13, design: .monospaced))
                        .foregroundColor(.secondary)
                }
                Image(systemName: "chevron.up.chevron.down")
                    .font(.system(size: 10, weight: .semibold))
                    .foregroundColor(.secondary)
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 8)
            .frame(maxWidth: .infinity)
            .background(Color(.systemGray6))
            .clipShape(RoundedRectangle(cornerRadius: 8))
        }
        .disabled(isSampling)
    }

    private var targetTitle: String {
        if !scanner.bluetoothReady { return "Bluetooth unavailable" }
        return scanner.target?.name ?? "Searching for JX_…"
    }

    // MARK: Map

    private var map: some View {
        GeometryReader { geo in
            let side = min(geo.size.width, geo.size.height)
            let radius = max(side / 2 - 36, 60)
            let diameter = min(40, max(radius / 3 - 3, 28))
            let ringStep = radius / CGFloat(CalibrationPoint.ringDistances.count)
            ZStack {
                // Schematic rings (evenly spaced so the inner points stay tappable) and the x/y axes.
                ForEach(Array(CalibrationPoint.ringDistances.enumerated()), id: \.offset) { index, distance in
                    let r = ringStep * CGFloat(index + 1)
                    Circle()
                        .stroke(Color.secondary.opacity(0.25), style: StrokeStyle(lineWidth: 1, dash: [3, 4]))
                        .frame(width: r * 2, height: r * 2)
                    Text("\(Int(distance)) m")
                        .font(.system(size: 9, weight: .medium))
                        .foregroundColor(.secondary)
                        .offset(x: r * 0.7071 + 12, y: -(r * 0.7071) - 2)
                }
                Path { p in
                    p.move(to: CGPoint(x: side / 2 - radius, y: side / 2))
                    p.addLine(to: CGPoint(x: side / 2 + radius, y: side / 2))
                    p.move(to: CGPoint(x: side / 2, y: side / 2 - radius))
                    p.addLine(to: CGPoint(x: side / 2, y: side / 2 + radius))
                }
                .stroke(Color.secondary.opacity(0.25), lineWidth: 1)
                .frame(width: side, height: side)

                ForEach(["N", "E", "S", "W"], id: \.self) { dir in
                    let offset = radius + diameter / 2 + 9
                    Text(dir)
                        .font(.system(size: 10, weight: .semibold))
                        .foregroundColor(.secondary)
                        .offset(x: dir == "E" ? offset : (dir == "W" ? -offset : 0),
                                y: dir == "S" ? offset : (dir == "N" ? -offset : 0))
                }

                ForEach(CalibrationPoint.all) { point in
                    let reach = ringFraction(point.distance) * radius
                    pointView(point, diameter: diameter)
                        .offset(x: point.unitX * reach, y: -point.unitY * reach)
                }
            }
            .frame(width: side, height: side)
            .position(x: geo.size.width / 2, y: geo.size.height / 2)
        }
    }

    /// Ring positions are evenly spaced (1/5/10 m → 1/3, 2/3, 1 of the radius), not to scale.
    private func ringFraction(_ distance: Double) -> CGFloat {
        guard let index = CalibrationPoint.ringDistances.firstIndex(of: distance) else { return 0 }
        return CGFloat(index + 1) / CGFloat(CalibrationPoint.ringDistances.count)
    }

    @ViewBuilder
    private func pointView(_ point: CalibrationPoint, diameter: CGFloat) -> some View {
        let measurement = measurements[point.id]
        let isActive = scanner.samplingPointID == point.id
        let hasNoSignal = noSignal.contains(point.id)
        let dimmed = isSampling && !isActive

        ZStack {
            Circle()
                .fill(measurement.map { Self.color(forRSSI: $0.adjusted(by: attenuation)) } ?? Color(.systemBackground))
            Circle()
                .stroke(hasNoSignal ? Color.red : (measurement == nil ? Color.secondary : Color.clear),
                        lineWidth: hasNoSignal ? 2 : 1.5)
            if isActive {
                Circle()
                    .trim(from: 0, to: scanner.sampleProgress)
                    .stroke(Color.accentColor, style: StrokeStyle(lineWidth: 3, lineCap: .round))
                    .rotationEffect(.degrees(-90))
                    .animation(.linear(duration: 0.1), value: scanner.sampleProgress)
            }
            Text(pointLabel(point, measurement: measurement, hasNoSignal: hasNoSignal))
                .font(.system(size: 12, weight: .semibold, design: .rounded))
                .foregroundColor(measurement != nil ? Color.black.opacity(0.8) : (hasNoSignal ? Color.red : Color.secondary))
                .minimumScaleFactor(0.6)
                .lineLimit(1)
        }
        .frame(width: diameter, height: diameter)
        .contentShape(Circle())
        .opacity(dimmed ? 0.35 : 1)
        .onTapGesture { startSampling(point) }
        .onLongPressGesture(minimumDuration: 0.6) { clear(point) }
        .accessibilityElement(children: .ignore)
        .accessibilityLabel("\(point.id), \(Int(point.distance)) meters")
        .accessibilityValue(measurement.map { "\(Int($0.adjusted(by: attenuation).rounded())) dBm" } ?? (hasNoSignal ? "no signal" : "not measured"))
        .accessibilityAddTraits(.isButton)
    }

    private func pointLabel(_ point: CalibrationPoint, measurement: CalibrationMeasurement?, hasNoSignal: Bool) -> String {
        if let measurement { return String(Int(measurement.adjusted(by: attenuation).rounded())) }
        if hasNoSignal { return "—" }
        return point.distance == 0 ? "0" : String(Int(point.distance))
    }

    private static func color(forRSSI rssi: Double) -> Color {
        // -95 dBm (red) … -40 dBm (green)
        let t = min(max((rssi + 95) / 55, 0), 1)
        return Color(hue: 0.33 * t, saturation: 0.55, brightness: 0.95)
    }

    // MARK: Status and bottom bar

    private var statusLine: some View {
        let text: String
        if let id = scanner.samplingPointID {
            let remaining = max(0, CalibrationScanner.sampleDuration * (1 - scanner.sampleProgress))
            text = String(format: "Sampling %@… %.1f s · %d adv", id, remaining, scanner.sampleCount)
        } else {
            text = "\(measuredCount)/\(CalibrationPoint.all.count) measured · tap a point to sample 5 s"
        }
        return HStack(spacing: 6) {
            Text(text)
            if abs(attenuation - 1.0) > 0.0005 {
                Text("×\(Self.format(attenuation))")
                    .font(.system(size: 10, design: .monospaced))
                    .foregroundColor(.secondary.opacity(0.8))
            }
        }
        .font(.system(size: 12))
        .foregroundColor(.secondary)
        .lineLimit(1)
        .minimumScaleFactor(0.8)
        .frame(maxWidth: .infinity)
    }

    private var bottomBar: some View {
        HStack(spacing: 10) {
            Button {
                showVisualize = true
            } label: {
                Label("Visualize", systemImage: "chart.xyaxis.line")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.borderedProminent)
            // With nothing measured the page shows example data; once measuring, at least 4 points are required.
            .disabled((measuredCount > 0 && measuredCount < CalibrationPoint.minimumForVisualize) || isSampling)

            Button {
                shareCSV()
            } label: {
                Label("Share CSV", systemImage: "square.and.arrow.up")
                    .frame(maxWidth: .infinity)
            }
            .buttonStyle(.bordered)
            .disabled(measuredCount == 0 || isSampling)
        }
        .controlSize(.large)
    }

    // MARK: Actions

    private func startSampling(_ point: CalibrationPoint) {
        guard !isSampling, scanner.target != nil else { return }
        scanner.sample(pointID: point.id) { result in
            if let result {
                measurements[point.id] = result
                noSignal.remove(point.id)
                UINotificationFeedbackGenerator().notificationOccurred(.success)
            } else {
                // Keep any earlier value for this point; just flag the failed attempt when there is none.
                if measurements[point.id] == nil { noSignal.insert(point.id) }
                UINotificationFeedbackGenerator().notificationOccurred(.warning)
            }
        }
    }

    private func clear(_ point: CalibrationPoint) {
        guard !isSampling else { return }
        measurements[point.id] = nil
        noSignal.remove(point.id)
    }

    private func applyAttenuationText() {
        let cleaned = attenuationText.trimmingCharacters(in: .whitespaces).replacingOccurrences(of: ",", with: ".")
        if let value = Double(cleaned), value.isFinite, value > 0 {
            attenuation = value
        }
    }

    private static func format(_ value: Double) -> String {
        var s = String(format: "%.3f", value)
        while s.hasSuffix("0") { s.removeLast() }
        if s.hasSuffix(".") { s.append("0") }
        return s
    }

    private func orderedMeasurements() -> [CalibrationMeasurement] {
        CalibrationPoint.all.compactMap { measurements[$0.id] }
    }

    private func shareCSV() {
        let ordered = orderedMeasurements()
        guard let latest = ordered.max(by: { $0.date < $1.date }) else { return }
        let text = CalibrationExport.csv(measurements: ordered, factor: attenuation)
        let name = "\(latest.deviceName)_calibration_\(CalibrationExport.stamp(latest.date)).csv"
        do {
            let url = try CalibrationExport.write(Data(text.utf8), fileName: name)
            sharePayload = CalibrationSharePayload(urls: [url])
        } catch {
            exportError = error.localizedDescription
        }
    }

    private func makeReport() -> CalibrationReport {
        let ordered = orderedMeasurements()
        // Nothing captured yet: show synthetic data so the plots can be checked.
        guard let latest = ordered.max(by: { $0.date < $1.date }) else {
            return CalibrationReport.example(iosModel: CalibrationExport.iosModel)
        }
        let rows: [CalibrationReport.Row] = ordered.compactMap { m in
            guard let point = CalibrationPoint.all.first(where: { $0.id == m.pointID }) else { return nil }
            return CalibrationReport.Row(pointID: point.id, direction: point.direction,
                                         distance: point.distance, rssi: m.adjusted(by: attenuation),
                                         sd: m.standardDeviation * abs(attenuation))
        }
        return CalibrationReport(
            deviceName: latest.deviceName,
            date: latest.date,
            factor: attenuation,
            iosModel: CalibrationExport.iosModel,
            rows: rows,
            isExample: false
        )
    }
}

// MARK: - Report (data for the two plots)

struct CalibrationReport {
    struct Row: Identifiable {
        var id: String { pointID }
        let pointID: String
        let direction: String
        let distance: Double
        /// Adjusted mean RSSI (dBm).
        let rssi: Double
        /// Adjusted SD of the samples taken at this point (dBm).
        let sd: Double
    }

    /// All measured points at one distance, combined.
    struct DistanceStat: Identifiable {
        var id: Double { distance }
        let distance: Double
        let mean: Double
        /// ±1 SD across directions; a lone point falls back to its own sample SD.
        let sd: Double
        let count: Int
        var low: Double { mean - sd }
        var high: Double { mean + sd }
    }

    let deviceName: String
    let date: Date
    let factor: Double
    let iosModel: String
    let rows: [Row]
    let isExample: Bool

    var stats: [DistanceStat] {
        Dictionary(grouping: rows, by: \.distance)
            .map { distance, group -> DistanceStat in
                let values = group.map(\.rssi)
                let n = Double(values.count)
                let mean = values.reduce(0, +) / n
                let sd: Double
                if values.count > 1 {
                    sd = (values.reduce(0.0) { $0 + pow($1 - mean, 2) } / (n - 1)).squareRoot()
                } else {
                    sd = group[0].sd
                }
                return DistanceStat(distance: distance, mean: mean, sd: sd, count: values.count)
            }
            .sorted { $0.distance < $1.distance }
    }

    /// Synthetic log-distance data with a direction-dependent offset, so the plots can be checked without measuring.
    static func example(iosModel: String) -> CalibrationReport {
        let offsets: [String: Double] = ["C": 0, "N": 0, "E": -4, "S": -9, "W": -3]
        let rows: [Row] = CalibrationPoint.all.enumerated().map { index, point in
            let base = point.distance == 0 ? -45.0 : -48 - 22 * log10(point.distance)
            let jitter = sin(Double(index) * 12.9898) * 1.2
            return Row(pointID: point.id, direction: point.direction, distance: point.distance,
                       rssi: base + (offsets[point.direction] ?? 0) + jitter,
                       sd: 1.5 + point.distance * 0.08)
        }
        return CalibrationReport(deviceName: "JX_EXAMPLE", date: Date(), factor: 1.0,
                                 iosModel: iosModel, rows: rows, isExample: true)
    }
}

// MARK: - Radial field (interpolated radiation pattern)

/// Colormap shared by the heatmap and its legend (viridis-like; bright = strong signal).
private enum CalibrationColorMap {
    private static let stops: [(r: Double, g: Double, b: Double)] = [
        (68, 1, 84), (59, 82, 139), (33, 145, 140), (94, 201, 98), (253, 231, 37)
    ]

    static func color(_ t: Double) -> Color {
        let clamped = min(max(t, 0), 1)
        let scaled = clamped * Double(stops.count - 1)
        let i = min(Int(scaled), stops.count - 2)
        let f = scaled - Double(i)
        let a = stops[i], b = stops[i + 1]
        return Color(red: (a.r + (b.r - a.r) * f) / 255,
                     green: (a.g + (b.g - a.g) * f) / 255,
                     blue: (a.b + (b.b - a.b) * f) / 255)
    }
}

/// RSSI over (radius, compass heading). Around each ring the measured headings are blended with a
/// cosine ease (wrapping at 360°); between rings (and the center) values are linear in distance.
private struct CalibrationRadialField {
    private struct Node {
        let radius: Double
        /// Sorted by heading; one entry means "same value in every direction".
        let samples: [(angle: Double, value: Double)]
    }

    private static let headings: [String: Double] = ["N": 0, "E": 90, "S": 180, "W": 270]
    private let nodes: [Node]
    let maxRadius: Double

    init?(rows: [CalibrationReport.Row]) {
        var built: [Node] = []
        if let center = rows.first(where: { $0.direction == "C" }) {
            built.append(Node(radius: 0, samples: [(0, center.rssi)]))
        }
        let ringRows = rows.filter { $0.distance > 0 }
        for distance in Set(ringRows.map(\.distance)).sorted() {
            let samples = ringRows
                .filter { $0.distance == distance }
                .compactMap { row in Self.headings[row.direction].map { (angle: $0, value: row.rssi) } }
                .sorted { $0.angle < $1.angle }
            if !samples.isEmpty { built.append(Node(radius: distance, samples: samples)) }
        }
        guard let last = built.last else { return nil }
        nodes = built
        maxRadius = max(last.radius, 5)
    }

    func value(radius: Double, heading: Double) -> Double {
        guard let first = nodes.first, let last = nodes.last else { return 0 }
        if radius <= first.radius { return Self.ringValue(first.samples, at: heading) }
        if radius >= last.radius { return Self.ringValue(last.samples, at: heading) }
        for i in 1..<nodes.count where radius <= nodes[i].radius {
            let a = nodes[i - 1], b = nodes[i]
            let t = (radius - a.radius) / (b.radius - a.radius)
            return Self.ringValue(a.samples, at: heading) * (1 - t) + Self.ringValue(b.samples, at: heading) * t
        }
        return Self.ringValue(last.samples, at: heading)
    }

    private static func ringValue(_ samples: [(angle: Double, value: Double)], at heading: Double) -> Double {
        if samples.count == 1 { return samples[0].value }
        var theta = heading.truncatingRemainder(dividingBy: 360)
        if theta < 0 { theta += 360 }
        let hi = samples.firstIndex(where: { $0.angle > theta }) ?? 0
        let lo = (hi - 1 + samples.count) % samples.count
        let a0 = samples[lo].angle
        var a1 = samples[hi].angle
        if a1 <= a0 { a1 += 360 }
        var t = theta
        if t < a0 { t += 360 }
        let u = (t - a0) / (a1 - a0)
        let w = (1 - cos(Double.pi * u)) / 2
        return samples[lo].value * (1 - w) + samples[hi].value * w
    }
}

// MARK: - Radial plot

/// Top-down heatmap: north up, true radial scale, measured points marked with their dBm.
private struct CalibrationRadialPlot: View {
    let rows: [CalibrationReport.Row]
    let range: ClosedRange<Double>

    private static let radialSteps = 40
    private static let angularSteps = 96

    var body: some View {
        Canvas { context, size in
            guard let field = CalibrationRadialField(rows: rows) else { return }
            let center = CGPoint(x: size.width / 2, y: size.height / 2)
            let radius = min(size.width, size.height) / 2 - 16
            let scale = radius / field.maxRadius
            let span = max(range.upperBound - range.lowerBound, 0.001)

            func point(_ r: Double, _ heading: Double) -> CGPoint {
                let rad = heading * .pi / 180
                return CGPoint(x: center.x + CGFloat(r * scale * sin(rad)),
                               y: center.y - CGFloat(r * scale * cos(rad)))
            }

            // Heatmap cells.
            let dr = field.maxRadius / Double(Self.radialSteps)
            let da = 360.0 / Double(Self.angularSteps)
            for ia in 0..<Self.angularSteps {
                let a0 = Double(ia) * da
                let a1 = a0 + da
                for ir in 0..<Self.radialSteps {
                    let r0 = Double(ir) * dr
                    let r1 = r0 + dr
                    let v = field.value(radius: (r0 + r1) / 2, heading: (a0 + a1) / 2)
                    let color = CalibrationColorMap.color((v - range.lowerBound) / span)
                    var cell = Path()
                    cell.move(to: point(r0, a0))
                    cell.addLine(to: point(r1, a0))
                    cell.addLine(to: point(r1, a1))
                    cell.addLine(to: point(r0, a1))
                    cell.closeSubpath()
                    context.fill(cell, with: .color(color))
                    // Hairline in the same color hides anti-aliasing seams between cells.
                    context.stroke(cell, with: .color(color), lineWidth: 0.5)
                }
            }

            // Axes and measured rings.
            var axes = Path()
            axes.move(to: point(field.maxRadius, 0)); axes.addLine(to: point(field.maxRadius, 180))
            axes.move(to: point(field.maxRadius, 90)); axes.addLine(to: point(field.maxRadius, 270))
            context.stroke(axes, with: .color(.white.opacity(0.3)), lineWidth: 0.5)

            for distance in CalibrationPoint.ringDistances where distance <= field.maxRadius {
                let r = CGFloat(distance * scale)
                let ring = Path(ellipseIn: CGRect(x: center.x - r, y: center.y - r, width: r * 2, height: r * 2))
                context.stroke(ring, with: .color(.white.opacity(0.55)),
                               style: StrokeStyle(lineWidth: 0.75, dash: [3, 3]))
                context.draw(
                    Text("\(Int(distance)) m").font(.system(size: 8, weight: .medium)).foregroundColor(.white),
                    at: point(distance, 45).applying(CGAffineTransform(translationX: 2, y: -2)),
                    anchor: .bottomLeading
                )
            }

            // Compass.
            for (label, heading) in [("N", 0.0), ("E", 90.0), ("S", 180.0), ("W", 270.0)] {
                context.draw(
                    Text(label).font(.system(size: 10, weight: .semibold)).foregroundColor(.gray),
                    at: point(field.maxRadius + 14 / scale, heading),
                    anchor: .center
                )
            }

            // Measured points with their value.
            for row in rows {
                let headings: [String: Double] = ["N": 0, "E": 90, "S": 180, "W": 270]
                let heading: Double = headings[row.direction] ?? 0
                let p = point(row.distance, heading)
                let dot = Path(ellipseIn: CGRect(x: p.x - 3, y: p.y - 3, width: 6, height: 6))
                context.fill(dot, with: .color(.white))
                context.stroke(dot, with: .color(.black), lineWidth: 1)

                // The 1 m ring sits a few points from the center at true scale; its labels would pile up.
                if row.distance > 0 && row.distance < 2 { continue }

                let label = context.resolve(
                    Text("\(Int(row.rssi.rounded()))").font(.system(size: 8, weight: .semibold)).foregroundColor(.black)
                )
                let textSize = label.measure(in: CGSize(width: 60, height: 20))
                let labelCenter = CGPoint(x: p.x + 6 + textSize.width / 2, y: p.y - 7)
                let pill = CGRect(x: labelCenter.x - textSize.width / 2 - 2, y: labelCenter.y - textSize.height / 2 - 1,
                                  width: textSize.width + 4, height: textSize.height + 2)
                context.fill(Path(roundedRect: pill, cornerRadius: 3), with: .color(.white.opacity(0.8)))
                context.draw(label, at: labelCenter, anchor: .center)
            }
        }
        .aspectRatio(1, contentMode: .fit)
        .background(Color.white)
    }
}

// MARK: - Report view (the shareable picture)

/// Header, RSSI-vs-distance chart, radial heatmap and notes. Always light mode on white so the image is consistent.
private struct CalibrationReportView: View {
    let report: CalibrationReport

    private static let directionKeys: [String] = ["C", "N", "E", "S", "W"]
    private static let directionColors: [Color] = [.gray, .blue, .orange, .green, .purple]

    private var colorRange: ClosedRange<Double> {
        let values = report.rows.map(\.rssi)
        guard let lo = values.min(), let hi = values.max() else { return -100 ... -40 }
        return hi - lo < 2 ? (lo - 1) ... (hi + 1) : lo ... hi
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            header
            distanceChart
            radialSection
        }
        .padding(16)
        .background(Color.white)
        .foregroundColor(.black)
        .environment(\.colorScheme, .light)
    }

    private var header: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 8) {
                Text("\(report.deviceName) · orientation calibration")
                    .font(.system(size: 16, weight: .bold))
                    .lineLimit(1)
                    .minimumScaleFactor(0.7)
                if report.isExample {
                    Text("EXAMPLE DATA")
                        .font(.system(size: 9, weight: .bold))
                        .foregroundColor(.white)
                        .padding(.horizontal, 6)
                        .padding(.vertical, 2)
                        .background(Color.red)
                        .clipShape(Capsule())
                }
            }
            Text(headerDetail)
                .font(.system(size: 11))
                .foregroundColor(.gray)
        }
    }

    private var distanceChart: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("RSSI vs distance")
                .font(.system(size: 13, weight: .semibold))
            Chart {
                ForEach(report.stats) { stat in
                    RuleMark(x: .value("Distance (m)", stat.distance),
                             yStart: .value("Low", stat.low),
                             yEnd: .value("High", stat.high))
                        .foregroundStyle(Color.black.opacity(0.75))
                        .lineStyle(StrokeStyle(lineWidth: 1.5))
                    RuleMark(xStart: .value("Cap start", stat.distance - 0.25),
                             xEnd: .value("Cap end", stat.distance + 0.25),
                             y: .value("Low", stat.low))
                        .foregroundStyle(Color.black.opacity(0.75))
                        .lineStyle(StrokeStyle(lineWidth: 1.5))
                    RuleMark(xStart: .value("Cap start", stat.distance - 0.25),
                             xEnd: .value("Cap end", stat.distance + 0.25),
                             y: .value("High", stat.high))
                        .foregroundStyle(Color.black.opacity(0.75))
                        .lineStyle(StrokeStyle(lineWidth: 1.5))
                    LineMark(x: .value("Distance (m)", stat.distance),
                             y: .value("Mean RSSI (dBm)", stat.mean),
                             series: .value("Series", "mean"))
                        .foregroundStyle(Color.gray.opacity(0.6))
                        .lineStyle(StrokeStyle(lineWidth: 1.2, dash: [4, 3]))
                }
                ForEach(report.rows) { row in
                    PointMark(x: .value("Distance (m)", row.distance),
                              y: .value("RSSI (dBm)", row.rssi))
                        .foregroundStyle(by: .value("Direction", row.direction))
                        .symbolSize(26)
                        .opacity(0.85)
                }
                ForEach(report.stats) { stat in
                    PointMark(x: .value("Distance (m)", stat.distance),
                              y: .value("Mean RSSI (dBm)", stat.mean))
                        .foregroundStyle(Color.black)
                        .symbolSize(80)
                }
            }
            .chartXScale(domain: -0.5 ... 10.5)
            .chartXAxis {
                AxisMarks(values: [0, 1, 5, 10]) { _ in
                    AxisGridLine()
                    AxisTick()
                    AxisValueLabel()
                }
            }
            .chartYScale(domain: .automatic(includesZero: false))
            .chartXAxisLabel("Distance (m)")
            .chartYAxisLabel("RSSI (dBm)")
            .chartForegroundStyleScale(domain: Self.directionKeys, range: Self.directionColors)
            .chartLegend(position: .bottom, alignment: .center)
            .frame(height: 230)

            Text("Black: mean ± 1 SD across directions at each distance (a single point shows the SD of its own samples). Colored dots: individual directions.")
                .font(.system(size: 9))
                .foregroundColor(.gray)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var radialSection: some View {
        VStack(alignment: .leading, spacing: 4) {
            Text("Radiation pattern")
                .font(.system(size: 13, weight: .semibold))
            CalibrationRadialPlot(rows: report.rows, range: colorRange)
                .frame(maxWidth: .infinity)

            HStack(spacing: 6) {
                Text("\(Int(colorRange.lowerBound.rounded())) dBm")
                LinearGradient(
                    colors: (0...8).map { CalibrationColorMap.color(Double($0) / 8) },
                    startPoint: .leading, endPoint: .trailing
                )
                .frame(height: 8)
                .clipShape(Capsule())
                Text("\(Int(colorRange.upperBound.rounded())) dBm")
            }
            .font(.system(size: 9))
            .foregroundColor(.gray)

            Text("Top view, north up, true distance scale. Dots mark measured points; between them the pattern is interpolated (smooth around each ring, linear between rings), so only the dots are measurements.")
                .font(.system(size: 9))
                .foregroundColor(.gray)
                .fixedSize(horizontal: false, vertical: true)
        }
    }

    private var headerDetail: String {
        let df = DateFormatter()
        df.dateStyle = .medium
        df.timeStyle = .short
        let factor = abs(report.factor - 1) < 0.0005 ? "" : String(format: " · factor %.3f", report.factor)
        return "\(df.string(from: report.date)) · \(report.rows.count) points\(factor) · \(report.iosModel)"
    }
}

// MARK: - Visualize sheet

private struct CalibrationVisualizeSheet: View {
    let report: CalibrationReport
    @Environment(\.dismiss) private var dismiss
    @State private var sharePayload: CalibrationSharePayload?
    @State private var exportError: String?

    var body: some View {
        NavigationStack {
            ScrollView {
                CalibrationReportView(report: report)
                    .clipShape(RoundedRectangle(cornerRadius: 10))
                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color(.separator), lineWidth: 0.5))
                    .padding(.horizontal, 12)
                    .padding(.vertical, 8)
            }
            .background(Color(.systemGroupedBackground))
            .safeAreaInset(edge: .bottom) {
                Button {
                    shareImage()
                } label: {
                    Label("Share Image", systemImage: "square.and.arrow.up")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)
                .controlSize(.large)
                .padding(.horizontal, 16)
                .padding(.vertical, 8)
                .background(.bar)
            }
            .navigationTitle(report.isExample ? "Visualize (example)" : "Visualize")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .navigationBarTrailing) {
                    Button("Done") { dismiss() }
                }
            }
            .sheet(item: $sharePayload) { payload in
                ShareSheet(items: payload.urls.map { $0 as Any }) {
                    payload.urls.forEach(CalibrationExport.discard)
                    sharePayload = nil
                }
            }
            .alert("Export Failed", isPresented: Binding(
                get: { exportError != nil },
                set: { if !$0 { exportError = nil } }
            )) {
                Button("OK", role: .cancel) { exportError = nil }
            } message: {
                Text(exportError ?? "")
            }
        }
        .presentationDetents([.large])
    }

    private func shareImage() {
        // Fixed width, natural height: both plots stack into one tall image.
        let content = CalibrationReportView(report: report)
            .frame(width: 380)
        let renderer = ImageRenderer(content: content)
        renderer.scale = 3
        guard let data = renderer.uiImage?.pngData() else {
            exportError = "Could not render the image."
            return
        }
        let prefix = report.isExample ? "example" : report.deviceName
        let name = "\(prefix)_calibration_\(CalibrationExport.stamp(report.date)).png"
        do {
            let url = try CalibrationExport.write(data, fileName: name)
            sharePayload = CalibrationSharePayload(urls: [url])
        } catch {
            exportError = error.localizedDescription
        }
    }
}

#Preview {
    NavigationStack { OrientationCalibrationView() }
}
