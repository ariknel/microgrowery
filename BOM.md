# Bill of Materials

Part of the [GrowBox](README.md) build. Component pricing is rough LCSC/JLCPCB-tier
estimates, not real quotes — swap in your own once you've ordered. PCB
fabrication and 3D-printed parts were missing from earlier versions of this
list; they're now included since they're a real chunk of the total cost.

---

### Hub Board (×1)

| Component | Part | LCSC | Qty | Unit | Total |
|-----------|------|------|-----|------|-------|
| ESP32-S3 SoC | ESP32-S3 QFN-56 | — | 1 | ~€2.50 | €2.50 |
| NOR Flash | GD25WQ64ESIGR | — | 1 | ~€0.80 | €0.80 |
| PD controller | FUSB302UCX | C481901 | 1 | €0.46 | €0.46 |
| 3.3V regulator | AP63203 | — | 1 | €0.15 | €0.15 |
| Crystal 40MHz | ±10ppm | — | 1 | €0.20 | €0.20 |
| USB-C receptacle | 16-pin SMD | C165948 | 1 | €0.18 | €0.18 |
| Passives + connectors | — | — | — | — | ~€2.00 |
| **Subtotal** | | | | | **~€6.30** |

### LED Driver Boards (×4, 4 channels each)

| Component | Part | LCSC | Qty/board | Unit | Per board | ×4 |
|-----------|------|------|----------|------|----------|-----|
| TX6120 driver | TX6120 ESOP-8 | C329272 | 4 | €0.26 | €1.04 | €4.16 |
| Inductor 330µH | Isat ≥500mA shielded | — | 4 | €0.25 | €1.00 | €4.00 |
| Schottky SS14 | 1A 40V SOD-123 | C2480 | 4 | €0.03 | €0.12 | €0.48 |
| R_sense 0.75Ω | 1% 0603 | — | 4 | €0.02 | €0.08 | €0.32 |
| R_VDD 7.2kΩ | 1% 0805 | — | 4 | €0.01 | €0.04 | €0.16 |
| C_in 10µF 25V | X7R 0805 | — | 4 | €0.03 | €0.12 | €0.48 |
| Passives + connectors | — | — | — | — | €0.30 | €1.20 |
| **Subtotal** | | | | | **€2.70/board** | **€10.80** |

### LED Panel Boards (×4, 20 LEDs each)

| Component | Part | LCSC | Qty/board | Unit | Per board | ×4 |
|-----------|------|------|----------|------|----------|-----|
| Warm white LED | XL-3030WWC-1W-3V | C2843893 | 20 | €0.034 | €0.68 | €2.72 |
| Connectors + passives | — | — | — | — | €0.20 | €0.80 |
| **Subtotal** | | | | | **€0.88/board** | **€3.52** |

### Sensor Board (×1)

| Component | Part | LCSC | Qty | Unit | Total |
|-----------|------|------|------|-----|------|-------|
| AHT20 | LCC-8 | C2757724 | 1 | €0.58 | €0.58 |
| C1 100nF | 0402 | — | 1 | €0.01 | €0.01 |
| C2 10µF | 0805 | — | 1 | €0.03 | €0.03 |
| R1, R2 4.7kΩ pull-ups | 0402 | — | 2 | €0.01 | €0.02 |
| Connector | JST XH 4-pin | — | 1 | €0.05 | €0.05 |
| **Subtotal** | | | | | **~€0.70** |

### PCB Fabrication (JLCPCB, 5-board minimum batch per design)

| Board | Spec | Est. cost |
|-------|------|-----------|
| Hub board | 2-layer FR4, ~50×70mm | ~€8 |
| LED driver board | 2-layer FR4 | ~€8 |
| LED panel board | Single-layer, aluminum-bottom, 85×55mm | ~€12 |
| Sensor board | 2-layer FR4, 25×20mm | ~€5 |
| **Subtotal** (4 batches — extras usable as spares) | | **~€33** |

### 3D Printed Parts

| Part | Material | Est. cost |
|------|---------|-----------|
| Camera mount bracket | PLA/PETG, ~15g | ~€1 |
| Carbon filter housing | PLA/PETG, ~40g | ~€2 |
| **Subtotal** | | **~€3** |

### Other

| Item | Cost |
|------|------|
| ESP32-CAM module | ~€3.00 |
| 100W GaN USB-C charger | ~€20.00 |
| 120mm PC fans ×2 | ~€6.00 |
| JST cables and connectors | ~€3.00 |
| Aquarium activated carbon | ~€4.00 |
| MDF + hardware for cabinet | ~€25.00 |
| Mylar reflective film | ~€5.00 |
| EPDM door seal + latches | ~€5.00 |
| **Subtotal** | **~€71.00** |

---

**Total estimated build cost: ~€125–135**

(Roughly €30 higher than earlier estimates — those omitted PCB fabrication entirely, which isn't optional.)
