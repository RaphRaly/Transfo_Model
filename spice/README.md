# SPICE Models — Neve/Marinair Transformers

LTSpice/ngspice circuit models for the transformers used in the Neve 1073 console.

All component values sourced from:
- Neve Drawing EDO 71/13 (22/3/72 and 6/11/73)
- Marinair Type 1400-1500 Catalogue
- Codebase `WindingConfig.h` and `CoreGeometry.h` parameters

## Files

| File | Transformer | Format |
|------|------------|--------|
| `neve_10468_input.cir` | 10468 mic input (1:2, 300->1200 ohm, +6dB) | SPICE netlist |
| `neve_10468_input.asc` | Same | LTSpice schematic |
| `neve_li1166_output.cir` | LI1166 line output, gapped (5:3, 200->600 ohm, -4dB) | SPICE netlist |
| `neve_lo2567_hot.cir` | LO2567 ungapped output, "Neve Hot" (5:3, 200->600 ohm) | SPICE netlist |
| `neve_lo1173_output.cir` | LO1173 line output (3:1, 70->600 ohm, -8dB) | SPICE netlist |
| `neve_1073_full_channel.cir` | Full 1073 chain: 10468 -> preamp -> LI1166 | SPICE netlist |

## Equivalent Circuit (Steinmetz Model)

All models use the standard Steinmetz transformer equivalent circuit:

```
         Rs        Rdc_pri     L_leakage           N1:N2          Rdc_sec
Vsrc ---/\/\/------/\/\/-------LLLL------+---||---+---||---+------/\/\/--- Rload
                                         |        |        |
                                        Ciw      Css     Css2
                                         |        |        |
                                        GND      GND      GND

Rs          = Source impedance
Rdc_pri     = Primary DC winding resistance
L_leakage   = Leakage inductance (primary-referred)
Lp          = Primary magnetizing inductance
Ciw         = Interwinding capacitance
Css         = Secondary-to-shield capacitance
N1:N2       = Turns ratio (coupled inductors, K=0.9999)
Rdc_sec     = Secondary DC winding resistance
Rload       = Load impedance
```

The ideal transformer is modeled with coupled inductors:
```
Lp    node_pri  0   {Lp_value}
Ls    node_sec1 node_sec2  {Lp_value * (N2/N1)^2}
K1    Lp  Ls  0.9999
```

## Component Values Summary

| Parameter | 10468 Input | LI1166 Output | LO2567 Hot | LO1173 Output |
|-----------|------------|---------------|------------|---------------|
| N1:N2 | 1:2 | 5:3 | 5:3 | 3:1 |
| Rdc_pri | 8 ohm | 12 ohm | 12 ohm | 5 ohm |
| Rdc_sec | 32 ohm | 18 ohm | 18 ohm | 15 ohm |
| Lp | 5 H | 8 H | 80 H | 3 H |
| L_leakage | 0.5 mH | 1 mH | 1 mH | 0.3 mH |
| Ciw | 8 pF | 6 pF | 6 pF | 5 pF |
| Css | 120 pF | 100 pF | 100 pF | 80 pF |
| Z_source | 300 ohm | 200 ohm | 200 ohm | 70 ohm |
| Z_load | 1200 ohm | 600 ohm | 600 ohm | 600 ohm |
| Air gap | none | 0.1 mm | **none** | none |
| Gain | +6 dB | -4 dB | -4 dB | -8 dB |

## Gapped vs Ungapped (LI1166 vs LO2567)

The LI1166 has a 0.1mm air gap that:
- Reduces effective permeability by ~10x (mu_eff ~ 5,000 vs ~50,000)
- Lowers magnetizing inductance (8H vs ~80H ungapped)
- Linearizes the B-H curve (less harmonic distortion)
- Increases headroom before saturation
- Allows DC tolerance (no core saturation from DC offset)

The LO2567 (ungapped) has the same windings but:
- Higher Lp (no gap reluctance)
- Earlier saturation, more 2nd/3rd harmonic coloration
- Better low-frequency extension (higher Lp = lower fc)
- No DC tolerance

## Running

**LTSpice:** Open `.asc` file directly, or import `.cir` via File > Open.

**ngspice:**
```sh
ngspice neve_10468_input.cir
> run
> plot V(out)
> meas tran ...
```

## Validation Against WDF Model

These SPICE models serve as reference for validating the WDF-based
`TransformerModel` in `core/`. To compare:

1. Run the SPICE simulation at 1kHz, measure THD and gain
2. Run the equivalent preset through `tools/simulate.cpp`
3. Compare: gain (should match within 1 dB), THD (order of magnitude)

The WDF model adds Jiles-Atherton hysteresis which the linear SPICE
model does not capture. For nonlinear SPICE simulation, use the
Chan/J-A core model (commented in `neve_lo2567_hot.cir`).
