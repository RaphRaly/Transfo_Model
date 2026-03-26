# FROZEN SPECIFICATION — Triple-Topology Preamp
# All values FINAL. No more "or" / "approximately" / "to be decided".
# This document is the SINGLE SOURCE OF TRUTH for KiCad schematic entry.
# Version 1.0 — 2026-03-24

---

## PSU ±24V — FINAL VALUES

| Ref | Value | Tolerance | Power | Function |
|-----|-------|-----------|-------|----------|
| TR_main | Toroidal 50VA, 2×18VAC | - | 50VA | Main PSU transformer |
| BR1 | KBU4J (4A 600V bridge) | - | - | Main rectifier |
| C_main1 | 4700µF / 50V electro | 20% | - | +rail filter (after bridge) |
| C_main2 | 4700µF / 50V electro | 20% | - | -rail filter (after bridge) |
| U_reg1 | LM317T (TO-220) | - | - | +24V regulator |
| U_reg2 | LM337T (TO-220) | - | - | -24V regulator |
| R_adj1 | 240R | 1% | 1/4W | LM317 reference |
| R_adj1b | 4.32K | 1% | 1/4W | LM317 set resistor |
| R_trim1 | 500R trimpot (Bourns 3296W) | - | - | Fine adjust +24V |
| R_adj2 | 240R | 1% | 1/4W | LM337 reference |
| R_adj2b | 4.32K | 1% | 1/4W | LM337 set resistor |
| R_trim2 | 500R trimpot (Bourns 3296W) | - | - | Fine adjust -24V |
| C_reg_in1 | 100nF / 50V ceramic | - | - | LM317 input bypass |
| C_reg_in2 | 100nF / 50V ceramic | - | - | LM337 input bypass |
| C_reg_out1 | 10µF / 50V electro | - | - | LM317 output stability |
| C_reg_out2 | 10µF / 50V electro | - | - | LM337 output stability |
| C_main3 | 4700µF / 50V electro | 20% | - | +24V post-reg filter |
| C_main4 | 4700µF / 50V electro | 20% | - | -24V post-reg filter |
| C_bypass_p | 100nF / 50V ceramic | - | - | +24V local bypass |
| C_bypass_n | 100nF / 50V ceramic | - | - | -24V local bypass |

**Calculated output:**
Vout+ = 1.25 × (1 + 4320/240) = 1.25 × 19.0 = **+23.75V** → trim to +24.0V
Vout- = same calculation → **-24.0V**

---

## PSU HT (+270V) — FINAL VALUES

| Ref | Value | Tolerance | Power | Function |
|-----|-------|-----------|-------|----------|
| TR_HT | Antek AN-0T220 (220VAC sec, 15VA) | - | 15VA | HV transformer |
| BR2 | W04G (1A 400V bridge) | - | - | HV rectifier |
| C_HT1 | 47µF / 400V electro | 20% | - | First filter cap |
| R_HT1 | 3.3K | 5% | 3W | RC filter stage 1 |
| C_HT2 | 100µF / 400V electro | 20% | - | Second filter cap |
| R_HT2 | 1.5K | 5% | 2W | RC filter stage 2 |
| C_HT3 | 100µF / 400V electro | 20% | - | Third filter cap (B+ output) |
| R_bleed1 | 470K | 5% | 2W | Bleeder on C_HT1 |
| R_bleed2 | 470K | 5% | 2W | Bleeder on C_HT2 |
| R_bleed3 | 470K | 5% | 2W | Bleeder on C_HT3 |

**Calculated B+:**
V_peak = 220 × √2 = 311V. After bridge: 311 - 2 = 309V.
Under load (I_total ≈ 4mA):
- Drop R_HT1: 3.3K × 4mA = 13.2V
- Drop R_HT2: 1.5K × 4mA = 6.0V
- Total drop: 19.2V
- **B+ = 309 - 19.2 ≈ 290V** (no-load will be ~309V)

With regulation sag under tubes drawing ~3-5mA and cap ESR:
**B+ = 270-290V** (acceptable range for ECC83, nominal 275V)

---

## HEATER SUPPLY — FINAL VALUES (SERIES MODE, 12.6V DC)

| Ref | Value | Function |
|-----|-------|----------|
| U_reg3 | LM317T (TO-220) | Heater regulator |
| R_adj3 | 240R (1%) | LM317 reference |
| R_adj3b | 2.21K (1%) | LM317 set resistor |
| R_trim3 | 500R trimpot | Fine adjust to 12.6V |
| C_heat_in | 100nF ceramic | Input bypass |
| C_heat_out | 10µF / 25V electro | Output stability |
| C_heat_filt | 4700µF / 25V electro | Ripple filter |

**Configuration: 2 tubes in SERIES at 12.6V, I = 150mA**
V1 heater pins 4-5 → V2 heater pins 4-5 → GND
P_diss LM317 = (24 - 12.6) × 0.15 = **1.71W** → TO-220 heatsink required

---

## INPUT SECTION — FINAL VALUES

| Ref | Value | Tolerance | Function |
|-----|-------|-----------|----------|
| R_ph1 | 6.81K | 0.1% | Phantom R, pin 2 |
| R_ph2 | 6.81K | 0.1% | Phantom R, pin 3 |
| R_pad1 | 649R | 1% | Pad, hot leg |
| R_pad2 | 649R | 1% | Pad, cold leg |
| R_term | 13.7K | 1% | T1 secondary termination |
| R_30K | 30.1K | 1% | T1 secondary bias |
| C_term | 680pF | 5% NP0 | T1 HF damping |
| T1 | JT-115K-E | - | Input transformer |
| SW_48V | SPST toggle | - | Phantom on/off |
| SW_PAD | DPST toggle | - | Pad -20dB on/off |

---

## CHEMIN A (NEVE) — FINAL VALUES

| Ref | Value | Tolerance | Power | Function |
|-----|-------|-----------|-------|----------|
| Q1A | BC184C (NPN TO-92) | - | - | Input CE stage |
| Q2A | BC214C (PNP TO-92) | - | - | 2nd CE stage |
| Q3A | BD139 (NPN TO-126) | - | - | Output EF |
| R6A | 100K | 1% | 1/4W | Q1A bias high |
| R7A | 100K | 1% | 1/4W | Q1A bias low |
| R8A | 10K | 1% | 1/4W | Q1A Rc |
| R9A | 47R | 0.1% | 1/4W | Q1A Re AC (gain ref) |
| R10A | 15K | 1% | 1/4W | Q1A Re DC bias |
| R11A | 7.5K | 1% | 1/4W | Q2A Re |
| R12A | 6.8K | 1% | 1/4W | Q2A Rc |
| R_biasA | 390R | 5% | 1W | Q3A EF bias to -24V |
| R_serA | 10R | 1% | 1/4W | Output series R |
| C3A | 100µF bipolar | 20% | 50V | Input coupling |
| C4A | 100µF electro | 20% | 25V | Emitter bypass |
| C5A | 100pF | 5% NP0 | - | Miller comp |
| C6A | 470µF electro | 20% | 25V | Feedback coupling |
| C_outA | 220µF bipolar | 20% | 50V | Output coupling |
| C_outA_f | 4.7µF polyprop | 10% | 250V | Output film parallel |

---

## CHEMIN B (JE-990) — FINAL VALUES

| Ref | Value | Tolerance | Power | Function |
|-----|-------|-----------|-------|----------|
| Q1B/Q2B | AS394H (matched NPN pair) | - | - | Diff input |
| Q3B | 2N4250A (PNP TO-92) | - | - | Active load Q1 |
| Q4B | 2N3904 (NPN TO-92) | - | - | Tail current source |
| Q5B | 2N4250A (PNP TO-92) | - | - | Active load Q2 |
| Q6B | 2N4250A (PNP TO-92) | - | - | VAS |
| Q7B | 2N3904 (NPN TO-92) | - | - | PNP output pre-driver |
| Q8B | MJE181 (NPN TO-126) | - | - | NPN output |
| Q9B | MJE171 (PNP TO-126) | - | - | PNP output |
| R1B | 30R | 1% | 1/4W | Q1 emitter degen |
| R2B | 30R | 1% | 1/4W | Q2 emitter degen |
| R3B | 160R | 1% | 1/4W | Tail current set |
| R4B | 300R | 1% | 1/4W | Q3 collector load |
| R5B | 300R | 1% | 1/4W | Q5 collector load |
| R6B | 2K | 1% | 1/4W | PNP cascode bias |
| R7B | 160R | 1% | 1/4W | VAS bias |
| R8B | 130R | 1% | 1/4W | VAS stabilization |
| R10B | 62K | 1% | 1/4W | Output bias |
| R11B | 62K | 1% | 1/4W | Output bias |
| R13B | 3.9R | 1% | 1/2W | Output current sense |
| R14B | 39R | 1% | 1/4W | NPN output degen |
| R15B | 39R | 1% | 1/4W | PNP output degen |
| Rg_B | 47R | 0.1% | 1/4W | Gain ground ref |
| R_loadB | 39R (carbon comp) | 5% | 1W | Load isolator R |
| L1B | 22µH axial (Bourns 78F220J) | - | - | Q1 emitter inductor |
| L2B | 22µH axial (Bourns 78F220J) | - | - | Q2 emitter inductor |
| L3B | 40T #30 on R_loadB | - | - | Load isolator L |
| C1B | 150pF | 5% NP0 | - | Miller comp main |
| C2B | 62pF | 5% NP0 | - | HF comp output+ |
| C3B_comp | 91pF | 5% NP0 | - | HF comp output- |
| C4B | 100nF ceramic | - | 50V | +24V bypass local |
| C5B | 100nF ceramic | - | 50V | -24V bypass local |
| C3B | 100µF bipolar | 20% | 50V | Input coupling |
| C_outB | 220µF bipolar | 20% | 50V | Output coupling |
| C_outB_f | 4.7µF polyprop | 10% | 250V | Output film parallel |
| CR1-CR2 | 1N4148 | - | - | Input protection |
| CR3 | 1N4148 | - | - | PNP cascode bias |
| CR4-CR12 | 1N4148 | - | - | Output protection/bias |

---

## CHEMIN C (TUBE) — FINAL VALUES

| Ref | Value | Tolerance | Power | Function |
|-----|-------|-----------|-------|----------|
| V1 | ECC83 / 12AX7 (JJ ECC83S) | - | - | Gain tube (V1a + V1b) |
| V2 | ECC83 / 12AX7 (JJ ECC83S) | - | - | Buffer tube (V2a WCF bot + V2b WCF top) |
| Rp1 | 220K | 1% | 1/2W | V1a plate load |
| Rp2 | 220K | 1% | 1/2W | V1b plate load |
| Rp_WCF | 100K | 1% | 1/2W | V2b plate load (WCF top) |
| Rk1 | 1.5K | 1% | 1/4W | V1a cathode |
| Rk2 | 1.5K | 1% | 1/4W | V1b cathode |
| Rk_WCF | 1.5K | 1% | 1/4W | V2a cathode (WCF bottom) |
| Rg1 | 1M | 1% | 1/4W | V1a grid leak |
| Rg2 | 1M | 1% | 1/4W | V1b grid leak |
| Rg3 | 1M | 1% | 1/4W | V2a grid leak |
| R_outC | 10R | 1% | 1/4W | Output series R |
| Ck1 | 22µF | 20% | 63V | V1a cathode bypass |
| Ck2 | 22µF | 20% | 63V | V1b cathode bypass |
| C3C | 1µF polyprop | 10% | 400V | Input coupling |
| Cc1 | 470nF polyprop | 10% | 400V | V1a→atten coupling |
| Cc2 | 470nF polyprop | 10% | 400V | atten→V1b coupling |
| Cc3 | 330nF polyprop | 10% | 400V | V1b→V2a coupling |
| Cg_WCF | 1µF polyprop | 10% | 400V | V2b gate (WCF feedback) |
| C_outC | 100µF bipolar | 20% | 50V | Output coupling |
| C_outC_f | 4.7µF polyprop | 10% | 250V | Output film parallel |
| C_HT_dec1-4 | 100nF film | - | 400V | Local B+ bypass ×4 |

---

## GAIN SWITCH — FINAL VALUES (all 3 poles, 11 positions)

Switch: **Grayhill 71BD30-03-1-AJN** (3-deck, 1 pole/deck, 12 pos, adj stop set to 11)

### Pole A (Neve feedback) and Pole B (JE-990 feedback)
SAME values on both poles. Gain = 1 + Rfb / 47R

| Pos | Gain (dB) | Rfb value (E96) |
|-----|-----------|-----------------|
| 1 | +10 | 102R |
| 2 | +14 | 188R |
| 3 | +18 | 324R |
| 4 | +22 | 536R |
| 5 | +26 | 887R |
| 6 | +30 | 1.43K |
| 7 | +34 | 2.32K |
| 8 | +38 | 3.65K |
| 9 | +42 | 5.90K |
| 10 | +46 | 9.31K |
| 11 | +50 | 14.7K |

### Pole C (Tube interstage attenuator)
R_shunt to GND. Gain calibrated to match poles A/B.
VALUES TO BE CALIBRATED ON PROTOTYPE — initial estimates:

| Pos | Gain target | R_shunt (estimate) |
|-----|-------------|-------------------|
| 1 | +10 | 2.2K |
| 2 | +14 | 3.9K |
| 3 | +18 | 6.8K |
| 4 | +22 | 12K |
| 5 | +26 | 22K |
| 6 | +30 | 39K |
| 7 | +34 | 68K |
| 8 | +38 | 120K |
| 9 | +42 | 220K |
| 10 | +46 | 470K |
| 11 | +50 | 1M (near bypass) |

---

## RELAY SWITCHING — FINAL SCHEMATIC (one and only)

```
TOPOLOGY: 2× Omron G6S-2-DC24 (DPDT, 24V coil)

K1: Selects A vs (B or C)
K2: Selects B vs C (only relevant when K1 = ON)

SIGNAL PATH:
  Output_A ──────► K1:NO
  K1:COM ─────────────────────────────► T2 primary HOT
  K1:NC ──────────► K2:COM
  Output_B ──────► K2:NC
  Output_C ──────► K2:NO

CONTROL LOGIC (active high):
  SW_ABC = 3-position rotary switch (Alpha SR1712F-0203)

  Position 1 (A/Neve):   K1_ctrl = LOW,  K2_ctrl = LOW
  Position 2 (B/Jensen): K1_ctrl = HIGH, K2_ctrl = LOW
  Position 3 (C/Tube):   K1_ctrl = HIGH, K2_ctrl = HIGH

  SW_ABC common ── +24V
  SW_ABC pos2 ──── R_relay1 [1K] ── Q_relay1 base (BC184C)
  SW_ABC pos3 ──── R_relay1 [1K] ── Q_relay1 base (same wire, OR'd)
  SW_ABC pos3 ──── R_relay2 [1K] ── Q_relay2 base (BC184C)

  Q_relay1: E=GND, C=K1 coil(-), K1 coil(+)=+24V, D_fly1 (1N4148) across coil
  Q_relay2: E=GND, C=K2 coil(-), K2 coil(+)=+24V, D_fly2 (1N4148) across coil

RESULT:
  SW pos 1 → K1=OFF(NC), K2=OFF(NC) → K1:NC=open → K1:NO selected → Output_A → T2 ✓
  Wait — K1:NO is selected when K1 is OFF? No.

  CORRECTION — relay logic:
  K1 OFF (de-energized) → COM connects to NC
  K1 ON (energized) → COM connects to NO

  REVISED WIRING:
  Output_A ──────► K1:NC    (selected when K1 OFF = position A)
  K2:COM ─────────► K1:NO   (selected when K1 ON = position B or C)
  Output_B ──────► K2:NC    (selected when K2 OFF = position B)
  Output_C ──────► K2:NO    (selected when K2 ON = position C)
  K1:COM ─────────────────────────────► T2 primary HOT

  Position 1 (A): K1=OFF → K1:COM=K1:NC=Output_A → T2 ✓
  Position 2 (B): K1=ON → K1:COM=K1:NO=K2:COM; K2=OFF → K2:COM=K2:NC=Output_B → T2 ✓
  Position 3 (C): K1=ON → K1:COM=K1:NO=K2:COM; K2=ON → K2:COM=K2:NO=Output_C → T2 ✓

CONFIRMED FINAL.
```

---

## LED INDICATORS

| LED | Color | Condition |
|-----|-------|-----------|
| LED_A | Green | SW_ABC pos 1 (K1=OFF) → via R_LED 1K from +24V through inverted K1 sense |
| LED_B | Blue | SW_ABC pos 2 (K1=ON, K2=OFF) |
| LED_C | Amber | SW_ABC pos 3 (K1=ON, K2=ON) |
| LED_48V | Red | Phantom active |

Simple implementation: each LED driven by a dedicated contact on SW_ABC rotary.

---

## GROUNDING STRATEGY — EXPLICIT NETS

| Net Name | Function | Connection Point |
|----------|----------|-----------------|
| GND_STAR | Central star point | Near T1 secondary cold |
| AGND_INPUT | Input section return | → GND_STAR via trace |
| AGND_NEVE | Chemin A return | → GND_STAR via trace |
| AGND_990 | Chemin B return | → GND_STAR via trace |
| AGND_TUBE | Chemin C return | → GND_STAR via trace |
| PGND | Power supply return | → GND_STAR via trace |
| CGND | Chassis ground | → GND_STAR via 10R // 100nF/250V |
| GND (plane) | L2 copper pour | Connected to GND_STAR, continuous |

KiCad net class "GND_STAR_BRANCHES": 0.5mm trace width, explicit routing only.
Decoupling caps (100nF) connect to GND plane via direct vias.
Power return currents use dedicated AGND_xxx traces back to star point.

---

## KNOWN DESIGN RISK — T1 HF LOADING

All 3 paths load T1 secondary simultaneously via their coupling caps.
At HF (>10kHz), coupling caps become transparent.
Combined load: ~10.4K (vs ~13K single path) — within JT-115K-E spec.

**Proto test #1:** Oscilloscope on JE-990 output, all 3 paths connected,
sweep gain positions. Check for HF oscillation or anomalous behavior.

**Mitigation if needed:** Add DPDT relay on input to disconnect inactive paths,
OR add ferrite beads (600R@100MHz) in series with inactive path inputs.
