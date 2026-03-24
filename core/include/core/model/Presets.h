#pragma once

// =============================================================================
// Presets — Factory presets + dynamic user presets for audio transformers.
//
// The 2 factory presets (indices 0-1) are always available and immutable.
// User presets loaded from JSON files are appended after the factory preset.
//
// Usage:
//   PresetManager mgr;
//   mgr.loadFromDirectory("data/transformers/");
//   int total = mgr.getPresetCount();       // 2 + loaded
//   auto cfg  = mgr.getByIndex(2);          // first user preset
//   mgr.addPreset(myConfig);                // add at runtime
//   mgr.removePreset(1);                    // remove user preset (not factory)
// =============================================================================

#include "TransformerConfig.h"
#include "PresetLoader.h"
#include <vector>
#include <string>
#include <algorithm>
#include <mutex>

namespace transfo {

// ── Factory preset access (free functions, backward-compatible) ──────────────

namespace Presets
{
    inline TransformerConfig Jensen_JT115KE()           { return TransformerConfig::Jensen_JT115KE(); }
    inline TransformerConfig Jensen_JT11ELCF()          { return TransformerConfig::Jensen_JT11ELCF(); }

    constexpr int kFactoryCount = 15;

    inline int count() { return kFactoryCount; }

    inline TransformerConfig getByIndex(int index)
    {
        switch (index)
        {
            case 0:  return TransformerConfig::Jensen_JT115KE();
            case 1:  return TransformerConfig::Jensen_JT11ELCF();
            case 2:  return TransformerConfig::Neve_10468_Input();
            case 3:  return TransformerConfig::Neve_LI1166_Output();
            case 4:  return TransformerConfig::API_AP2503();
            case 5:  return TransformerConfig::Lundahl_LL1538();
            case 6:  return TransformerConfig::Fender_Deluxe_OT();
            case 7:  return TransformerConfig::Vox_AC30_OT();
            case 8:  return TransformerConfig::UTC_HA100X();
            case 9:  return TransformerConfig::Clean_DI();
            case 10: return TransformerConfig::Vocal_Warmth();
            case 11: return TransformerConfig::Bass_Thickener();
            case 12: return TransformerConfig::Drum_Punch();
            case 13: return TransformerConfig::Guitar_Crunch();
            case 14: return TransformerConfig::Master_Glue();
            default: return TransformerConfig::Jensen_JT115KE();
        }
    }

    inline const char* getNameByIndex(int index)
    {
        switch (index)
        {
            case 0:  return "Jensen JT-115K-E";
            case 1:  return "Jensen JT-11ELCF";
            case 2:  return "Neve 10468 Input";
            case 3:  return "Neve LI1166 Output";
            case 4:  return "API AP2503";
            case 5:  return "Lundahl LL1538";
            case 6:  return "Fender Deluxe OT";
            case 7:  return "Vox AC30 OT";
            case 8:  return "UTC HA-100X";
            case 9:  return "Clean DI";
            case 10: return "Vocal Warmth";
            case 11: return "Bass Thickener";
            case 12: return "Drum Punch";
            case 13: return "Guitar Crunch";
            case 14: return "Master Glue";
            default: return "Unknown";
        }
    }
}

// ── PresetManager — combines factory + dynamic user presets ──────────────────

class PresetManager
{
public:
    PresetManager() = default;

    // ── Total preset count (factory + user) ──────────────────────────────────
    int getPresetCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return Presets::kFactoryCount + static_cast<int>(userPresets_.size());
    }

    // ── Factory preset count ─────────────────────────────────────────────────
    static constexpr int getFactoryCount() { return Presets::kFactoryCount; }

    // ── User preset count ────────────────────────────────────────────────────
    int getUserPresetCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(userPresets_.size());
    }

    // ── Get preset by global index (0..N-1) ──────────────────────────────────
    // Index 0 is the factory preset, 1+ are user presets.
    TransformerConfig getByIndex(int index) const
    {
        if (index < Presets::kFactoryCount)
            return Presets::getByIndex(index);

        std::lock_guard<std::mutex> lock(mutex_);
        int userIdx = index - Presets::kFactoryCount;
        if (userIdx >= 0 && userIdx < static_cast<int>(userPresets_.size()))
            return userPresets_[static_cast<size_t>(userIdx)];

        return Presets::getByIndex(0); // fallback
    }

    // ── Get preset name by global index ──────────────────────────────────────
    std::string getNameByIndex(int index) const
    {
        if (index < Presets::kFactoryCount)
            return Presets::getNameByIndex(index);

        std::lock_guard<std::mutex> lock(mutex_);
        int userIdx = index - Presets::kFactoryCount;
        if (userIdx >= 0 && userIdx < static_cast<int>(userPresets_.size()))
            return userPresets_[static_cast<size_t>(userIdx)].name;

        return "Unknown";
    }

    // ── Check if index is a factory preset ───────────────────────────────────
    static bool isFactoryPreset(int index) { return index < Presets::kFactoryCount; }

    // ── Add a user preset ────────────────────────────────────────────────────
    // Returns the global index of the newly added preset.
    int addPreset(const TransformerConfig& preset)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        userPresets_.push_back(preset);
        return Presets::kFactoryCount + static_cast<int>(userPresets_.size()) - 1;
    }

    // ── Remove a user preset by global index ─────────────────────────────────
    // Returns true if successfully removed, false if index is out of range
    // or refers to a factory preset.
    bool removePreset(int globalIndex)
    {
        if (globalIndex < Presets::kFactoryCount)
            return false; // Cannot remove factory presets

        std::lock_guard<std::mutex> lock(mutex_);
        int userIdx = globalIndex - Presets::kFactoryCount;
        if (userIdx < 0 || userIdx >= static_cast<int>(userPresets_.size()))
            return false;

        userPresets_.erase(userPresets_.begin() + userIdx);
        return true;
    }

    // ── Replace a user preset ────────────────────────────────────────────────
    bool replacePreset(int globalIndex, const TransformerConfig& preset)
    {
        if (globalIndex < Presets::kFactoryCount)
            return false;

        std::lock_guard<std::mutex> lock(mutex_);
        int userIdx = globalIndex - Presets::kFactoryCount;
        if (userIdx < 0 || userIdx >= static_cast<int>(userPresets_.size()))
            return false;

        userPresets_[static_cast<size_t>(userIdx)] = preset;
        return true;
    }

    // ── Clear all user presets ───────────────────────────────────────────────
    void clearUserPresets()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        userPresets_.clear();
        loadErrors_.clear();
    }

    // ── Load all JSON presets from a directory ───────────────────────────────
    // Returns the number of presets successfully loaded.
    int loadFromDirectory(const std::string& dirPath)
    {
        auto infos = PresetLoader::scanDirectory(dirPath);
        int loaded = 0;

        for (const auto& info : infos)
        {
            auto result = PresetLoader::loadFromFile(info.filePath);
            if (result.ok)
            {
                addPreset(result.value);
                ++loaded;
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex_);
                loadErrors_.push_back(info.filePath + ": " + result.error);
            }
        }

        return loaded;
    }

    // ── Load a single preset from a JSON file ────────────────────────────────
    // Returns the global index on success, -1 on failure.
    int loadFromFile(const std::string& filePath)
    {
        auto result = PresetLoader::loadFromFile(filePath);
        if (!result.ok)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            loadErrors_.push_back(filePath + ": " + result.error);
            return -1;
        }

        return addPreset(result.value);
    }

    // ── Get load errors (for diagnostics / UI display) ───────────────────────
    std::vector<std::string> getLoadErrors() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return loadErrors_;
    }

    // ── Find preset index by name ────────────────────────────────────────────
    // Returns -1 if not found.
    int findByName(const std::string& name) const
    {
        // Search factory presets
        for (int i = 0; i < Presets::kFactoryCount; ++i)
        {
            if (Presets::getNameByIndex(i) == name)
                return i;
        }

        // Search user presets
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < userPresets_.size(); ++i)
        {
            if (userPresets_[i].name == name)
                return Presets::kFactoryCount + static_cast<int>(i);
        }

        return -1;
    }

    // ── Get all preset names (factory + user) ────────────────────────────────
    std::vector<std::string> getAllPresetNames() const
    {
        std::vector<std::string> names;
        names.reserve(static_cast<size_t>(getPresetCount()));

        for (int i = 0; i < Presets::kFactoryCount; ++i)
            names.push_back(Presets::getNameByIndex(i));

        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& p : userPresets_)
            names.push_back(p.name);

        return names;
    }

private:
    std::vector<TransformerConfig>  userPresets_;
    std::vector<std::string>        loadErrors_;
    mutable std::mutex              mutex_;
};

} // namespace transfo
