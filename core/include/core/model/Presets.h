#pragma once

// =============================================================================
// Presets — Factory presets for known audio transformers.
// =============================================================================

#include "TransformerConfig.h"

namespace transfo {

namespace Presets
{
    inline TransformerConfig Jensen_JT115KE()       { return TransformerConfig::Jensen_JT115KE(); }
    inline TransformerConfig Neve_Marinair_LO1166() { return TransformerConfig::Neve_Marinair_LO1166(); }
    inline TransformerConfig API_AP2503()            { return TransformerConfig::API_AP2503(); }

    inline int count() { return 3; }

    inline TransformerConfig getByIndex(int index)
    {
        switch (index)
        {
            case 0: return Jensen_JT115KE();
            case 1: return Neve_Marinair_LO1166();
            case 2: return API_AP2503();
            default: return Jensen_JT115KE();
        }
    }

    inline const char* getNameByIndex(int index)
    {
        switch (index)
        {
            case 0: return "Jensen JT-115K-E";
            case 1: return "Neve Marinair LO1166";
            case 2: return "API AP2503";
            default: return "Unknown";
        }
    }
}

} // namespace transfo
