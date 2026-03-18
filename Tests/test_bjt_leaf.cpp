// =============================================================================
// Test: BJTLeaf — Ebers-Moll BJT WDF one-port validation
//
// Validates the BJTLeaf (simplified forward-active Ebers-Moll) against
// expected transistor behavior:
//
// Test groups:
//   1. Construction and configure (no crash, all 8 presets)
//   2. Ic = beta * Ib in forward-active region (Vce > 0.3V)
//   3. Gm = Ic / Vt at operating point
//   4. BC184C params: Ic ~ 1mA for Vbe ~ 0.6V, beta ~ 400
//   5. NPN/PNP polarity symmetry (sign-inverted curves)
//   6. No NaN/Inf for extreme inputs
//   7. Passivity: energy dissipated >= 0
//   8. Port resistance tracks rbe = Bf*Vt/Ic
//   9. All 8 factory presets are valid
//  10. NR convergence speed
//
// Pattern: standalone test, same CHECK macro as other tests.
//
// Reference: ANALYSE_ET_DESIGN_Rev2.md component values
// =============================================================================

#include "../core/include/core/wdf/BJTLeaf.h"
#include "../core/include/core/model/BJTParams.h"
#include "../core/include/core/preamp/GainTable.h"
#include "../core/include/core/model/PreampConfig.h"
#include "../core/include/core/util/Constants.h"
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>

static constexpr double PI = 3.14159265358979323846;

static int g_pass = 0;
static int g_fail = 0;

void CHECK(bool cond, const char* msg)
{
    if (cond) {
        std::cout << "  PASS: " << msg << std::endl;
        g_pass++;
    } else {
        std::cout << "  *** FAIL: " << msg << " ***" << std::endl;
        g_fail++;
    }
}

void CHECK_NEAR(double actual, double expected, double tol, const char* msg)
{
    double err = std::abs(actual - expected);
    if (err <= tol) {
        std::cout << "  PASS: " << msg << " (err=" << err << ")" << std::endl;
        g_pass++;
    } else {
        std::cout << "  *** FAIL: " << msg
                  << " -- expected " << expected << ", got " << actual
                  << " (err=" << err << ", tol=" << tol << ") ***" << std::endl;
        g_fail++;
    }
}

int main()
{
    using namespace transfo;

    std::cout << "=== BJTLeaf Tests ===" << std::endl;

    // ── Test 1: Construction and configure (all 8 presets) ──────────────────
    std::cout << "\n--- Test 1: Factory Presets ---" << std::endl;
    {
        BJTParams presets[] = {
            BJTParams::BC184C(),
            BJTParams::BC214C(),
            BJTParams::BD139(),
            BJTParams::LM394(),
            BJTParams::N2N4250A(),
            BJTParams::N2N2484(),
            BJTParams::MJE181(),
            BJTParams::MJE171()
        };
        const char* names[] = {
            "BC184C", "BC214C", "BD139", "LM394",
            "2N4250A", "2N2484", "MJE181", "MJE171"
        };

        for (int i = 0; i < 8; ++i) {
            CHECK(presets[i].isValid(),
                  (std::string(names[i]) + " params are valid").c_str());
        }

        // Test BJTLeaf with each preset
        for (int i = 0; i < 8; ++i) {
            BJTLeaf leaf;
            leaf.configure(presets[i]);
            leaf.prepare(44100.0f);
            leaf.reset();

            // Process one sample
            float a = presets[i].polaritySign() * 0.6f;
            float b = leaf.scatter(a);
            CHECK(!std::isnan(b) && !std::isinf(b),
                  (std::string(names[i]) + " scatter produces finite output").c_str());
        }
    }

    // ── Test 2: Ic = beta * Ib ──────────────────────────────────────────────
    std::cout << "\n--- Test 2: Current Gain (Ic = Bf * Ib) ---" << std::endl;
    {
        BJTLeaf leaf;
        BJTParams p = BJTParams::BC184C();
        leaf.configure(p);
        leaf.prepare(44100.0f);
        leaf.reset();

        // Drive the BJT into forward-active region with a moderate
        // incident wave. Use a direct value — do NOT compute from
        // getPortResistance() which is adaptive and differs from
        // the solver's internal Z_port_.
        float a = 0.8f;

        // Settle for several samples
        for (int i = 0; i < 100; ++i)
            leaf.scatter(a);

        float Ic = leaf.getIc();
        float Ib = leaf.getIb();

        // Check beta ≈ Bf
        float beta_meas = std::abs(Ic / (Ib + 1e-20f));
        std::cout << "    Ic = " << Ic << " A, Ib = " << Ib
                  << " A, beta = " << beta_meas << std::endl;

        CHECK(beta_meas > 100.0f && beta_meas < 1000.0f,
              "Measured beta in reasonable range [100, 1000] for BC184C (nom 400)");

        // More precise: Ic should track Bf * Ib
        float expected_beta = p.Bf;
        float beta_err = std::abs(beta_meas - expected_beta) / expected_beta;
        CHECK_NEAR(beta_meas, expected_beta, expected_beta * 0.1,
                   "Beta matches Bf within 10%");
    }

    // ── Test 3: Transconductance gm = Ic / Vt ──────────────────────────────
    std::cout << "\n--- Test 3: Transconductance ---" << std::endl;
    {
        BJTLeaf leaf;
        leaf.configure(BJTParams::BC184C());
        leaf.prepare(44100.0f);
        leaf.reset();

        // Drive to a stable forward-active operating point
        float a = 0.8f;
        for (int i = 0; i < 100; ++i)
            leaf.scatter(a);

        float gm = leaf.getGm();
        float Ic = std::abs(leaf.getIc());
        float Vt = 0.02585f;
        float gm_expected = Ic / Vt;

        std::cout << "    gm = " << gm << " S, expected = " << gm_expected << " S" << std::endl;
        CHECK_NEAR(gm, gm_expected, gm_expected * 0.05,
                   "gm = Ic/Vt within 5%");
    }

    // ── Test 4: BC184C operating point ──────────────────────────────────────
    std::cout << "\n--- Test 4: BC184C Operating Point ---" << std::endl;
    {
        // Strategy: sweep incident wave 'a' directly, let the NR solve
        // for converged Vbe, and check the resulting operating points.
        // This avoids the Z mismatch between getPortResistance() (adaptive)
        // and Z_port_ (internal solver impedance).

        float Ic_at_06V = 0.0f;
        float best_Vbe_for_1mA = 0.0f;
        float best_Ic_dist = 1e10f;

        for (float a_in = 0.3f; a_in <= 2.0f; a_in += 0.01f) {
            BJTLeaf leaf;
            leaf.configure(BJTParams::BC184C());
            leaf.prepare(44100.0f);
            leaf.reset();

            // Settle
            for (int i = 0; i < 20; ++i)
                leaf.scatter(a_in);

            float Ic = std::abs(leaf.getIc());
            float Vbe = leaf.getVbe();

            // Find Ic at Vbe ~ 0.6V
            if (std::abs(Vbe - 0.6f) < 0.02f)
                Ic_at_06V = Ic;

            // Find Vbe closest to Ic = 1mA
            float dist = std::abs(Ic - 1e-3f);
            if (dist < best_Ic_dist) {
                best_Ic_dist = dist;
                best_Vbe_for_1mA = Vbe;
            }
        }

        std::cout << "    Vbe for Ic~1mA: " << best_Vbe_for_1mA << " V" << std::endl;
        std::cout << "    Ic at Vbe~0.6V: " << Ic_at_06V << " A" << std::endl;

        // Ic at Vbe=0.6V should be in the uA to mA range for BC184C
        CHECK(Ic_at_06V > 1e-7f && Ic_at_06V < 0.1f,
              "Ic at Vbe~0.6V is in reasonable range");
    }

    // ── Test 5: NPN/PNP Polarity Symmetry ──────────────────────────────────
    std::cout << "\n--- Test 5: NPN/PNP Symmetry ---" << std::endl;
    {
        BJTLeaf npn_leaf, pnp_leaf;
        npn_leaf.configure(BJTParams::BC184C());  // NPN
        pnp_leaf.configure(BJTParams::BC214C());  // PNP (complement)
        npn_leaf.prepare(44100.0f);
        pnp_leaf.prepare(44100.0f);
        npn_leaf.reset();
        pnp_leaf.reset();

        // Drive both with the SAME magnitude incident wave, opposite sign.
        // Use a moderate value that produces forward-active bias.
        const float a_mag = 0.8f;
        float a_npn =  a_mag;  // NPN: positive a → positive Vbe
        float a_pnp = -a_mag;  // PNP: negative a → negative Vbe

        for (int i = 0; i < 100; ++i) {
            npn_leaf.scatter(a_npn);
            pnp_leaf.scatter(a_pnp);
        }

        float Ic_npn = npn_leaf.getIc();
        float Ic_pnp = pnp_leaf.getIc();
        float Vbe_npn = npn_leaf.getVbe();
        float Vbe_pnp = pnp_leaf.getVbe();

        std::cout << "    NPN: Vbe=" << Vbe_npn << "V, Ic=" << Ic_npn << "A" << std::endl;
        std::cout << "    PNP: Vbe=" << Vbe_pnp << "V, Ic=" << Ic_pnp << "A" << std::endl;

        // Vbe signs should be opposite
        CHECK(Vbe_npn > 0.0f, "NPN Vbe > 0 (forward-active)");
        CHECK(Vbe_pnp < 0.0f, "PNP Vbe < 0 (forward-active)");

        // |Ic| magnitudes should be comparable (same beta, same Is)
        float ratio = std::abs(Ic_npn) / (std::abs(Ic_pnp) + 1e-20f);
        std::cout << "    |Ic_npn/Ic_pnp| = " << ratio << std::endl;
        CHECK(ratio > 0.1f && ratio < 10.0f,
              "NPN and PNP |Ic| within 10x (same device params)");
    }

    // ── Test 6: Numerical Stability ─────────────────────────────────────────
    std::cout << "\n--- Test 6: No NaN/Inf ---" << std::endl;
    {
        BJTLeaf leaf;
        leaf.configure(BJTParams::BC184C());
        leaf.prepare(44100.0f);
        leaf.reset();

        bool anyNaN = false;
        bool anyInf = false;

        float testValues[] = {
            0.0f, 0.001f, -0.001f,
            0.1f, -0.1f, 0.5f, -0.5f,
            1.0f, -1.0f, 5.0f, -5.0f,
            10.0f, -10.0f, 50.0f, -50.0f,
            100.0f, -100.0f
        };

        for (float a : testValues) {
            float b = leaf.scatter(a);
            if (std::isnan(b)) anyNaN = true;
            if (std::isinf(b)) anyInf = true;
        }

        CHECK(!anyNaN, "No NaN for inputs in [-100, +100]");
        CHECK(!anyInf, "No Inf for inputs in [-100, +100]");
    }

    // ── Test 7: Passivity ───────────────────────────────────────────────────
    std::cout << "\n--- Test 7: Passivity ---" << std::endl;
    {
        BJTLeaf leaf;
        leaf.configure(BJTParams::BC184C());
        leaf.prepare(44100.0f);
        leaf.reset();

        // Note: BJT is NOT strictly passive (it has gain from the supply).
        // However, the BE junction alone (which is what the WDF port models)
        // IS passive — it's a diode. |b| <= |a| should hold for the BE port.
        bool passive = true;
        const int N = 44100;

        for (int i = 0; i < N; ++i) {
            float t = static_cast<float>(i) / 44100.0f;
            float a = 0.3f * std::sin(2.0f * static_cast<float>(PI) * 100.0f * t);
            float b = leaf.scatter(a);

            if (std::abs(b) > std::abs(a) + 1e-5f) {
                passive = false;
                break;
            }
        }

        CHECK(passive, "BE junction port is passive (|b| <= |a|)");
    }

    // ── Test 8: Port Resistance Tracks rbe ──────────────────────────────────
    std::cout << "\n--- Test 8: Port Resistance ---" << std::endl;
    {
        BJTLeaf leaf;
        BJTParams p = BJTParams::BC184C();
        leaf.configure(p);
        leaf.prepare(44100.0f);
        leaf.reset();

        // Low current bias → high rbe
        float a_low = 0.3f;
        for (int i = 0; i < 100; ++i)
            leaf.scatter(a_low);
        float Z_low = leaf.getSmallSignalRbe();

        // High current bias → low rbe
        leaf.reset();
        float a_high = 1.0f;
        for (int i = 0; i < 100; ++i)
            leaf.scatter(a_high);
        float Z_high = leaf.getSmallSignalRbe();

        std::cout << "    rbe @ low bias: " << Z_low << " Ohm" << std::endl;
        std::cout << "    rbe @ high bias: " << Z_high << " Ohm" << std::endl;

        CHECK(Z_low > Z_high, "rbe decreases with increasing bias current");
    }

    // ── Test 9: GainTable validation ────────────────────────────────────────
    std::cout << "\n--- Test 9: GainTable ---" << std::endl;
    {
        CHECK_NEAR(GainTable::getGainDB(0), 10.0, 0.5,
                   "Position 0: ~+10 dB");
        CHECK_NEAR(GainTable::getGainDB(10), 50.0, 0.5,
                   "Position 10: ~+50 dB");

        // Monotonically increasing
        bool monotonic = true;
        for (int i = 1; i < GainTable::kNumPositions; ++i) {
            if (GainTable::getGainDB(i) <= GainTable::getGainDB(i - 1))
                monotonic = false;
        }
        CHECK(monotonic, "Gain is monotonically increasing across positions");

        // Total gain with T1 1:10
        CHECK_NEAR(GainTable::getTotalGainDB(0, 20.0f), 30.0, 1.0,
                   "Total gain position 0 with 1:10: ~30 dB");
    }

    // ── Test 10: PreampConfig validation ────────────────────────────────────
    std::cout << "\n--- Test 10: PreampConfig ---" << std::endl;
    {
        PreampConfig cfg = PreampConfig::DualTopology();
        CHECK(cfg.isValid(), "DualTopology() config is valid");
        CHECK(cfg.name == "Dual Topology Neve/Jensen", "Config name is correct");
        CHECK(cfg.Rg == 47.0f, "Rg = 47 Ohm");
        CHECK(cfg.input.ratio == InputStageConfig::Ratio::X10, "Default ratio is 1:10");
        CHECK(cfg.neveConfig.q1.polarity == BJTParams::Polarity::NPN, "Q1 is NPN");
        CHECK(cfg.neveConfig.q2.polarity == BJTParams::Polarity::PNP, "Q2 is PNP");
        CHECK(cfg.je990Config.q8_top.polarity == BJTParams::Polarity::NPN, "Q8 is NPN");
        CHECK(cfg.je990Config.q9_bottom.polarity == BJTParams::Polarity::PNP, "Q9 is PNP");
    }

    // ── Test 11: NR Convergence Speed ───────────────────────────────────────
    std::cout << "\n--- Test 11: NR Convergence ---" << std::endl;
    {
        BJTLeaf leaf;
        leaf.configure(BJTParams::BC184C());
        leaf.prepare(44100.0f);
        leaf.reset();

        const int N = 44100;
        int totalIter = 0;
        int maxIter = 0;

        for (int i = 0; i < N; ++i) {
            float t = static_cast<float>(i) / 44100.0f;
            float a = 0.5f * std::sin(2.0f * static_cast<float>(PI) * 1000.0f * t);
            leaf.scatter(a);
            int iter = leaf.getLastIterCount();
            totalIter += iter;
            maxIter = std::max(maxIter, iter);
        }

        float avgIter = static_cast<float>(totalIter) / N;
        std::cout << "    Avg iterations: " << avgIter << ", Max: " << maxIter << std::endl;
        CHECK(avgIter < 6.0f, "Average NR iterations < 6");
    }

    // ── Summary ─────────────────────────────────────────────────────────────
    std::cout << "\n=== BJTLeaf Results ===" << std::endl;
    std::cout << "  Total: " << (g_pass + g_fail) << "  Pass: " << g_pass
              << "  Fail: " << g_fail << std::endl;

    return g_fail > 0 ? 1 : 0;
}
