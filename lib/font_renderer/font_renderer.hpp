// ============================================
// ファイル: font_renderer.hpp
// MISF サブセットフォント（美咲 8x8）UTF-8 描画
// ============================================

#ifndef FONT_RENDERER_HPP
#define FONT_RENDERER_HPP

#include <cstddef>
#include <cstdint>

/** SD 上の .bin（generate_font.py 生成）を読み込み UTF-8 テキストを描画 */
class FontRenderer {
public:
    static constexpr char kMagic[4] = {'M', 'I', 'S', 'F'};

    FontRenderer();
    ~FontRenderer();

    FontRenderer(const FontRenderer&) = delete;
    FontRenderer& operator=(const FontRenderer&) = delete;

    /** SD から MISF v1 を読み込む */
    bool loadFromSd(const char* path);
    void unload();

    bool isLoaded() const { return glyph_data_ != nullptr; }
    uint8_t glyphWidth() const { return glyph_w_; }
    uint8_t glyphHeight() const { return glyph_h_; }
    uint8_t defaultAdvance() const { return default_advance_; }

    /**
     * バンド FB へ UTF-8 テキスト描画（背景付き）。
     * band_rows / band_y0: GameDisplay の現在バンド座標系。
     */
    void drawTextBg(uint16_t* fb, uint16_t fb_w, uint16_t band_rows, int band_y0, int x, int y,
                    const char* utf8, uint16_t fg, uint16_t bg) const;

    /** グローバルアクティブフォント（GameDisplay が参照） */
    static FontRenderer* active() { return s_active_; }
    static void setActive(FontRenderer* font) { s_active_ = font; }

private:
    struct IndexEntry {
        uint32_t codepoint;
        uint8_t advance;
        uint8_t flags;
        uint16_t glyph_index;
    } __attribute__((packed));

    static FontRenderer* s_active_;

    uint8_t glyph_w_ = 8;
    uint8_t glyph_h_ = 8;
    uint8_t default_advance_ = 8;
    uint16_t glyph_count_ = 0;
    uint16_t bytes_per_glyph_ = 8;
    IndexEntry* index_ = nullptr;
    uint8_t* glyph_data_ = nullptr;
    size_t alloc_bytes_ = 0;

    const IndexEntry* findGlyph(uint32_t codepoint) const;
    void drawGlyph(uint16_t* fb, uint16_t fb_w, uint16_t band_rows, int band_y0, int x, int y,
                   const uint8_t* glyph, uint16_t fg, uint16_t bg) const;
};

#endif  // FONT_RENDERER_HPP
