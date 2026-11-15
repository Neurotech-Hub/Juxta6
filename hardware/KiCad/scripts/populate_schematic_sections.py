#!/usr/bin/env python3
"""Populate juxta6_v1-0 hierarchical schematic sections (floorplan sprint).

Creates distinct child sheets under juxta6_v1-0.kicad_sch with locked
specialized parts + generic Device passives (0201 preferred) and footprints.
No net wiring — placeholders for PCB area check.
"""
from __future__ import annotations

import re
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJ = ROOT / "juxta6_v1-0"
JUXT_SYM = PROJ / "libs" / "juxta6.kicad_sym"
DEVICE_SYM = Path(
    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols/Device.kicad_sym"
)
CONN_SYM = Path(
    "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols/Connector.kicad_sym"
)

FP_R0201 = "Resistor_SMD:R_0201_0603Metric"
FP_C0201 = "Capacitor_SMD:C_0201_0603Metric"
FP_C0603 = "Capacitor_SMD:C_0603_1608Metric"
FP_L0201 = "Inductor_SMD:L_0201_0603Metric"
FP_L0603 = "Inductor_SMD:L_0603_1608Metric"
FP_L0805 = "Inductor_SMD:L_0805_2012Metric"
FP_TP = "TestPoint:TestPoint_Pad_D1.5mm"
# Aliases used throughout placement lists
FP_R0402 = FP_R0201
FP_C0402 = FP_C0201
FP_L0402 = FP_L0201


def new_uuid() -> str:
    return str(uuid.uuid4())


def extract_symbol(path: Path, name: str) -> str:
    text = path.read_text()
    m = re.search(rf'\n([ \t]*)\(symbol "{re.escape(name)}"\n', text)
    if not m:
        raise KeyError(f"{name} not in {path}")
    start = m.start() + 1
    i = text.find("(", start)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return text[i : j + 1]
    raise RuntimeError(f"unbalanced symbol {name}")


def retitle_symbol(sym_body: str, lib_id: str) -> str:
    """Rewrite top-level (symbol "Name" → (symbol "Lib:Name"."""
    body = re.sub(
        r'^\(symbol "[^"]+"',
        f'(symbol "{lib_id}"',
        sym_body,
        count=1,
    )
    # EasyEDA imports can embed non-ASCII (e.g. manufacturer); strip for KiCad parse safety
    body = "".join(c if ord(c) < 128 else "" for c in body)
    body = body.replace("Winbond()", "Winbond")
    return body


def pin_numbers(sym_body: str) -> list[str]:
    nums: list[str] = []
    for m in re.finditer(r'\(pin\b[\s\S]*?\(number\s+"([^"]+)"', sym_body):
        n = m.group(1)
        if n not in nums:
            nums.append(n)
    return nums


def prop(
    name: str,
    value: str,
    at: tuple[float, float, int],
    *,
    hide: bool = False,
    size: float = 1.27,
) -> str:
    x, y, rot = at
    hide_s = "\n\t\t\t\t\t(hide yes)" if hide else ""
    return f"""\t\t(property "{name}" "{value}"
\t\t\t(at {x} {y} {rot})
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size {size} {size})
\t\t\t\t){hide_s}
\t\t\t)
\t\t)"""


def format_symbol_instance(
    *,
    lib_id: str,
    ref: str,
    value: str,
    footprint: str,
    x: float,
    y: float,
    rot: int,
    pins: list[str],
    sheet_path: str,
    project: str,
    dnp: bool = False,
    fields: dict[str, str] | None = None,
) -> str:
    uid = new_uuid()
    dnp_s = "yes" if dnp else "no"
    pin_block = "\n".join(
        f'\t\t(pin "{p}"\n\t\t\t(uuid "{new_uuid()}")\n\t\t)' for p in pins
    )
    extra = ""
    if fields:
        for k, v in fields.items():
            extra += "\n" + prop(k, v, (x, y - 7.62, 0), hide=True)
    return f"""\t(symbol
\t\t(lib_id "{lib_id}")
\t\t(at {x:.2f} {y:.2f} {rot})
\t\t(unit 1)
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(dnp {dnp_s})
\t\t(uuid "{uid}")
{prop("Reference", ref, (x, y - 2.54, 0))}
{prop("Value", value, (x, y + 2.54, 0))}
{prop("Footprint", footprint, (x, y + 5.08, 0), hide=True)}
{prop("Datasheet", "", (x, y, 0), hide=True)}
{prop("Description", "", (x, y, 0), hide=True)}{extra}
{pin_block}
\t\t(instances
\t\t\t(project "{project}"
\t\t\t\t(path "{sheet_path}"
\t\t\t\t\t(reference "{ref}")
\t\t\t\t\t(unit 1)
\t\t\t\t)
\t\t\t)
\t\t)
\t)"""


def text_box(x: float, y: float, w: float, h: float, title: str, body: str) -> str:
    return f"""\t(text_box "{title}\\n{body}"
\t\t(exclude_from_sim no)
\t\t(at {x:.2f} {y:.2f} 0)
\t\t(size {w:.2f} {h:.2f})
\t\t(margins 0.508 0.508 0.508 0.508)
\t\t(stroke
\t\t\t(width 0)
\t\t\t(type default)
\t\t)
\t\t(fill
\t\t\t(type none)
\t\t)
\t\t(uuid "{new_uuid()}")
\t\t(effects
\t\t\t(font
\t\t\t\t(size 1.27 1.27)
\t\t\t)
\t\t\t(justify left top)
\t\t)
\t)"""


def sheet_file(
    *,
    uuid_sheet: str,
    title: str,
    paper: str,
    lib_symbols: list[str],
    body_items: list[str],
) -> str:
    libs = "\n".join(lib_symbols)
    body = "\n".join(body_items)
    return f"""(kicad_sch
\t(version 20260306)
\t(generator "eeschema")
\t(generator_version "10.0")
\t(uuid "{uuid_sheet}")
\t(paper "{paper}")
\t(title_block
\t\t(title "{title}")
\t\t(comment 1 "Juxta6 v1-0 floorplan placeholders — values TBD")
\t)
\t(lib_symbols
{libs}
\t)
{body}
\t(sheet_instances
\t\t(path "/"
\t\t\t(page "?")
\t\t)
\t)
\t(embedded_fonts no)
)
"""


def grid_place(
    items: list[dict],
    *,
    origin: tuple[float, float],
    pitch_x: float,
    pitch_y: float,
    cols: int,
    lib_pins: dict[str, list[str]],
    sheet_path: str,
    project: str,
) -> list[str]:
    out = []
    ox, oy = origin
    for i, it in enumerate(items):
        col = i % cols
        row = i // cols
        x = ox + col * pitch_x
        y = oy + row * pitch_y
        out.append(
            format_symbol_instance(
                lib_id=it["lib_id"],
                ref=it["ref"],
                value=it["value"],
                footprint=it["footprint"],
                x=x,
                y=y,
                rot=it.get("rot", 0),
                pins=lib_pins[it["lib_id"]],
                sheet_path=sheet_path,
                project=project,
                dnp=it.get("dnp", False),
                fields=it.get("fields"),
            )
        )
    return out


def main() -> None:
    project = "juxta6_v1-0"
    root_uuid = "698e8d87-293e-4fa1-bca5-9aae2c2e31e2"  # keep existing root uuid

    # Child sheet UUIDs (stable-ish new)
    sheets_meta = [
        ("01_power", "Power", "TPS63900 + Bat1/J1 (DNP) + CFG/bulk"),
        ("02_mcu_clocks", "MCU_Clocks", "nRF54L15 + X1/X2 + DCDC/decouple"),
        ("03_rf", "RF", "PE42421 + A1/A2 + match network"),
        ("04_sensors", "Sensors", "BME688 + BMI270 + TWI pulls"),
        ("05_peripherals", "Peripherals", "Flash + LED1 + SW1 + photo"),
        ("06_debug", "Debug", "SWD testpoints (5 pads)"),
    ]
    sheet_uuids = {name: new_uuid() for name, _, _ in sheets_meta}

    # Load / cache symbols
    raw = {
        "juxta6:NRF54L15-QFAA-R": extract_symbol(JUXT_SYM, "NRF54L15-QFAA-R"),
        "juxta6:TPS63900DSKR": extract_symbol(JUXT_SYM, "TPS63900DSKR"),
        "juxta6:PE42421SCAA-Z-1": extract_symbol(JUXT_SYM, "PE42421SCAA-Z-1"),
        "juxta6:BME688_C3664478": extract_symbol(JUXT_SYM, "BME688_C3664478"),
        "juxta6:BMI270": extract_symbol(JUXT_SYM, "BMI270"),
        "juxta6:W25Q64JWUUIQ": extract_symbol(JUXT_SYM, "W25Q64JWUUIQ"),
        "juxta6:2450AT18D0100001E": extract_symbol(JUXT_SYM, "2450AT18D0100001E"),
        "juxta6:APHF1608LSEEQBDZGKC": extract_symbol(JUXT_SYM, "APHF1608LSEEQBDZGKC"),
        "juxta6:BU2032SM-FH-GTR": extract_symbol(JUXT_SYM, "BU2032SM-FH-GTR"),
        "juxta6:S2B-PH-SM4-TB": extract_symbol(JUXT_SYM, "S2B-PH-SM4-TB"),
        "juxta6:TEMT6200FX01": extract_symbol(JUXT_SYM, "TEMT6200FX01"),
        "juxta6:RV2C010UNT2L": extract_symbol(JUXT_SYM, "RV2C010UNT2L"),
        "juxta6:3.2X4.2X3.0DH260G-QBPJ": extract_symbol(
            JUXT_SYM, "3.2X4.2X3.0DH260G-QBPJ"
        ),
        "juxta6:FC-12M32.768KHZ9PF20PPM": extract_symbol(
            JUXT_SYM, "FC-12M32.768KHZ9PF20PPM"
        ),
        "juxta6:X201632MKB4SI": extract_symbol(JUXT_SYM, "X201632MKB4SI"),
        "Device:R": extract_symbol(DEVICE_SYM, "R"),
        "Device:C": extract_symbol(DEVICE_SYM, "C"),
        "Device:L": extract_symbol(DEVICE_SYM, "L"),
        "Device:FerriteBead": extract_symbol(DEVICE_SYM, "FerriteBead"),
        "Connector:TestPoint": extract_symbol(CONN_SYM, "TestPoint"),
    }
    embedded = {k: retitle_symbol(v, k) for k, v in raw.items()}
    pins = {k: pin_numbers(v) for k, v in raw.items()}

    def libs_for(*ids: str) -> list[str]:
        # Device/Connector first: EasyEDA-style juxta6 symbols first in lib_symbols
        # can make KiCad 10 fail to load the following symbol definitions.
        ordered = sorted(
            ids,
            key=lambda i: (0 if i.startswith(("Device:", "Connector:")) else 1, i),
        )
        out = []
        for i in ordered:
            body = embedded[i]
            lines = body.splitlines()
            indented = "\n".join("\t" + ln if ln else ln for ln in lines)
            out.append(indented)
        return out

    # ---- Power sheet ----
    power_path = f"/{root_uuid}/{sheet_uuids['01_power']}"
    power_items = [
        {
            "lib_id": "juxta6:TPS63900DSKR",
            "ref": "U2",
            "value": "TPS63900DSKR",
            "footprint": "juxta6:WSON-10_L2.5-W2.5-W0.50-TL-EP",
            "fields": {"LCSC": "C1518762"},
        },
        {
            "lib_id": "juxta6:BU2032SM-FH-GTR",
            "ref": "Bat1",
            "value": "BU2032SM-FH-GTR",
            "footprint": "juxta6:BU2032SM-FH-GTR_MPD",
            "dnp": True,
            "fields": {"LCSC": "C6755625", "Assembly": "DNP default"},
        },
        {
            "lib_id": "juxta6:S2B-PH-SM4-TB",
            "ref": "J1",
            "value": "S2B-PH-SM4-TB",
            "footprint": "juxta6:CONN-SMD_P2.00_S2B-PH-SM4-TB-LF-SN",
            "dnp": True,
            "fields": {"LCSC": "C295747", "Assembly": "DNP default"},
        },
        # CFG resistors
        *[{"lib_id": "Device:R", "ref": f"R{n}", "value": "CFG_TBD", "footprint": FP_R0402} for n in (1, 2, 3)],
        {"lib_id": "Device:C", "ref": "C1", "value": "10u Cin", "footprint": FP_C0603},
        {"lib_id": "Device:C", "ref": "C2", "value": "22u Cout", "footprint": FP_C0603},
        {"lib_id": "Device:C", "ref": "C3", "value": "100n VIN", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C4", "value": "100n VOUT", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C5", "value": "47u bulk", "footprint": FP_C0603},
        {"lib_id": "Device:C", "ref": "C6", "value": "100n Bat/J1", "footprint": FP_C0402},
        {"lib_id": "Device:L", "ref": "L1", "value": "2u2 TPS", "footprint": FP_L0805},
        {"lib_id": "Device:FerriteBead", "ref": "FB1", "value": "FB_TBD", "footprint": FP_L0402},
        {"lib_id": "Device:FerriteBead", "ref": "FB2", "value": "FB_TBD", "footprint": FP_L0402},
    ]
    power_body = [
        text_box(
            20,
            20,
            80,
            18,
            "POWER",
            "TPS63900 buck-boost; Bat1 XOR J1 (both DNP default). Vout = 1.8 V.",
        ),
        *grid_place(
            power_items,
            origin=(30, 45),
            pitch_x=25.4,
            pitch_y=25.4,
            cols=5,
            lib_pins=pins,
            sheet_path=power_path,
            project=project,
        ),
    ]
    (PROJ / "01_power.kicad_sch").write_text(
        sheet_file(
            uuid_sheet=sheet_uuids["01_power"],
            title="Power",
            paper="A3",
            lib_symbols=libs_for(
                "juxta6:TPS63900DSKR",
                "juxta6:BU2032SM-FH-GTR",
                "juxta6:S2B-PH-SM4-TB",
                "Device:R",
                "Device:C",
                "Device:L",
                "Device:FerriteBead",
            ),
            body_items=power_body,
        )
    )

    # ---- MCU / clocks ----
    mcu_path = f"/{root_uuid}/{sheet_uuids['02_mcu_clocks']}"
    mcu_items = [
        {
            "lib_id": "juxta6:NRF54L15-QFAA-R",
            "ref": "U1",
            "value": "NRF54L15-QFAA-R",
            "footprint": "juxta6:QFN-48_L6.0-W6.0-P0.40-TL-EP4.6",
            "fields": {"LCSC": "C42458750"},
        },
        {
            "lib_id": "juxta6:FC-12M32.768KHZ9PF20PPM",
            "ref": "X1",
            "value": "X1A0000610008",
            "footprint": "juxta6:OSC-SMD_L2.0-W1.2",
            "fields": {"LCSC": "C96341"},
        },
        {
            "lib_id": "juxta6:X201632MKB4SI",
            "ref": "X2",
            "value": "X201632MKB4SI",
            "footprint": "juxta6:CRYSTAL-SMD_4P-L2.0-W1.6-BL",
            "fields": {"LCSC": "C718072"},
        },
        {"lib_id": "Device:L", "ref": "L2", "value": "4u7 DCDC", "footprint": FP_L0603},
        {"lib_id": "Device:R", "ref": "R4", "value": "RESET_PU TBD", "footprint": FP_R0402},
        {"lib_id": "Device:C", "ref": "C10", "value": "2u2", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C11", "value": "2u2", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C12", "value": "10u", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C13", "value": "100p", "footprint": FP_C0402},
        *[{"lib_id": "Device:C", "ref": f"C{n}", "value": "100n", "footprint": FP_C0402} for n in range(14, 26)],  # 12x 100n
        {"lib_id": "Device:C", "ref": "C26", "value": "18p X1", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C27", "value": "CL X2a", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C28", "value": "CL X2b", "footprint": FP_C0402},
    ]
    mcu_body = [
        text_box(
            20,
            20,
            90,
            18,
            "MCU + CLOCKS",
            "nRF54L15 + LF/HF crystals + DCDC L + decouple bank (0201 preferred).",
        ),
        *grid_place(
            mcu_items,
            origin=(35, 50),
            pitch_x=22.86,
            pitch_y=22.86,
            cols=6,
            lib_pins=pins,
            sheet_path=mcu_path,
            project=project,
        ),
    ]
    (PROJ / "02_mcu_clocks.kicad_sch").write_text(
        sheet_file(
            uuid_sheet=sheet_uuids["02_mcu_clocks"],
            title="MCU_Clocks",
            paper="A3",
            lib_symbols=libs_for(
                "juxta6:NRF54L15-QFAA-R",
                "juxta6:FC-12M32.768KHZ9PF20PPM",
                "juxta6:X201632MKB4SI",
                "Device:R",
                "Device:C",
                "Device:L",
            ),
            body_items=mcu_body,
        )
    )

    # ---- RF ----
    rf_path = f"/{root_uuid}/{sheet_uuids['03_rf']}"
    rf_items = [
        {
            "lib_id": "juxta6:PE42421SCAA-Z-1",
            "ref": "U6",
            "value": "PE42421SCAA-Z",
            "footprint": "juxta6:SC-70-6_L2.1-W1.3-P0.65-LS2.1-BR",
            "fields": {"LCSC": "C470893"},
        },
        {
            "lib_id": "juxta6:2450AT18D0100001E",
            "ref": "A1",
            "value": "2450AT18D0100001E",
            "footprint": "juxta6:ANT-SMD_4P-L3.2-W1.6",
            "fields": {"LCSC": "C7450310"},
        },
        {
            "lib_id": "juxta6:2450AT18D0100001E",
            "ref": "A2",
            "value": "2450AT18D0100001E",
            "footprint": "juxta6:ANT-SMD_4P-L3.2-W1.6",
            "fields": {"LCSC": "C7450310"},
        },
        {"lib_id": "Device:C", "ref": "C30", "value": "100n U6 VDD", "footprint": FP_C0402},
        {"lib_id": "Device:R", "ref": "R5", "value": "CTRL_PU TBD", "footprint": FP_R0402},
        *[{"lib_id": "Device:C", "ref": f"C{n}", "value": "RF_C TBD", "footprint": FP_C0402} for n in range(31, 43)],  # 12 RF C
        *[{"lib_id": "Device:L", "ref": f"L{n}", "value": "RF_nH TBD", "footprint": FP_L0402} for n in range(3, 7)],  # L3-L6
    ]
    rf_body = [
        text_box(
            20,
            20,
            100,
            20,
            "RF",
            "PE42421 (not SKY pinout) + dual Johanson A1/A2. Match network TBD / retune.",
        ),
        *grid_place(
            rf_items,
            origin=(30, 50),
            pitch_x=22.86,
            pitch_y=22.86,
            cols=6,
            lib_pins=pins,
            sheet_path=rf_path,
            project=project,
        ),
    ]
    (PROJ / "03_rf.kicad_sch").write_text(
        sheet_file(
            uuid_sheet=sheet_uuids["03_rf"],
            title="RF",
            paper="A3",
            lib_symbols=libs_for(
                "juxta6:PE42421SCAA-Z-1",
                "juxta6:2450AT18D0100001E",
                "Device:R",
                "Device:C",
                "Device:L",
            ),
            body_items=rf_body,
        )
    )

    # ---- Sensors ----
    sens_path = f"/{root_uuid}/{sheet_uuids['04_sensors']}"
    sens_items = [
        {
            "lib_id": "juxta6:BME688_C3664478",
            "ref": "U3",
            "value": "BME688",
            "footprint": "juxta6:LGA-8_L3.0-W3.0-P0.80_BME688",
            "fields": {"LCSC": "C3664478"},
        },
        {
            "lib_id": "juxta6:BMI270",
            "ref": "U4",
            "value": "BMI270",
            "footprint": "juxta6:LGA-14_L3.0-W2.5-P0.50-BR",
            "fields": {"LCSC": "C2836813"},
        },
        {"lib_id": "Device:R", "ref": "R6", "value": "10k SDA", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R7", "value": "10k SCL", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R8", "value": "SPI series TBD", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R9", "value": "SPI series TBD", "footprint": FP_R0402},
        {"lib_id": "Device:C", "ref": "C50", "value": "100n U3", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C51", "value": "100n U3", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C52", "value": "100n U4 VDD", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C53", "value": "100n U4 VDDIO", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C54", "value": "100n U4", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C55", "value": "100n U4", "footprint": FP_C0402},
    ]
    sens_body = [
        text_box(
            20,
            20,
            90,
            18,
            "SENSORS",
            "BME688 (TWI) + BMI270 only (no ADXL367).",
        ),
        *grid_place(
            sens_items,
            origin=(30, 50),
            pitch_x=25.4,
            pitch_y=25.4,
            cols=4,
            lib_pins=pins,
            sheet_path=sens_path,
            project=project,
        ),
    ]
    (PROJ / "04_sensors.kicad_sch").write_text(
        sheet_file(
            uuid_sheet=sheet_uuids["04_sensors"],
            title="Sensors",
            paper="A3",
            lib_symbols=libs_for(
                "juxta6:BME688_C3664478",
                "juxta6:BMI270",
                "Device:R",
                "Device:C",
            ),
            body_items=sens_body,
        )
    )

    # ---- Peripherals ----
    peri_path = f"/{root_uuid}/{sheet_uuids['05_peripherals']}"
    peri_items = [
        {
            "lib_id": "juxta6:W25Q64JWUUIQ",
            "ref": "U8",
            "value": "W25Q64JWUUIQ",
            "footprint": "juxta6:USON-8_L4.0-W3.0-P0.80-BL_WINBOND_UU",
            "fields": {"LCSC": "C6604692"},
        },
        {
            "lib_id": "juxta6:APHF1608LSEEQBDZGKC",
            "ref": "LED1",
            "value": "APHF1608LSEEQBDZGKC",
            "footprint": "juxta6:LED-SMD_4P-L1.6-W0.8_APHF1608LSEEQBDZGKC",
            "fields": {"LCSC": "C3019171"},
        },
        {
            "lib_id": "juxta6:3.2X4.2X3.0DH260G-QBPJ",
            "ref": "SW1",
            "value": "3.2X4.2X3.0DH260G-QBPJ",
            "footprint": "juxta6:SW-SMD_4P-L4.2-W3.3-P2.15-LS5.2",
            "fields": {"LCSC": "C55232901"},
        },
        {
            "lib_id": "juxta6:TEMT6200FX01",
            "ref": "Q1",
            "value": "TEMT6200FX01",
            "footprint": "juxta6:SENSORS-SMD_0805",
            "fields": {"LCSC": "C143695"},
        },
        {
            "lib_id": "juxta6:RV2C010UNT2L",
            "ref": "Q2",
            "value": "RV2C010UNT2L",
            "footprint": "juxta6:X2-DFN1006-3_L1.0-W0.6-BR",
            "fields": {"LCSC": "C570828"},
        },
        {
            "lib_id": "juxta6:RV2C010UNT2L",
            "ref": "Q3",
            "value": "RV2C010UNT2L",
            "footprint": "juxta6:X2-DFN1006-3_L1.0-W0.6-BR",
            "fields": {"LCSC": "C570828"},
        },
        {"lib_id": "Device:R", "ref": "R10", "value": "100k flash PU", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R11", "value": "LED R 1.8V TBD", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R12", "value": "LED G 1.8V TBD", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R13", "value": "LED B 1.8V TBD", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R14", "value": "2k7 photo", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R15", "value": "photo gate TBD", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R16", "value": "photo pull TBD", "footprint": FP_R0402},
        {"lib_id": "Device:R", "ref": "R17", "value": "SW1 PU TBD", "footprint": FP_R0402},
        {"lib_id": "Device:C", "ref": "C60", "value": "100n flash", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C61", "value": "100n flash", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C62", "value": "photo/debounce", "footprint": FP_C0402},
        {"lib_id": "Device:C", "ref": "C63", "value": "SW1 debounce", "footprint": FP_C0402},
    ]
    peri_body = [
        text_box(
            20,
            20,
            100,
            18,
            "PERIPHERALS",
            "W25Q64 (1.8V) + RGB LED1 + SW1 + TEMT6200 photo + FETs.",
        ),
        *grid_place(
            peri_items,
            origin=(30, 50),
            pitch_x=25.4,
            pitch_y=25.4,
            cols=5,
            lib_pins=pins,
            sheet_path=peri_path,
            project=project,
        ),
    ]
    (PROJ / "05_peripherals.kicad_sch").write_text(
        sheet_file(
            uuid_sheet=sheet_uuids["05_peripherals"],
            title="Peripherals",
            paper="A3",
            lib_symbols=libs_for(
                "juxta6:W25Q64JWUUIQ",
                "juxta6:APHF1608LSEEQBDZGKC",
                "juxta6:3.2X4.2X3.0DH260G-QBPJ",
                "juxta6:TEMT6200FX01",
                "juxta6:RV2C010UNT2L",
                "Device:R",
                "Device:C",
            ),
            body_items=peri_body,
        )
    )

    # ---- Debug ----
    dbg_path = f"/{root_uuid}/{sheet_uuids['06_debug']}"
    dbg_items = [
        {
            "lib_id": "Connector:TestPoint",
            "ref": "TP1",
            "value": "VDD",
            "footprint": FP_TP,
        },
        {
            "lib_id": "Connector:TestPoint",
            "ref": "TP2",
            "value": "GND",
            "footprint": FP_TP,
        },
        {
            "lib_id": "Connector:TestPoint",
            "ref": "TP3",
            "value": "SWDIO",
            "footprint": FP_TP,
        },
        {
            "lib_id": "Connector:TestPoint",
            "ref": "TP4",
            "value": "SWDCLK",
            "footprint": FP_TP,
        },
        {
            "lib_id": "Connector:TestPoint",
            "ref": "TP5",
            "value": "RESET",
            "footprint": FP_TP,
        },
    ]
    dbg_body = [
        text_box(
            20,
            20,
            90,
            18,
            "DEBUG / SWD",
            "Five pads only — no 2x5. Pitch/order TBD.",
        ),
        *grid_place(
            dbg_items,
            origin=(40, 50),
            pitch_x=25.4,
            pitch_y=25.4,
            cols=5,
            lib_pins=pins,
            sheet_path=dbg_path,
            project=project,
        ),
    ]
    (PROJ / "06_debug.kicad_sch").write_text(
        sheet_file(
            uuid_sheet=sheet_uuids["06_debug"],
            title="Debug",
            paper="A4",
            lib_symbols=libs_for("Connector:TestPoint"),
            body_items=dbg_body,
        )
    )

    # ---- Root sheet with hierarchical symbols ----
    def root_sheet_symbol(
        name: str, filename: str, sheet_uuid: str, x: float, y: float, desc: str
    ) -> str:
        return f"""\t(sheet
\t\t(at {x:.2f} {y:.2f})
\t\t(size 50.8 25.4)
\t\t(exclude_from_sim no)
\t\t(stroke
\t\t\t(width 0.1524)
\t\t\t(type solid)
\t\t)
\t\t(fill
\t\t\t(type none)
\t\t)
\t\t(uuid "{sheet_uuid}")
\t\t(property "Sheetname" "{name}"
\t\t\t(at {x:.2f} {(y - 0.5):.2f} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(justify left bottom)
\t\t\t)
\t\t)
\t\t(property "Sheetfile" "{filename}"
\t\t\t(at {x:.2f} {(y + 25.9):.2f} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(justify left top)
\t\t\t)
\t\t)
\t\t(property "Description" "{desc}"
\t\t\t(at {x:.2f} {(y + 12.7):.2f} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(justify left)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t)"""

    positions = [
        (25.4, 40.0),
        (90.0, 40.0),
        (154.6, 40.0),
        (25.4, 90.0),
        (90.0, 90.0),
        (154.6, 90.0),
    ]
    root_sheets = []
    for (fname, sname, desc), (x, y) in zip(sheets_meta, positions):
        root_sheets.append(
            root_sheet_symbol(sname, f"{fname}.kicad_sch", sheet_uuids[fname], x, y, desc)
        )

    root = f"""(kicad_sch
\t(version 20260306)
\t(generator "eeschema")
\t(generator_version "10.0")
\t(uuid "{root_uuid}")
\t(paper "A3")
\t(title_block
\t\t(title "Juxta6 v1-0")
\t\t(comment 1 "Hierarchical floorplan sheets — specialize + passive placeholders")
\t\t(comment 2 "See parts/Passive_Budget_Estimate.md")
\t)
\t(lib_symbols
\t)
{text_box(20, 20, 120, 14, "JUXTA6 v1-0", "Open child sheets for Power / MCU / RF / Sensors / Peripherals / Debug.")}
{chr(10).join(root_sheets)}
\t(sheet_instances
\t\t(path "/"
\t\t\t(page "1")
\t\t)
\t)
\t(embedded_fonts no)
)
"""
    (PROJ / "juxta6_v1-0.kicad_sch").write_text(root)

    # Ensure project knows Device/Connector/Resistor/Capacitor/Inductor/TestPoint libs
    # via global KiCad tables — usually already present. Project tables only have juxta6.

    print("Wrote hierarchical schematic sections:")
    for fname, sname, _ in sheets_meta:
        print(f"  {fname}.kicad_sch  ({sname}) uuid={sheet_uuids[fname]}")
    print("Root: juxta6_v1-0.kicad_sch")


if __name__ == "__main__":
    main()
