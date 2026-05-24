// ============================================
// ファイル: game_loader.hpp
// SDカードからゲーム・画像・音声を読み込む機能
// ============================================

#ifndef GAME_LOADER_HPP
#define GAME_LOADER_HPP

#include "pico/stdlib.h"
#include "st7789_lcd.hpp"
#include <cstdint>
#include <cstring>

/** games/ 以下の 1 ゲーム分のメタ情報 */
struct GameInfo {
    char name[64];
    char program_path[128];
    char image_path[128];
    char audio_path[128];
    bool valid;
};

/** RGB565 ピクセル配列（デストラクタで pixels を解放） */
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

/** FatFS 経由で SD 上のゲーム資産を読み込む */
class GameLoader {
private:
    FATFS fs;
    bool mounted;
    
    /** SD_ROOT + dir + filename を dest に連結する */
    void buildPath(char* dest, const char* dir, const char* filename);
    
    ImageData loadImageRGB565(const char* path);
    
public:
    GameLoader();
    ~GameLoader();
    
    /** SD ドライバ初期化と FatFS マウント */
    bool init();
    
    /** FatFS がマウント済みか */
    bool isMounted() const { return mounted; }
    
    /** games/ 配下のサブディレクトリを列挙し GameInfo を埋める。戻り値は件数 */
    int getGameList(GameInfo* games, int max_games);
    
    /** RGB565 生データとして画像を読み込む（240x320 固定想定） */
    ImageData loadImage(const char* path);
    
    /** WAV（PCM 16bit）を読み込み、呼び出し側で free(*samples) する */
    bool loadAudioWAV(const char* path, int16_t** samples, uint32_t* sample_count, uint32_t* sample_rate);
    
    /** 任意バイナリを malloc して読み込む */
    bool loadProgram(const char* path, uint8_t** data, size_t* size);
    
    /** f_stat でファイルの存在を確認する */
    bool fileExists(const char* path);
    
    /** ディレクトリ内容をシリアルに一覧表示する（デバッグ用） */
    void listDirectory(const char* path);
};

#endif // GAME_LOADER_HPP
