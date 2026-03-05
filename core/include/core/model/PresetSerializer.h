#pragma once

// =============================================================================
// PresetSerializer — Serialize TransformerConfig to JSON string.
//
// Pure C++17 stdlib implementation (no JUCE, no external dependencies).
// Produces human-readable JSON compatible with PresetLoader.
//
// Public API:
//   toJson(config)           -> std::string (formatted JSON)
//   saveToFile(config, path) -> bool
// =============================================================================

#include "TransformerConfig.h"
#include <string>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <cmath>

namespace transfo {

class PresetSerializer
{
public:
    // ── Serialize a TransformerConfig to a JSON string ────────────────────────
    static std::string toJson(const TransformerConfig& cfg, int indentSpaces = 4)
    {
        std::ostringstream os;
        std::string ind(static_cast<size_t>(indentSpaces), ' ');
        std::string ind2 = ind + ind;
        std::string ind3 = ind2 + ind;

        os << "{\n";
        os << ind << jsonString("name") << ": " << jsonString(cfg.name) << ",\n";

        // --- Material family ---
        os << ind << jsonString("material_family") << ": "
           << jsonString(materialFamilyToString(cfg.material)) << ",\n";
        os << ind << jsonString("gapped") << ": "
           << (cfg.core.isGapped() ? "true" : "false") << ",\n";

        // --- Core Geometry ---
        os << "\n" << ind << jsonString("core_geometry") << ": {\n";
        os << ind2 << jsonString("Gamma_center_m")  << ": " << jsonFloat(cfg.core.Gamma_center) << ",\n";
        os << ind2 << jsonString("Gamma_outer_m")   << ": " << jsonFloat(cfg.core.Gamma_outer) << ",\n";
        os << ind2 << jsonString("Gamma_yoke_m")    << ": " << jsonFloat(cfg.core.Gamma_yoke) << ",\n";
        os << ind2 << jsonString("Lambda_center_m2") << ": " << jsonFloat(cfg.core.Lambda_center) << ",\n";
        os << ind2 << jsonString("Lambda_outer_m2")  << ": " << jsonFloat(cfg.core.Lambda_outer) << ",\n";
        os << ind2 << jsonString("Lambda_yoke_m2")   << ": " << jsonFloat(cfg.core.Lambda_yoke) << ",\n";
        os << ind2 << jsonString("air_gap_length_m") << ": " << jsonFloat(cfg.core.airGapLength) << "\n";
        os << ind << "},\n";

        // --- Electrical (Windings) ---
        os << "\n" << ind << jsonString("electrical") << ": {\n";
        os << ind2 << jsonString("turns_N1")             << ": " << cfg.windings.turnsRatio_N1 << ",\n";
        os << ind2 << jsonString("turns_N2")             << ": " << cfg.windings.turnsRatio_N2 << ",\n";
        os << ind2 << jsonString("turns_ratio")          << ": "
           << jsonString(std::to_string(cfg.windings.turnsRatio_N1) + ":" +
                         std::to_string(cfg.windings.turnsRatio_N2)) << ",\n";
        os << ind2 << jsonString("Rdc_primary_ohm")      << ": " << jsonFloat(cfg.windings.Rdc_primary) << ",\n";
        os << ind2 << jsonString("Rdc_secondary_ohm")    << ": " << jsonFloat(cfg.windings.Rdc_secondary) << ",\n";
        os << ind2 << jsonString("C_sec_shield_F")       << ": " << jsonFloat(cfg.windings.C_sec_shield) << ",\n";
        os << ind2 << jsonString("C_interwinding_F")     << ": " << jsonFloat(cfg.windings.C_interwinding) << ",\n";
        os << ind2 << jsonString("Lp_estimated_H")       << ": " << jsonFloat(cfg.windings.Lp_primary) << ",\n";
        os << ind2 << jsonString("L_leakage_H")          << ": " << jsonFloat(cfg.windings.L_leakage) << ",\n";
        os << ind2 << jsonString("source_impedance_ohm") << ": " << jsonFloat(cfg.windings.sourceImpedance) << ",\n";
        os << ind2 << jsonString("load_impedance_ohm")   << ": " << jsonFloat(cfg.windings.loadImpedance) << "\n";
        os << ind << "},\n";

        // --- Jiles-Atherton Parameters ---
        os << "\n" << ind << jsonString("ja_parameters") << ": {\n";
        os << ind2 << jsonString("Ms")    << ": " << jsonFloat(cfg.material.Ms) << ",\n";
        os << ind2 << jsonString("a")     << ": " << jsonFloat(cfg.material.a) << ",\n";
        os << ind2 << jsonString("alpha") << ": " << jsonFloat(cfg.material.alpha) << ",\n";
        os << ind2 << jsonString("k")     << ": " << jsonFloat(cfg.material.k) << ",\n";
        os << ind2 << jsonString("c")     << ": " << jsonFloat(cfg.material.c) << ",\n";
        os << ind2 << jsonString("K1")    << ": " << jsonFloat(cfg.material.K1) << ",\n";
        os << ind2 << jsonString("K2")    << ": " << jsonFloat(cfg.material.K2) << "\n";
        os << ind << "},\n";

        // --- Top-level load impedance ---
        os << "\n" << ind << jsonString("load_impedance_ohm") << ": " << jsonFloat(cfg.loadImpedance) << "\n";

        os << "}\n";
        return os.str();
    }

    // ── Save to file ────────────────────────────────────────────────────────
    static bool saveToFile(const TransformerConfig& cfg, const std::string& filePath)
    {
        std::ofstream file(filePath);
        if (!file.is_open())
            return false;

        file << toJson(cfg);
        return file.good();
    }

private:
    // ── JSON formatting helpers ──────────────────────────────────────────────

    static std::string jsonString(const std::string& s)
    {
        std::ostringstream os;
        os << '"';
        for (char c : s)
        {
            switch (c)
            {
                case '"':  os << "\\\""; break;
                case '\\': os << "\\\\"; break;
                case '\b': os << "\\b";  break;
                case '\f': os << "\\f";  break;
                case '\n': os << "\\n";  break;
                case '\r': os << "\\r";  break;
                case '\t': os << "\\t";  break;
                default:   os << c;      break;
            }
        }
        os << '"';
        return os.str();
    }

    static std::string jsonFloat(float value)
    {
        if (value == 0.0f)
            return "0.0";

        // Use scientific notation for very small or very large values
        float absVal = std::fabs(value);
        if (absVal < 1e-3f || absVal >= 1e6f)
        {
            std::ostringstream os;
            os << std::scientific << std::setprecision(6) << static_cast<double>(value);
            return os.str();
        }

        // Fixed notation with enough precision
        std::ostringstream os;
        os << std::fixed;

        if (absVal < 0.01f)        os << std::setprecision(8);
        else if (absVal < 1.0f)    os << std::setprecision(6);
        else if (absVal < 100.0f)  os << std::setprecision(4);
        else if (absVal < 10000.0f) os << std::setprecision(2);
        else                       os << std::setprecision(1);

        os << static_cast<double>(value);

        // Remove trailing zeros but keep at least one decimal
        std::string result = os.str();
        size_t dot = result.find('.');
        if (dot != std::string::npos)
        {
            size_t lastNonZero = result.find_last_not_of('0');
            if (lastNonZero != std::string::npos && lastNonZero > dot)
                result = result.substr(0, lastNonZero + 1);
            else if (lastNonZero == dot)
                result = result.substr(0, dot + 2); // keep "X.0"
        }

        return result;
    }

    // ── Material family string from J-A parameters ──────────────────────────
    // Heuristic based on Ms range (matches JAParameterSet defaults)
    static std::string materialFamilyToString(const JAParameterSet& mat)
    {
        if (mat.Ms > 1.0e6f)
            return "go_sife";
        if (mat.Ms > 6.5e5f)
            return "nife_50";
        return "mu_metal";
    }
};

} // namespace transfo
