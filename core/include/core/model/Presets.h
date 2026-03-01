#pragma once

// =============================================================================
// Presets — Factory presets for known audio transformers.
// =============================================================================

#include "TransformerConfig.h"

namespace transfo {

namespace Presets
{
    inline TransformerConfig Jensen_JT115KE()       { return TransformerConfig::Jensen_JT115KE(); }
    inline TransformerConfig Neve_1073_Input()       { return TransformerConfig::Neve_1073_Input(); }
    inline TransformerConfig Neve_1073_Output()      { return TransformerConfig::Neve_1073_Output(); }
    inline TransformerConfig API_AP2503()             { return TransformerConfig::API_AP2503(); }

    // Legacy alias
    inline TransformerConfig Neve_Marinair_LO1166()  { return Neve_1073_Output(); }

    inline int count() { return 4; }

    inline TransformerConfig getByIndex(int index)
    {
        switch (index)
        {
            case 0: return Jensen_JT115KE();
            case 1: return Neve_1073_Input();
            case 2: return Neve_1073_Output();
            case 3: return API_AP2503();
            default: return Jensen_JT115KE();
        }
    }

    inline const char* getNameByIndex(int index)
    {
        switch (index)
        {
            case 0: return "Jensen JT-115K-E";
            case 1: return "Neve 1073 Input (10468)";
            case 2: return "Neve 1073 Output (LI1166)";
            case 3: return "API AP2503";
            default: return "Unknown";
        }
    }
}

} // namespace transfo
