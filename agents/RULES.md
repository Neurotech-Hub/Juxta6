# Agent rules (Juxta6-nRF)

Canonical constraints for Cursor / coding agents. Cursor also loads a short alwaysApply mirror under `.cursor/rules/`.

## Build and debug ownership

- The **user** builds, flashes, runs RTT, and debugs hardware via **nRF Connect** in VS Code / Cursor.
- Agents must **not** run `west build`, `west flash`, JLink/RTT sessions, or diagnose issues by building.
- Document board target, snippets, and expected nRF Connect / west settings; then **wait** for user build/flash feedback.

## Repository boundaries

- Never west-build or modify `reference/juxta5-8/` (frozen behavioral reference only).
- Prefer Zephyr DT aliases / nodelabels and sensors API over copied nRF52 register drivers.
- No LIS2DH12 — motion path is ADXL367 (`adi,adxl367`). Keep BMI270 / BME688 shut down unless a fixture explicitly probes them.
- BTN1 (`sw0`) stands in for Juxta MAG_INT; LED1 RGB only (not LED2).
- Do not invent a custom `boards/` tree until a custom PCB is requested.
- Keep HIL apps small and isolated.
- **Channel Sounding / RAS:** only inside [`lib/juxta_range/`](../lib/juxta_range/). Applications must not call `bt_le_cs_*` or RAS APIs directly.
- **Never** copy nRF54L15 DK CS antenna-switch overlays onto Tag (`gpio1 11–14` are Tag TWI). Use Tag `skyworks,sky13348` / board DTS.
- Do not expand into Hublink, NOR CSV logging, MCUboot/DFU, Encounter Manager, or Juxta prod without an explicit request.
- Anchors are a **policy** on the same mobile↔mobile ranging stack — do not invent anchor-only CS paths.
- **iOS companion** lives under [`companion/iOS/`](../companion/iOS/) (Xcode project **Juxta6**). Do not west-build it; do not mix NCS/west commands into that tree. Keep firmware (`applications/`, `lib/`) and companion edits in lockstep on the Hublink / schema v6 contract (`companion/iOS/spec_HUBLINK.md`).

## When stuck

- Power / DEBUG OUT / missing board target / NCS too old / missing CS Kconfig: **stop and ask**. Do not iterate blind hardware fixes.
- TrustZone (`cpuapp/ns`) and FLPR are out of scope unless the user asks.
