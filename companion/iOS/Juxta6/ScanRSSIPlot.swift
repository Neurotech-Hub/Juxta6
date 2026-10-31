//
//  ScanRSSIPlot.swift
//  Juxta6
//
//  Advertisement RSSI plot for the Device tab: one column per advertiser (JX_ / JB_ names and
//  connectable Hublink devices), one dot per advertisement seen during a scan.
//

import SwiftUI
import Foundation

// MARK: - Model

struct ScanPlotDevice: Identifiable, Equatable {
    /// `CBPeripheral.identifier` (stable per advertiser address on iOS).
    let id: UUID
    var name: String
}

struct ScanPlotSighting {
    let deviceID: UUID
    let rssi: Int
    let isConnectable: Bool
    let date: Date
}

/// Separate from `AppState` so a burst of advertisements only redraws the plot, not all of `ContentView`.
final class ScanPlotModel: ObservableObject {
    /// First-seen order; this is the left-to-right column order.
    @Published private(set) var devices: [ScanPlotDevice] = []
    /// Oldest first.
    @Published private(set) var sightings: [ScanPlotSighting] = []
    /// True once a scan has started in this session (drives the "data stale" message and placeholder).
    @Published private(set) var hasScanned = false
    /// Newest advertisement per device (white dot and collapsing ring). Changes always come with a `sightings` update.
    private(set) var latestByDevice: [UUID: ScanPlotSighting] = [:]

    private var countsByDevice: [UUID: Int] = [:]

    /// Keeps memory and drawing bounded during a 120 s scan with a fast connectable advertiser.
    static let maxSightingsPerDevice = 120

    func slot(of deviceID: UUID) -> Int? {
        devices.firstIndex(where: { $0.id == deviceID })
    }

    /// Called when the user starts a scan; the previous plot is dropped here (not when the scan stops).
    func beginScan() {
        devices.removeAll()
        sightings.removeAll()
        countsByDevice.removeAll()
        latestByDevice.removeAll()
        hasScanned = true
    }

    func record(deviceID: UUID, name: String, rssi: Int, isConnectable: Bool, date: Date = Date()) {
        if let index = devices.firstIndex(where: { $0.id == deviceID }) {
            if devices[index].name != name { devices[index].name = name }
        } else {
            devices.append(ScanPlotDevice(id: deviceID, name: name))
        }

        let sighting = ScanPlotSighting(deviceID: deviceID, rssi: rssi, isConnectable: isConnectable, date: date)
        latestByDevice[deviceID] = sighting
        sightings.append(sighting)

        let count = (countsByDevice[deviceID] ?? 0) + 1
        countsByDevice[deviceID] = count
        if count > Self.maxSightingsPerDevice,
           let oldest = sightings.firstIndex(where: { $0.deviceID == deviceID }) {
            sightings.remove(at: oldest)
            countsByDevice[deviceID] = count - 1
        }
    }
}

// MARK: - Style

private enum ScanPlotStyle {
    static let background = Color(red: 0.067, green: 0.090, blue: 0.275)      // surface-1 #111746
    // Brand violet / blue, lightened so they read on the navy panel.
    static let connectable = Color(red: 0.655, green: 0.545, blue: 0.980)     // Violet #A78BFA
    static let nonConnectable = Color(red: 0.376, green: 0.647, blue: 0.980)  // Blue #60A5FA

    static let rssiMin = -100.0
    static let rssiMax = -30.0
    static let ringDuration: TimeInterval = 10
    static let ringStartScale: CGFloat = 3
    static let dotRadius: CGFloat = 4
    static let olderOpacity = 0.4
    /// Columns reserved before the axis starts to compress, so the first devices fill in from the left.
    static let minimumSlots = 8

    static func color(connectable: Bool) -> Color { connectable ? Self.connectable : Self.nonConnectable }
}

private struct ScanPlotLayout {
    static let left: CGFloat = 30
    static let right: CGFloat = 8
    static let top: CGFloat = 6
    static let bottom: CGFloat = 38

    let size: CGSize
    let deviceCount: Int

    var plot: CGRect {
        CGRect(x: Self.left,
               y: Self.top,
               width: max(size.width - Self.left - Self.right, 1),
               height: max(size.height - Self.top - Self.bottom, 1))
    }

    var slotWidth: CGFloat { plot.width / CGFloat(max(deviceCount, ScanPlotStyle.minimumSlots)) }

    func x(slot: Int) -> CGFloat { plot.minX + (CGFloat(slot) + 0.5) * slotWidth }

    func y(rssi: Int) -> CGFloat {
        let clamped = min(max(Double(rssi), ScanPlotStyle.rssiMin), ScanPlotStyle.rssiMax)
        let t = (clamped - ScanPlotStyle.rssiMin) / (ScanPlotStyle.rssiMax - ScanPlotStyle.rssiMin)
        return plot.maxY - CGFloat(t) * plot.height
    }
}

// MARK: - Panel

/// Legend, stale note, and plot. The caller sets the height (50% of the Devices area).
struct ScanRSSIPlotPanel: View {
    @ObservedObject var model: ScanPlotModel
    let isScanning: Bool

    private var showStale: Bool { !isScanning && model.hasScanned }

    private var placeholder: String? {
        guard model.sightings.isEmpty else { return nil }
        if isScanning { return "Listening for JX_ / JB_ advertisements…" }
        if model.hasScanned { return "No JX_ / JB_ advertisements seen" }
        return "Tap Scan to plot nearby\nJX_ / JB_ advertisements"
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            legend

            Text("data stale; scanning is off")
                .font(.system(size: 10, weight: .regular))
                .foregroundColor(.white.opacity(0.45))
                .opacity(showStale ? 1 : 0)
                .frame(height: 12)

            ZStack {
                ScanPlotStaticCanvas(model: model)
                ScanPlotRingOverlay(model: model, isScanning: isScanning)
                if let placeholder {
                    Text(placeholder)
                        .font(.system(size: 12, weight: .regular))
                        .foregroundColor(.white.opacity(0.5))
                        .multilineTextAlignment(.center)
                        .padding(.bottom, ScanPlotLayout.bottom / 2)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: 12).fill(ScanPlotStyle.background))
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
        .frame(maxWidth: .infinity)
        .background(Color(.systemBackground))
        .accessibilityElement(children: .contain)
        .accessibilityLabel("Advertisement RSSI plot, \(model.devices.count) devices")
    }

    private var legend: some View {
        HStack(spacing: 12) {
            legendDot(color: ScanPlotStyle.connectable, text: "Connectable")
            legendDot(color: ScanPlotStyle.nonConnectable, text: "Non-connectable")
            HStack(spacing: 4) {
                ZStack {
                    Circle().stroke(Color.white.opacity(0.7), lineWidth: 1).frame(width: 11, height: 11)
                    Circle().fill(Color.white).frame(width: 5, height: 5)
                }
                Text("Latest").lineLimit(1)
            }
            Spacer(minLength: 0)
            Text("dBm").lineLimit(1)
        }
        .font(.system(size: 10, weight: .regular))
        .foregroundColor(.white.opacity(0.65))
        .minimumScaleFactor(0.8)
    }

    private func legendDot(color: Color, text: String) -> some View {
        HStack(spacing: 4) {
            Circle().fill(color).frame(width: 7, height: 7)
            Text(text).lineLimit(1)
        }
    }
}

// MARK: - Static layer (axes, labels, dots)

private struct ScanPlotStaticCanvas: View {
    @ObservedObject var model: ScanPlotModel

    var body: some View {
        Canvas { context, size in
            let devices = model.devices
            let sightings = model.sightings
            let layout = ScanPlotLayout(size: size, deviceCount: devices.count)
            let plot = layout.plot

            // Fixed RSSI axis.
            for dbm in stride(from: Int(ScanPlotStyle.rssiMin), through: Int(ScanPlotStyle.rssiMax), by: 10) {
                let y = layout.y(rssi: dbm)
                var line = Path()
                line.move(to: CGPoint(x: plot.minX, y: y))
                line.addLine(to: CGPoint(x: plot.maxX, y: y))
                context.stroke(line, with: .color(.white.opacity(0.08)), lineWidth: 0.5)
                context.draw(
                    Text("\(dbm)")
                        .font(.system(size: 8, design: .monospaced))
                        .foregroundColor(.white.opacity(0.5)),
                    at: CGPoint(x: plot.minX - 4, y: y),
                    anchor: .trailing
                )
            }

            guard !devices.isEmpty else { return }

            var slotByID: [UUID: Int] = [:]
            for (index, device) in devices.enumerated() { slotByID[device.id] = index }

            // Advertiser names, about 30 degrees, ending at each column.
            for (index, device) in devices.enumerated() {
                var labelContext = context
                labelContext.translateBy(x: layout.x(slot: index), y: plot.maxY + 6)
                labelContext.rotate(by: .degrees(-30))
                labelContext.draw(
                    Text(device.name)
                        .font(.system(size: 8, design: .monospaced))
                        .foregroundColor(.white.opacity(0.75)),
                    at: .zero,
                    anchor: .trailing
                )
            }

            // Every advertisement, translucent. Each device's newest one is redrawn white on top below.
            let r = ScanPlotStyle.dotRadius
            for sighting in sightings {
                guard let slot = slotByID[sighting.deviceID] else { continue }
                let center = CGPoint(x: layout.x(slot: slot), y: layout.y(rssi: sighting.rssi))
                let rect = CGRect(x: center.x - r, y: center.y - r, width: r * 2, height: r * 2)
                context.fill(
                    Path(ellipseIn: rect),
                    with: .color(ScanPlotStyle.color(connectable: sighting.isConnectable).opacity(ScanPlotStyle.olderOpacity))
                )
            }

            // Newest advertisement of each device: opaque white, highest z-order.
            for latest in model.latestByDevice.values {
                guard let slot = slotByID[latest.deviceID] else { continue }
                let center = CGPoint(x: layout.x(slot: slot), y: layout.y(rssi: latest.rssi))
                let rect = CGRect(x: center.x - r, y: center.y - r, width: r * 2, height: r * 2)
                context.fill(Path(ellipseIn: rect), with: .color(.white))
            }
        }
    }
}

// MARK: - Ring overlay (newest advertisement per device)

/// Ring around each device's white dot: 3x radius collapsing to 1x over 10 s from that advertisement. Only animates while scanning.
private struct ScanPlotRingOverlay: View {
    @ObservedObject var model: ScanPlotModel
    let isScanning: Bool

    var body: some View {
        TimelineView(.animation(minimumInterval: 1.0 / 30.0, paused: !isScanning)) { timeline in
            Canvas { context, size in
                guard isScanning else { return }
                let layout = ScanPlotLayout(size: size, deviceCount: model.devices.count)

                for (slot, device) in model.devices.enumerated() {
                    guard let latest = model.latestByDevice[device.id] else { continue }
                    let age = timeline.date.timeIntervalSince(latest.date)
                    guard age < ScanPlotStyle.ringDuration else { continue }
                    let progress = CGFloat(max(age, 0) / ScanPlotStyle.ringDuration)

                    let center = CGPoint(x: layout.x(slot: slot), y: layout.y(rssi: latest.rssi))
                    let scale = ScanPlotStyle.ringStartScale - (ScanPlotStyle.ringStartScale - 1) * progress
                    let radius = ScanPlotStyle.dotRadius * scale
                    let rect = CGRect(x: center.x - radius, y: center.y - radius, width: radius * 2, height: radius * 2)

                    context.stroke(
                        Path(ellipseIn: rect),
                        with: .color(.white.opacity(0.9 - 0.5 * Double(progress))),
                        lineWidth: 1.5
                    )
                }
            }
        }
        .allowsHitTesting(false)
    }
}
