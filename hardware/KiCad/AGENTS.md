# KiCad agents — Juxta6 hardware

Guidelines for AI agents working under `hardware/KiCad/`. Firmware rules in [`agents/RULES.md`](../../agents/RULES.md) still apply; this file narrows EDA scope.

## Scope

- **In scope:** schematic authoring and review (symbols, nets, power, hierarchical sheets, ERC, BOM fields, LCSC library import).
- **Out of scope for AI:** PCB component placement, routing, copper pours, length tuning, and manufacturing release. The human owns layout and fab handoff.
- **Reference hardware:** Nordic nRF54L15 Tag (`hardware/nrf54l15-tag/`, PCA20072). Juxta6 is a custom PCB derived from that contract — do not invent pinouts that contradict the Tag hardware reference without an explicit design decision.

## Active project

| Item | Path |
|------|------|
| Project | `hardware/KiCad/juxta6_v1-0/` |
| Schematic / PCB | `juxta6_v1-0.kicad_sch` / `juxta6_v1-0.kicad_pcb` |
| Project LCSC libs | `juxta6_v1-0/libs/juxta6.{kicad_sym,pretty,3dshapes}` |
| Project lib tables | `juxta6_v1-0/{sym,fp}-lib-table` → `${KIPRJMOD}/libs/juxta6…` |
| LCSC manifest | `parts/lcsc_manifest.csv` |
| Sourcing decisions | `parts/LCSC_Sourcing_Decision_Aid.md` |
| Tag BOM / contract | `../nrf54l15-tag/` |

KiCad **10** with the **IPC / API server** enabled is assumed (user-owned). Prefer MCP tools over hand-editing `.kicad_sch` / `.kicad_pcb` S-expressions when the MCP server is connected.

If `juxta6_v1-0.kicad_pro` is empty/`{}`, open the project in KiCad once and **Save** so project settings (and API project binding) are written properly.

## Tooling

### easyeda2kicad (LCSC → KiCad libs)

Installed on this machine as `easyeda2kicad` (v1.0.1). Pulls symbol + footprint + 3D from LCSC/EasyEDA into the project lib:

```bash
easyeda2kicad --full --lcsc_id C3664478 \
  --output hardware/KiCad/juxta6_v1-0/libs/juxta6 \
  --project-relative --use-cache
```

Helpers:

```bash
python3 hardware/KiCad/scripts/search_jlc.py BME688
python3 hardware/KiCad/scripts/pull_parts.py          # pulls all rows with lcsc_id in the manifest
python3 hardware/KiCad/scripts/pull_parts.py --dry-run
```

**Rules for imports**

1. Prefer exact MPN match from the Tag BOM / Juxta6 design intent (package matters — e.g. W25Q64 **USON-8**, not SOP).
2. Record the chosen `C######` in `parts/lcsc_manifest.csv` with notes (stock, package caveats).
3. Always double-check pin order and footprint against the datasheet; EasyEDA conversions are not guaranteed correct.
4. **Passives are not imported** via easyeda2kicad (resistors, caps, ferrites, matching L/C). Use KiCad Device libraries with **0201 preferred**; pick JLCPCB Basic parts at fab time. Crystals, LEDs, antennas, connectors, switches, and ICs *are* specialized imports.
5. Do not commit `.easyeda_cache/` (local API cache).
6. EasyEDA rate-limits batches (HTTP 403). Pull in small groups with `pull_parts.py --sleep 3`; retry `pending_import` rows later.
7. Use `scripts/stock_report.py` (and JLCPCB search) before locking alts — many Tag MPNs have **0** JLC stock.

### kicad-mcp-pro (JLCPCB-recommended MCP)

Upstream: [oaslananka/kicad-mcp-pro](https://github.com/oaslananka/kicad-mcp-pro).

**Fit for Juxta6:** good for schematic inspect/edit, ERC, BOM/sourcing, and read-only review. **Do not** use PCB placement or routing tools here. Prefer profile `schematic_authoring` (or `build` for bounded plan/apply/verify) over `expert`/`full`.

Recommended Cursor env (see `.cursor/mcp.json`):

| Variable | Juxta6 value | Why |
|----------|--------------|-----|
| `KICAD_MCP_PROJECT_DIR` | absolute path to `juxta6_v1-0` | Persistent project |
| `KICAD_MCP_PROFILE` | `schematic_authoring` | Schematic-focused tool surface |
| `KICAD_MCP_OPERATING_MODE` | `readonly` default; `write` only when editing | Risk gate independent of profile |

Safe loop: **inspect → plan → (human OK) → apply → ERC / schematic preview → summarize**. Never claim fab readiness without human layout + DRC + DFM.

CLI on this machine: `uv` / `uvx` in `~/.local/bin`, `kicad-mcp-pro` installed via `uv tool install`. Run `kicad-mcp-pro doctor` after KiCad path or MCP config changes. Open the board/schematic in KiCad with the API enabled when live IPC tools are needed.

## Schematic workflow for agents

1. Confirm project path and that `juxta6` symbol/footprint libs are on the project library tables.
2. Use Tag hardware reference + `lcsc_manifest.csv` for part identity; do not rename nets that firmware already depends on (e.g. shared TWI `P1.11`/`P1.12`, flash CS `P2.05`, `ANTSEL`) without an explicit change request.
3. For new parts: search JLCPCB → pick LCSC ID → `pull_parts.py` / `easyeda2kicad` → place on schematic via MCP or human → set fields (`LCSC`, MPN, value).
4. After edits: run ERC; report failures; do not “fix” ERC by silencing rules unless asked.
5. Stop before PCB: leave placement/routing to the human.

## Guardrails (hardware)

- **Motion (Juxta PCB):** **BMI270 only** (SPI) — low-power impulse + optional 6-axis. Do **not** place ADXL367 (Tag dual-sensor layout does not apply to juxta6_v1-0).
- Project flash is **W25Q64JWUUIQ** / **C6604692** (1.8 V, 64 Mbit USON-8). Do **not** place MX25L3233 (2.65 V min) or Tag MX25R6435.
- RF: dual antennas retained; switch is **PE42421** (SC-70, 1.8 V capable at VDD min). Not Tag **SKY13348**; not **AS179-92LF** (needs ~3 V control). Verify PE42421 CTRL vs Tag `ANTSEL`; retune matching at 1.8 V.
- **Power (Juxta):** **TPS63900** buck-boost; **Vout locked 1.8 V**; **CR2032 XOR JST PH LiPo** (never both energized). Default **Bat1 and J1 DNP**; DigiKey hand-place the chosen source. LiPo connector locked to **C295747** `S2B-PH-SM4-TB` (RA SMD). Coin holder: DigiKey `BU2032SM-FH-GTR` (alt LCSC **C964758`). `parts/import/` is staging only.
- **Passives:** prefer **0201** footprints where able; larger only for bulk / power / RF Q needs.
- **Debug:** no 2×5 Tag-style connector. Use **five testpoints**: VDD, GND, SWDIO, SWDCLK, RESET. Do not invent pitch/order without asking.

## What not to do

- Do not run `west build` / flash from hardware work (firmware ownership stays with the user).
- Do not edit `hardware/nrf54l15-tag/` Nordic source trees as if they were the Juxta6 KiCad project.
- Do not enable AI placement/routing “to save time.”
- Do not paste large copyrighted datasheet text into the repo; link MPNs / URLs instead.
