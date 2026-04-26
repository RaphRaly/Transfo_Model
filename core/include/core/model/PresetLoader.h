#pragma once

// =============================================================================
// PresetLoader — Load TransformerConfig presets from JSON files.
//
// Pure C++17 stdlib implementation (no JUCE, no external dependencies).
// Includes a minimal JSON parser for the flat/nested-object format used
// by the transformer data files in data/transformers/.
//
// Public API:
//   loadFromFile(path)        -> Expected<TransformerConfig>
//   loadFromString(json)      -> Expected<TransformerConfig>
//   scanDirectory(dir)        -> vector<PresetInfo>
// =============================================================================

#include "TransformerConfig.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <variant>
#include <optional>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

namespace transfo {

// ─── Result type for load operations ────────────────────────────────────────

template <typename T>
struct Expected
{
    T           value;
    bool        ok      = false;
    std::string error;

    static Expected success(T v)            { return {std::move(v), true, {}}; }
    static Expected failure(std::string msg) { return {{}, false, std::move(msg)}; }
};

// ─── Minimal JSON value representation ──────────────────────────────────────

namespace json_detail {

// Forward declaration
struct JsonValue;

using JsonObject = std::unordered_map<std::string, JsonValue>;
using JsonArray  = std::vector<JsonValue>;

struct JsonValue
{
    enum Type { Null, Bool, Number, String, Object, Array };
    Type type = Null;

    double                      number_val  = 0.0;
    bool                        bool_val    = false;
    std::string                 string_val;
    JsonObject                  object_val;
    JsonArray                   array_val;

    // Convenience accessors
    bool isObject() const { return type == Object; }
    bool isArray()  const { return type == Array; }
    bool isString() const { return type == String; }
    bool isNumber() const { return type == Number; }
    bool isBool()   const { return type == Bool; }
    bool isNull()   const { return type == Null; }

    double      asNumber(double def = 0.0)      const { return isNumber() ? number_val : def; }
    float       asFloat(float def = 0.0f)       const { return isNumber() ? static_cast<float>(number_val) : def; }
    int         asInt(int def = 0)              const { return isNumber() ? static_cast<int>(number_val) : def; }
    bool        asBool(bool def = false)        const { return isBool() ? bool_val : def; }
    std::string asString(const std::string& def = "") const { return isString() ? string_val : def; }

    // Object key lookup
    const JsonValue* get(const std::string& key) const
    {
        if (!isObject()) return nullptr;
        auto it = object_val.find(key);
        return (it != object_val.end()) ? &it->second : nullptr;
    }

    // Nested lookup: "electrical.turns_N1"
    const JsonValue* getPath(const std::string& dotPath) const
    {
        const JsonValue* cur = this;
        std::istringstream ss(dotPath);
        std::string segment;
        while (std::getline(ss, segment, '.'))
        {
            if (!cur || !cur->isObject()) return nullptr;
            cur = cur->get(segment);
        }
        return cur;
    }

    float pathFloat(const std::string& dotPath, float def = 0.0f) const
    {
        const JsonValue* v = getPath(dotPath);
        return (v && v->isNumber()) ? v->asFloat() : def;
    }

    int pathInt(const std::string& dotPath, int def = 0) const
    {
        const JsonValue* v = getPath(dotPath);
        return (v && v->isNumber()) ? v->asInt() : def;
    }

    std::string pathString(const std::string& dotPath, const std::string& def = "") const
    {
        const JsonValue* v = getPath(dotPath);
        return (v && v->isString()) ? v->string_val : def;
    }

    bool pathBool(const std::string& dotPath, bool def = false) const
    {
        const JsonValue* v = getPath(dotPath);
        return (v && v->isBool()) ? v->bool_val : def;
    }
};

// ─── Minimal JSON Parser ─────────────────────────────────────────────────────
// Handles: objects, arrays, strings, numbers (incl. scientific notation),
// booleans, null. Enough for our transformer JSON files.

class JsonParser
{
public:
    static Expected<JsonValue> parse(const std::string& input)
    {
        JsonParser p(input);
        p.skipWhitespace();
        if (p.pos_ >= p.src_.size())
            return Expected<JsonValue>::failure("Empty JSON input");

        auto result = p.parseValue();
        if (!result.ok) return result;

        p.skipWhitespace();
        if (p.pos_ < p.src_.size())
            return Expected<JsonValue>::failure(
                "Trailing content at position " + std::to_string(p.pos_));
        return result;
    }

private:
    explicit JsonParser(const std::string& src) : src_(src) {}

    const std::string& src_;
    size_t pos_ = 0;

    char peek() const { return (pos_ < src_.size()) ? src_[pos_] : '\0'; }
    char advance()    { return (pos_ < src_.size()) ? src_[pos_++] : '\0'; }

    void skipWhitespace()
    {
        while (pos_ < src_.size())
        {
            char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else if (c == '/' && pos_ + 1 < src_.size())
            {
                // Skip // line comments and /* block comments */
                if (src_[pos_ + 1] == '/')
                {
                    pos_ += 2;
                    while (pos_ < src_.size() && src_[pos_] != '\n') ++pos_;
                    if (pos_ < src_.size()) ++pos_;
                }
                else if (src_[pos_ + 1] == '*')
                {
                    pos_ += 2;
                    while (pos_ + 1 < src_.size() &&
                           !(src_[pos_] == '*' && src_[pos_ + 1] == '/'))
                        ++pos_;
                    if (pos_ + 1 < src_.size()) pos_ += 2;
                }
                else break;
            }
            else break;
        }
    }

    Expected<JsonValue> parseValue()
    {
        skipWhitespace();
        if (pos_ >= src_.size())
            return Expected<JsonValue>::failure("Unexpected end of input");

        char c = peek();
        if (c == '{')  return parseObject();
        if (c == '[')  return parseArray();
        if (c == '"')  return parseString();
        if (c == 't' || c == 'f') return parseBool();
        if (c == 'n')  return parseNull();
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();

        return Expected<JsonValue>::failure(
            std::string("Unexpected character '") + c + "' at position " + std::to_string(pos_));
    }

    Expected<JsonValue> parseObject()
    {
        advance(); // consume '{'
        JsonValue val;
        val.type = JsonValue::Object;

        skipWhitespace();
        if (peek() == '}') { advance(); return Expected<JsonValue>::success(std::move(val)); }

        while (true)
        {
            skipWhitespace();
            if (peek() != '"')
                return Expected<JsonValue>::failure(
                    "Expected '\"' for object key at position " + std::to_string(pos_));

            auto keyResult = parseString();
            if (!keyResult.ok) return Expected<JsonValue>::failure(keyResult.error);
            std::string key = std::move(keyResult.value.string_val);

            skipWhitespace();
            if (advance() != ':')
                return Expected<JsonValue>::failure(
                    "Expected ':' after key at position " + std::to_string(pos_ - 1));

            auto valResult = parseValue();
            if (!valResult.ok) return Expected<JsonValue>::failure(valResult.error);

            val.object_val[std::move(key)] = std::move(valResult.value);

            skipWhitespace();
            char c = peek();
            if (c == ',') { advance(); continue; }
            if (c == '}') { advance(); return Expected<JsonValue>::success(std::move(val)); }

            return Expected<JsonValue>::failure(
                "Expected ',' or '}' in object at position " + std::to_string(pos_));
        }
    }

    Expected<JsonValue> parseArray()
    {
        advance(); // consume '['
        JsonValue val;
        val.type = JsonValue::Array;

        skipWhitespace();
        if (peek() == ']') { advance(); return Expected<JsonValue>::success(std::move(val)); }

        while (true)
        {
            auto elemResult = parseValue();
            if (!elemResult.ok) return Expected<JsonValue>::failure(elemResult.error);
            val.array_val.push_back(std::move(elemResult.value));

            skipWhitespace();
            char c = peek();
            if (c == ',') { advance(); continue; }
            if (c == ']') { advance(); return Expected<JsonValue>::success(std::move(val)); }

            return Expected<JsonValue>::failure(
                "Expected ',' or ']' in array at position " + std::to_string(pos_));
        }
    }

    Expected<JsonValue> parseString()
    {
        advance(); // consume opening '"'
        std::string result;
        while (pos_ < src_.size())
        {
            char c = advance();
            if (c == '"')
            {
                JsonValue val;
                val.type = JsonValue::String;
                val.string_val = std::move(result);
                return Expected<JsonValue>::success(std::move(val));
            }
            if (c == '\\')
            {
                if (pos_ >= src_.size())
                    return Expected<JsonValue>::failure("Unterminated string escape");
                char esc = advance();
                switch (esc)
                {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case '/':  result += '/';  break;
                    case 'b':  result += '\b'; break;
                    case 'f':  result += '\f'; break;
                    case 'n':  result += '\n'; break;
                    case 'r':  result += '\r'; break;
                    case 't':  result += '\t'; break;
                    case 'u':
                        // Skip 4 hex digits (simplified: replace with '?')
                        for (int i = 0; i < 4 && pos_ < src_.size(); ++i) advance();
                        result += '?';
                        break;
                    default:
                        result += esc;
                        break;
                }
            }
            else
            {
                result += c;
            }
        }
        return Expected<JsonValue>::failure("Unterminated string");
    }

    Expected<JsonValue> parseNumber()
    {
        size_t start = pos_;
        if (peek() == '-') advance();

        if (peek() == '0')
        {
            advance();
        }
        else if (peek() >= '1' && peek() <= '9')
        {
            while (peek() >= '0' && peek() <= '9') advance();
        }
        else
        {
            return Expected<JsonValue>::failure(
                "Invalid number at position " + std::to_string(pos_));
        }

        // Fractional part
        if (peek() == '.')
        {
            advance();
            if (!(peek() >= '0' && peek() <= '9'))
                return Expected<JsonValue>::failure(
                    "Expected digit after '.' at position " + std::to_string(pos_));
            while (peek() >= '0' && peek() <= '9') advance();
        }

        // Exponent
        if (peek() == 'e' || peek() == 'E')
        {
            advance();
            if (peek() == '+' || peek() == '-') advance();
            if (!(peek() >= '0' && peek() <= '9'))
                return Expected<JsonValue>::failure(
                    "Expected digit in exponent at position " + std::to_string(pos_));
            while (peek() >= '0' && peek() <= '9') advance();
        }

        std::string numStr = src_.substr(start, pos_ - start);
        JsonValue val;
        val.type = JsonValue::Number;
        try
        {
            val.number_val = std::stod(numStr);
        }
        catch (...)
        {
            return Expected<JsonValue>::failure("Cannot parse number: " + numStr);
        }
        return Expected<JsonValue>::success(std::move(val));
    }

    Expected<JsonValue> parseBool()
    {
        if (src_.compare(pos_, 4, "true") == 0)
        {
            pos_ += 4;
            JsonValue val;
            val.type = JsonValue::Bool;
            val.bool_val = true;
            return Expected<JsonValue>::success(std::move(val));
        }
        if (src_.compare(pos_, 5, "false") == 0)
        {
            pos_ += 5;
            JsonValue val;
            val.type = JsonValue::Bool;
            val.bool_val = false;
            return Expected<JsonValue>::success(std::move(val));
        }
        return Expected<JsonValue>::failure(
            "Invalid boolean at position " + std::to_string(pos_));
    }

    Expected<JsonValue> parseNull()
    {
        if (src_.compare(pos_, 4, "null") == 0)
        {
            pos_ += 4;
            JsonValue val;
            val.type = JsonValue::Null;
            return Expected<JsonValue>::success(std::move(val));
        }
        return Expected<JsonValue>::failure(
            "Invalid null at position " + std::to_string(pos_));
    }
};

} // namespace json_detail

// ─── Preset info for directory scanning ──────────────────────────────────────

struct PresetInfo
{
    std::string name;          // Display name extracted from JSON "name" field
    std::string filePath;      // Absolute path to the .json file
    std::string materialFamily; // e.g. "mu_metal", "nife_50", "go_sife"
};

// ─── Material family string to enum mapping ──────────────────────────────────

namespace preset_detail {

inline MaterialFamily parseMaterialFamily(const std::string& str)
{
    // Normalize to lowercase for comparison
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "mu_metal" || lower == "mu-metal" || lower == "mumetal" ||
        lower == "mumetal_80nife" || lower == "mu_metal_80nife" ||
        lower.find("mu-metal") != std::string::npos ||
        lower.find("mu metal") != std::string::npos ||
        lower.find("80") != std::string::npos)
        return MaterialFamily::MuMetal_80NiFe;

    if (lower == "nife_50" || lower == "nife50" || lower == "ni-fe 50" ||
        lower.find("nife") != std::string::npos ||
        lower.find("alloy 2") != std::string::npos ||
        lower.find("radiometal") != std::string::npos)
        return MaterialFamily::NiFe_50;

    if (lower == "go_sife" || lower == "sife" || lower == "si-fe" ||
        lower.find("silicon") != std::string::npos ||
        lower.find("grain") != std::string::npos)
        return MaterialFamily::GO_SiFe;

    return MaterialFamily::Custom;
}

inline JAParameterSet defaultMaterialForFamily(MaterialFamily family)
{
    switch (family)
    {
        case MaterialFamily::MuMetal_80NiFe: return JAParameterSet::defaultMuMetal();
        case MaterialFamily::NiFe_50:        return JAParameterSet::defaultNiFe50();
        case MaterialFamily::GO_SiFe:        return JAParameterSet::defaultSiFe();
        case MaterialFamily::Custom:
        default:                             return JAParameterSet::defaultMuMetal();
    }
}

} // namespace preset_detail

// ─── PresetLoader ────────────────────────────────────────────────────────────

class PresetLoader
{
public:
    // ── Load from file path ──────────────────────────────────────────────────
    static Expected<TransformerConfig> loadFromFile(const std::string& filePath)
    {
        std::ifstream file(filePath);
        if (!file.is_open())
            return Expected<TransformerConfig>::failure(
                "Cannot open file: " + filePath);

        std::ostringstream ss;
        ss << file.rdbuf();
        if (file.fail() && !file.eof())
            return Expected<TransformerConfig>::failure(
                "Error reading file: " + filePath);

        return loadFromString(ss.str());
    }

    // ── Load from JSON string ────────────────────────────────────────────────
    static Expected<TransformerConfig> loadFromString(const std::string& jsonStr)
    {
        auto parseResult = json_detail::JsonParser::parse(jsonStr);
        if (!parseResult.ok)
            return Expected<TransformerConfig>::failure(
                "JSON parse error: " + parseResult.error);

        const auto& root = parseResult.value;
        if (!root.isObject())
            return Expected<TransformerConfig>::failure(
                "JSON root must be an object");

        return buildConfig(root);
    }

    // ── Scan a directory for .json preset files ──────────────────────────────
    static std::vector<PresetInfo> scanDirectory(const std::string& dirPath)
    {
        std::vector<PresetInfo> results;
        namespace fs = std::filesystem;

        std::error_code ec;
        if (!fs::is_directory(dirPath, ec))
            return results;

        for (const auto& entry : fs::directory_iterator(dirPath, ec))
        {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (ec) continue;

            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".json") continue;

            // Try to extract name and material from the file
            PresetInfo info;
            info.filePath = entry.path().string();

            // Quick parse to get name
            std::ifstream f(info.filePath);
            if (!f.is_open()) continue;

            std::ostringstream ss;
            ss << f.rdbuf();
            auto parsed = json_detail::JsonParser::parse(ss.str());
            if (!parsed.ok || !parsed.value.isObject()) continue;

            info.name = parsed.value.pathString("name",
                entry.path().stem().string());

            info.materialFamily = parsed.value.pathString("material_family",
                parsed.value.pathString("core_material", "unknown"));

            results.push_back(std::move(info));
        }

        // Sort by name
        std::sort(results.begin(), results.end(),
                  [](const PresetInfo& a, const PresetInfo& b) {
                      return a.name < b.name;
                  });

        return results;
    }

private:
    // ── Build TransformerConfig from parsed JSON ─────────────────────────────
    static Expected<TransformerConfig> buildConfig(const json_detail::JsonValue& root)
    {
        TransformerConfig cfg;

        // --- Name ---
        cfg.name = root.pathString("name", "Unnamed Preset");

        // --- Core Geometry ---
        cfg.core = buildCoreGeometry(root);

        // --- Windings ---
        cfg.windings = buildWindingConfig(root);

        // --- Material (Jiles-Atherton parameters) ---
        cfg.material = buildMaterial(root);

        // --- Top-level load impedance ---
        // Use electrical.load_impedance_ohm if present, else windings value
        const auto* elec = root.get("electrical");
        if (elec && elec->isObject())
        {
            const auto* loadZ = elec->get("load_impedance_ohm");
            if (loadZ && loadZ->isNumber())
                cfg.loadImpedance = loadZ->asFloat();
            else
                cfg.loadImpedance = cfg.windings.loadImpedance;
        }
        else
        {
            cfg.loadImpedance = cfg.windings.loadImpedance;
        }

        // --- Transformer Geometry (K_geo) ---
        cfg.geometry = buildGeometry(root);

        // --- LC Resonance Parameters ---
        cfg.lcParams = buildLCResonance(root);

        // --- Validation ---
        auto validationResult = validate(cfg);
        if (!validationResult.ok)
            return validationResult;

        return Expected<TransformerConfig>::success(std::move(cfg));
    }

    // ── Core Geometry ────────────────────────────────────────────────────────
    static CoreGeometry buildCoreGeometry(const json_detail::JsonValue& root)
    {
        CoreGeometry geo;

        // Check for explicit core_geometry section
        const auto* coreSection = root.get("core_geometry");
        if (coreSection && coreSection->isObject())
        {
            geo.Gamma_center  = coreSection->pathFloat("Gamma_center_m",  geo.Gamma_center);
            geo.Gamma_outer   = coreSection->pathFloat("Gamma_outer_m",   geo.Gamma_outer);
            geo.Gamma_yoke    = coreSection->pathFloat("Gamma_yoke_m",    geo.Gamma_yoke);
            geo.Lambda_center = coreSection->pathFloat("Lambda_center_m2", geo.Lambda_center);
            geo.Lambda_outer  = coreSection->pathFloat("Lambda_outer_m2",  geo.Lambda_outer);
            geo.Lambda_yoke   = coreSection->pathFloat("Lambda_yoke_m2",   geo.Lambda_yoke);
            geo.airGapLength  = coreSection->pathFloat("air_gap_length_m", geo.airGapLength);
            return geo;
        }

        // Check for air_gap section (as in neve_li1166_output.json)
        const auto* airGapSection = root.get("air_gap");
        if (airGapSection && airGapSection->isObject())
        {
            const auto* gapLen = airGapSection->get("gap_length_m");
            if (gapLen && gapLen->isNumber())
                geo.airGapLength = gapLen->asFloat();
        }

        // Check for "gapped" boolean
        const auto* gapped = root.get("gapped");
        if (gapped && gapped->isBool() && gapped->asBool() && geo.airGapLength == 0.0f)
        {
            // If gapped is true but no explicit gap length, use a default
            geo.airGapLength = 0.0001f; // 0.1mm default
        }

        // Infer geometry from known transformer types via material family
        std::string materialFamily = root.pathString("material_family", "");
        std::string name = root.pathString("name", "");

        // Try to infer from name/type for known transformers
        if (inferCoreFromKnown(name, materialFamily, geo.airGapLength > 0.0f, geo))
            return geo;

        return geo;
    }

    // Try to match known transformer core geometries by name
    static bool inferCoreFromKnown(const std::string& name,
                                   [[maybe_unused]] const std::string& materialFamily,
                                   [[maybe_unused]] bool isGapped,
                                   CoreGeometry& geo)
    {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower.find("jensen") != std::string::npos ||
            lower.find("jt-115") != std::string::npos ||
            lower.find("jt115") != std::string::npos)
        {
            geo = CoreGeometry::jensenJT115KE();
            return true;
        }

        return false;
    }

    // ── Winding Config ───────────────────────────────────────────────────────
    static WindingConfig buildWindingConfig(const json_detail::JsonValue& root)
    {
        WindingConfig w;

        const auto* elec = root.get("electrical");
        if (!elec || !elec->isObject())
            return w;

        w.turnsRatio_N1   = elec->pathInt("turns_N1", w.turnsRatio_N1);
        w.turnsRatio_N2   = elec->pathInt("turns_N2", w.turnsRatio_N2);
        w.Rdc_primary     = elec->pathFloat("Rdc_primary_ohm",    w.Rdc_primary);
        w.Rdc_secondary   = elec->pathFloat("Rdc_secondary_ohm",  w.Rdc_secondary);
        w.Lp_primary      = elec->pathFloat("Lp_estimated_H",     w.Lp_primary);
        w.L_leakage       = elec->pathFloat("L_leakage_H",        w.L_leakage);
        w.C_interwinding  = elec->pathFloat("C_interwinding_F",   w.C_interwinding);
        w.sourceImpedance = elec->pathFloat("source_impedance_ohm", w.sourceImpedance);
        w.loadImpedance   = elec->pathFloat("load_impedance_ohm",  w.loadImpedance);
        w.plateImpedance  = elec->pathFloat("plate_impedance_ohm", w.plateImpedance);

        // C_sec_shield may be present (Jensen format)
        w.C_sec_shield    = elec->pathFloat("C_sec_shield_F",     w.C_sec_shield);

        return w;
    }

    // ── Material (J-A parameters) ────────────────────────────────────────────
    static JAParameterSet buildMaterial(const json_detail::JsonValue& root)
    {
        // Check for explicit ja_parameters section
        const auto* jaSection = root.get("ja_parameters");
        if (jaSection && jaSection->isObject())
        {
            JAParameterSet mat;
            mat.Ms    = jaSection->pathFloat("Ms",    mat.Ms);
            mat.a     = jaSection->pathFloat("a",     mat.a);
            mat.alpha = jaSection->pathFloat("alpha", mat.alpha);
            mat.k     = jaSection->pathFloat("k",     mat.k);
            mat.c     = jaSection->pathFloat("c",     mat.c);
            mat.K1    = jaSection->pathFloat("K1",    mat.K1);
            mat.K2    = jaSection->pathFloat("K2",    mat.K2);
            return mat.clampToValid();
        }

        // Otherwise, infer from material_family or core_material string
        std::string familyStr = root.pathString("material_family",
            root.pathString("core_material", ""));

        MaterialFamily family = preset_detail::parseMaterialFamily(familyStr);
        return preset_detail::defaultMaterialForFamily(family);
    }

    // ── Transformer Geometry ─────────────────────────────────────────────────
    static TransformerGeometry buildGeometry(const json_detail::JsonValue& root)
    {
        TransformerGeometry geo;

        const auto* geoSection = root.get("geometry");
        if (geoSection && geoSection->isObject())
        {
            // Support both new "K_geo_m" key and legacy "K_geo_H" for backward compat
            geo.K_geo = geoSection->pathFloat("K_geo_m",
                            geoSection->pathFloat("K_geo_H", geo.K_geo));
        }

        return geo;
    }

    // ── LC Resonance Parameters ───────────────────────────────────────────────
    static LCResonanceParams buildLCResonance(const json_detail::JsonValue& root)
    {
        LCResonanceParams lc;

        const auto* lcSection = root.get("lc_resonance");
        if (lcSection && lcSection->isObject())
        {
            lc.Lleak = lcSection->pathFloat("Lleak_H", lc.Lleak);
            lc.Cw    = lcSection->pathFloat("Cw_F",    lc.Cw);
            lc.Cp_s  = lcSection->pathFloat("Cp_s_F",  lc.Cp_s);
            lc.CL    = lcSection->pathFloat("CL_F",    lc.CL);
            lc.Rz    = lcSection->pathFloat("Rz_ohm",  lc.Rz);
            lc.Cz    = lcSection->pathFloat("Cz_F",    lc.Cz);
        }

        return lc;
    }

    // ── Validation ───────────────────────────────────────────────────────────
    static Expected<TransformerConfig> validate(const TransformerConfig& cfg)
    {
        std::vector<std::string> errors;

        // Turns ratio must be positive
        if (cfg.windings.turnsRatio_N1 <= 0)
            errors.push_back("turnsRatio_N1 must be > 0 (got " +
                             std::to_string(cfg.windings.turnsRatio_N1) + ")");
        if (cfg.windings.turnsRatio_N2 <= 0)
            errors.push_back("turnsRatio_N2 must be > 0 (got " +
                             std::to_string(cfg.windings.turnsRatio_N2) + ")");

        // DC resistances must be non-negative
        if (cfg.windings.Rdc_primary < 0.0f)
            errors.push_back("Rdc_primary must be >= 0");
        if (cfg.windings.Rdc_secondary < 0.0f)
            errors.push_back("Rdc_secondary must be >= 0");

        // Inductances must be positive
        if (cfg.windings.Lp_primary <= 0.0f)
            errors.push_back("Lp_primary must be > 0 (got " +
                             std::to_string(cfg.windings.Lp_primary) + ")");
        if (cfg.windings.L_leakage < 0.0f)
            errors.push_back("L_leakage must be >= 0");

        // Capacitances must be non-negative
        if (cfg.windings.C_sec_shield < 0.0f)
            errors.push_back("C_sec_shield must be >= 0");
        if (cfg.windings.C_interwinding < 0.0f)
            errors.push_back("C_interwinding must be >= 0");

        // Core geometry: all dimensions positive
        if (cfg.core.Gamma_center <= 0.0f)
            errors.push_back("Gamma_center must be > 0");
        if (cfg.core.Lambda_center <= 0.0f)
            errors.push_back("Lambda_center must be > 0");
        if (cfg.core.airGapLength < 0.0f)
            errors.push_back("airGapLength must be >= 0");

        // Load impedance positive
        if (cfg.loadImpedance <= 0.0f)
            errors.push_back("loadImpedance must be > 0");

        // J-A stability condition: k > alpha * Ms
        if (!cfg.material.isPhysicallyValid())
        {
            float product = cfg.material.alpha * cfg.material.Ms;
            if (cfg.material.k <= product)
                errors.push_back(
                    "J-A stability violation: k (" +
                    std::to_string(cfg.material.k) + ") must be > alpha*Ms (" +
                    std::to_string(product) + ")");
            if (cfg.material.c < 0.0f || cfg.material.c > 1.0f)
                errors.push_back(
                    "J-A reversibility c must be in [0,1] (got " +
                    std::to_string(cfg.material.c) + ")");
            if (cfg.material.Ms <= 0.0f)
                errors.push_back("J-A Ms must be > 0");
            if (cfg.material.a <= 0.0f)
                errors.push_back("J-A a must be > 0");
        }

        if (!errors.empty())
        {
            std::string combined = "Validation failed for preset '" + cfg.name + "':\n";
            for (const auto& e : errors)
                combined += "  - " + e + "\n";
            return Expected<TransformerConfig>::failure(combined);
        }

        return Expected<TransformerConfig>::success(cfg);
    }
};

} // namespace transfo
