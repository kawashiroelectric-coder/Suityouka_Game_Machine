// ============================================
// ファイル: font_renderer.hpp
// MISF サブセットフォント（8x8 / 12x12 等）UTF-8 描画
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
    /** セッション終了時: malloc 起点を raw free（HeapBudget 迂回） */
    void unloadRaw();

    bool isLoaded() const { return glyph_data_ != nullptr; }
    uint8_t glyphWidth() const { return glyph_w_; }
    uint8_t glyphHeight() const { return glyph_h_; }
    uint8_t defaultAdvance() const { return default_advance_; }

    /** 描画倍率（整数比 num/den。例: 3/2 で 1.5 倍） */
    void setScale(uint8_t num, uint8_t den);
    uint8_t scaleNumerator() const { return scale_num_; }
    uint8_t scaleDenominator() const { return scale_den_; }

    uint8_t scaledGlyphWidth() const { return scaleValue(glyph_w_); }
    uint8_t scaledGlyphHeight() const { return scaleValue(glyph_h_); }
    uint8_t scaledDefaultAdvance() const { return scaleValue(default_advance_); }

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

    uint8_t scale_num_ = 1;
    uint8_t scale_den_ = 1;

    uint8_t scaleValue(uint8_t value) const;
    uint8_t bytesPerRow() const;
    bool glyphPixel(const uint8_t* glyph, int row, int col) const;
    const IndexEntry* findGlyph(uint32_t codepoint) const;
    void drawGlyph(uint16_t* fb, uint16_t fb_w, uint16_t band_rows, int band_y0, int x, int y,
                   const uint8_t* glyph, uint16_t fg, uint16_t bg) const;
};

#endif  // FONT_RENDERER_HPP
