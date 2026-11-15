#!/usr/bin/env python3
"""Freeze intended pin->net assignments for juxta6_v1-0 into juxta6_netmap.json.

Source: endpoint-only wire tracing of a schematic snapshot (no mid-wire touches, so
accidental bridges are ignored), plus explicit corrections below.
Usage: build_netmap.py <snapshot_dir>
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import sch_model as m

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "juxta6_v1-0" / "juxta6_netmap.json"

# Explicit assignments (override traced intent). Pin key = "REF.NUMBER".
OVERRIDES: dict[str, dict[str, str | None]] = {
    "02_mcu_clocks": {
        # 32 MHz crystal case pins.
        "X2.2": "GND",
        "X2.4": "GND",
    },
    "04_sensors": {
        # BME688 I2C address select: SDO=GND -> 0x76 (Tag DTS bme688@76).
        "U3.5": "GND",
        # R8/R9 are SPI series resistors: MCU side keeps BMI_* name.
        "R8.1": "BMI_SCK",
        "R8.2": "BMI_SCK_U4",
        "U4.13": "BMI_SCK_U4",
        "R9.1": "BMI_MOSI",
        "R9.2": "BMI_MOSI_U4",
        "U4.14": "BMI_MOSI_U4",
    },
    "05_peripherals": {
        # W25Q64JW (single SPI): WP#/HOLD# tied high.
        "U8.1": "FLASH_CS",
        "U8.2": "FLASH_MISO",
        "U8.3": "VDD",
        "U8.4": "GND",
        "U8.5": "FLASH_MOSI",
        "U8.6": "FLASH_SCK",
        "U8.7": "VDD",
        "U8.8": "VDD",
        "U8.9": "GND",
        "U8.10": "GND",
        "R10.1": "VDD",
        "R10.2": "FLASH_CS",
        "C60.1": "VDD",
        "C60.2": "GND",
        "C61.1": "VDD",
        "C61.2": "GND",
        # RGB LED: common anode to VDD, cathode -> R -> GPIO (active low).
        "LED1.1": "VDD",
        "LED1.2": "LED_R_K",
        "LED1.3": "LED_B_K",
        "LED1.4": "LED_G_K",
        "R11.1": "LED_R_K",
        "R11.2": "LED_R",
        "R12.1": "LED_G_K",
        "R12.2": "LED_G",
        "R13.1": "LED_B_K",
        "R13.2": "LED_B",
        # SW1: T1-T2 and T3-T4 are internally bridged.
        "SW1.1": "BTN1",
        "SW1.2": "BTN1",
        "SW1.3": "GND",
        "SW1.4": "GND",
        "R17.1": "VDD",
        "R17.2": "BTN1",
        "C63.1": "BTN1",
        "C63.2": "GND",
        # TEMT6200: C=VDD, E=LIGHT_SENSE; R14 2k7 -> Q2 drain; Q2 gate <- P2.06.
        "Q1.2": "VDD",
        "Q1.1": "PHOTO_SIG",
        "C62.1": "PHOTO_SIG",
        "C62.2": "GND",
        "R14.1": "PHOTO_SIG",
        "R14.2": "PHOTO_FET",
        "Q2.3": "PHOTO_FET",
        "Q2.2": "GND",
        "Q2.1": "PHOTO_GATE",
        "R15.1": "PHOTO_CTRL",
        "R15.2": "PHOTO_GATE",
        "R16.1": "PHOTO_GATE",
        "R16.2": "GND",
    },
}


def main() -> None:
    snap = Path(sys.argv[1])
    netmap: dict[str, dict[str, str]] = {}
    for f in sorted(snap.glob("0*.kicad_sch")):
        sheet = f.stem
        sh = m.load(f)
        res = m.intended_nets(sh)
        pins: dict[str, str] = {}
        for (ref, num), d in res.items():
            names = sorted(d["names"])
            if len(names) == 1:
                pins[f"{ref}.{num}"] = names[0]
            elif len(names) > 1:
                print(f"WARN {sheet} {ref}.{num} ambiguous {names}")
        if sheet in OVERRIDES:
            if sheet == "05_peripherals":
                pins = {}
            for k, v in OVERRIDES[sheet].items():
                if v is None:
                    pins.pop(k, None)
                else:
                    pins[k] = v
        netmap[sheet] = dict(sorted(pins.items()))
    OUT.write_text(json.dumps(netmap, indent=2) + "\n")
    print(f"wrote {OUT}")
    for s, p in netmap.items():
        print(f"  {s}: {len(p)} pins")


if __name__ == "__main__":
    main()
