# CHIRP driver: UV-K5 (F4HWN Services)

A CHIRP driver for this firmware's **Services** edition. Pick the radio model
**Quansheng → UV-K5 (F4HWN Services)**.

## Files
| File | What it is |
|---|---|
| `uvk5_f4hwn_services.py` | The driver CHIRP loads. Generated, so don't edit it by hand. |
| `make_driver.py` | Builds the driver from the upstream F4HWN driver by applying exact-match patches. Run `python make_driver.py` after changing it. |
| `upstream/` | armel/uv-k5-chirp-driver `uvk5_egzumer_f4hwn_ver_4_3_0.py` (F4HWN v4.3), unmodified. |

## Running it
CHIRP has no permanent place for extra drivers. It loads one at startup with
`--module`, and the **CHIRP (UV-K5 Services)** shortcut (Desktop and Start menu)
does exactly that:

    "C:\Program Files (x86)\CHIRP\chirpwx.exe" --module "C:\dev\uv-k5v1-firmware\Chirp\uvk5_f4hwn_services.py"

The window title shows "Module Loaded". You can also load it by hand: Help → Developer
Mode, then File → Load Module.

## What it adds to the F4HWN v4.3 driver
- **Power:** all eight F4HWN levels (USER, L1–L5, MID, HIGH). The stock CHIRP
  UV-K5 driver's "High" is stored as 2, which this firmware reads as LOW 2 (125 mW).
- **Groups:** the Banks tab shows Group 1–3, the memory groups you step through
  with VFO/MR on the radio (the firmware's scan lists). A memory can be in
  several. The per-memory "Groups" field edits the same bits.
- **WFM:** broadcast FM memories (87.5–108 MHz, played on the FM chip). They
  replace the stock FM radio's 20 presets, which this build and driver don't have.
- **TX off:** a per-memory setting that blocks transmitting on that memory.
- **VFO slots:** each VFO keeps one saved state per band: 10 m, 6 m, FM broadcast,
  Air, 2 m, 1.25 m, 70 cm, plus General for everything else, with the band edges
  set by Region. They're left out of the memory list and live under Settings → VFO
  slots: each band has a "switched on" box and a frequency per VFO (offset, tones and
  mode stay as the radio saved them). A switched-off band can't be tuned into; TX
  Any switches them all on. General refuses a frequency that belongs to another band.
- **Side keys:** this build's SQUELCH UP / SQUELCH DOWN / SYSTEM INFO actions.
  Upstream would reset them to NONE.
- **Settings → Services:** Watch, Region (ham bands and TX limits, the marine plan,
  FRS/GMRS/MURS in US and CANADA, PMR446 in INTL), SubCod (the FRS/GMRS brand
  numbering; in INTL, PMR446 channels number codes 1–38 and Simple UK channels use
  Ofcom's C1–C42 and DCS codes, automatically), T Live and TX Any. These
  live in the edition's own EEPROM area (0x1D00–0x1DFF), which is also uploaded.
  This only appears once the radio has run this firmware.

ScnRev (the upstream "Scan resume" setting) is the hold time for both scan and
watch hits in this edition.
