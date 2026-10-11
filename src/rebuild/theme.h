#pragma once

#include "../graphics.h"

namespace rebuild {

// Semantic tokens only: screens never own literal colours or spacing rules.
struct Theme {
    Color backgroundTop;
    Color backgroundBottom;
    Color surface;
    Color surfaceSelected;
    Color border;
    Color accent;
    Color accentSoft;
    Color textPrimary;
    Color textSecondary;
    Color textMuted;
    Color danger;
    int radius;
    int rowHeight;
    int gap;
    int contentLeft;
};

inline Theme midnightTheme() {
    return {{4, 15, 32}, {2, 7, 17}, {19, 35, 58}, {32, 62, 96},
        {92, 119, 151}, {65, 170, 255}, {18, 76, 132}, {248, 251, 255},
        {177, 197, 220}, {119, 145, 175}, {255, 92, 99}, 20, 84, 12, 148};
}

}
