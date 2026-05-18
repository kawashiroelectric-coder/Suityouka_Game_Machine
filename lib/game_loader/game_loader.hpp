// ============================================
// ファイル: game_loader.hpp
// SDカードからゲーム・画像・音声を読み込む機能
// ============================================

#ifndef GAME_LOADER_HPP
#define GAME_LOADER_HPP

#include "pico/stdlib.h"
#include "ff.h"
#include "st7789_lcd.hpp"
#include <cstdint>
#include <cstring>

// ゲーム情報構造体
struct GameInfo {
    char name[64];
    char program_path[128];
    char image_path[128];
    char audio_path[128];
    bool valid;
};

// 画像読み込み結果
struct ImageData {
    uint16_t* pixels;
    uint16_t width;
    uint16_t height;
    bool valid;
    
    ~ImageData() {
        if (pixels) {
            delete[] pixels;
            pixels = nullptr;
        }
    }
};

// ゲームローダークラス
class GameLoader {
private:
    FATFS fs;
    bool mounted;
    
    // ファイルパス構築
    void buildPath(char* dest, const char* dir, const char* filename);
    
    // 画像ファイル読み込み（簡易実装：RGB565形式を想定）
    ImageData loadImageRGB565(const char* path);
    
public:
    GameLoader();
    ~GameLoader();
    
    // SDカード初期化・マウント
    bool init();
    
    // マウント状態確認
    bool isMounted() const { return mounted; }
    
    // ゲーム一覧取得
    int getGameList(GameInfo* games, int max_games);
    
    // 画像読み込み（RGB565形式）
    ImageData loadImage(const char* path);
    
    // 音声ファイル読み込み（WAV形式、簡易実装）
    bool loadAudioWAV(const char* path, int16_t** samples, uint32_t* sample_count, uint32_t* sample_rate);
    
    // プログラムファイル読み込み
    bool loadProgram(const char* path, uint8_t** data, size_t* size);
    
    // ファイル存在確認
    bool fileExists(const char* path);
    
    // ディレクトリ一覧表示
    void listDirectory(const char* path);
};

#endif // GAME_LOADER_HPP
