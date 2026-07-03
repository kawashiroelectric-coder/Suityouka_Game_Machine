// ============================================
// ファイル: menu_backgrounds.cpp
// ============================================

#include "menu_backgrounds.hpp"

#include "BG1.h"
#include "BG2.h"
#include "BG3.h"
#include "BG4.h"

const MenuBgEntry kMenuBackgrounds[] = {
    {BG1_pixels, BG1_width, BG1_height},
    {BG2_pixels, BG2_width, BG2_height},
    {BG3_pixels, BG3_width, BG3_height},
    {BG4_pixels, BG4_width, BG4_height},
};

const int kMenuBackgroundCount = static_cast<int>(sizeof(kMenuBackgrounds) / sizeof(kMenuBackgrounds[0]));

static_assert(sizeof(kMenuBackgrounds) / sizeof(kMenuBackgrounds[0]) == 4,
              "menu background table must have 4 entries");
