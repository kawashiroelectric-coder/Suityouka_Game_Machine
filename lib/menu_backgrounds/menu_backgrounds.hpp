// ============================================
// ファイル: menu_backgrounds.hpp
// ゲーム選択・システム設定メニュー共通の BG 画像テーブル
// ============================================

#ifndef MENU_BACKGROUNDS_HPP
#define MENU_BACKGROUNDS_HPP

#include <cstdint>

struct MenuBgEntry {
    const uint16_t* pixels;
    int width;
    int height;
};

extern const MenuBgEntry kMenuBackgrounds[];
extern const int kMenuBackgroundCount;

#endif  // MENU_BACKGROUNDS_HPP
