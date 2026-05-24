// ============================================
// ファイル: game_loader.cpp
// ゲームローダーの実装
// ============================================

#include "game_loader.hpp"
#include "config.hpp"
#include "sd_card.h"
#include "hw_config.h"
#include <cstdio>
#include <cstdlib>

/** FatFS ワークエリアをゼロ初期化 */
GameLoader::GameLoader() : mounted(false) {
    memset(&fs, 0, sizeof(FATFS));
}

/** マウント済みなら f_mount(nullptr) でアンマウント */
GameLoader::~GameLoader() {
    if (mounted) {
        const char* unmount_root = GameConfig::SD_ROOT[0] ? GameConfig::SD_ROOT : "0:";
        f_mount(nullptr, unmount_root, 0);
        mounted = false;
    }
}

/** sd_get_by_num(0) で SD を取得し f_mount する */
bool GameLoader::init() {
    // SDカードの取得
    sd_card_t* pSD = sd_get_by_num(0);
    if (!pSD) {
        printf("GameLoader: SDカードが見つかりません\n");
        return false;
    }
    
    // FATFSマウント
    const char* mount_root = GameConfig::SD_ROOT[0] ? GameConfig::SD_ROOT : "0:";
    printf("GameLoader: マウント開始 %s\n", mount_root);
    FRESULT fr = f_mount(&fs, mount_root, 1);
    if (fr != FR_OK) {
        printf("GameLoader: SDカードマウント失敗 (%d)\n", fr);
        return false;
    }
    
    printf("GameLoader: マウント成功\n");
    
    mounted = true;
    printf("GameLoader: 初期化完了\n");
    return true;
}

/** SD_ROOT/dir/filename を dest に連結 */
void GameLoader::buildPath(char* dest, const char* dir, const char* filename) {
    strcpy(dest, GameConfig::SD_ROOT);
    strcat(dest, dir);
    strcat(dest, "/");
    strcat(dest, filename);
}

/** games/ 内の各サブディレクトリを 1 ゲームとして列挙 */
int GameLoader::getGameList(GameInfo* games, int max_games) {
    if (!mounted) return 0;
    
    DIR dir;
    FILINFO fno;
    int count = 0;
    
    char games_path[128];
    strcpy(games_path, GameConfig::SD_ROOT);
    strcat(games_path, GameConfig::GAMES_DIR);
    
    printf("GameLoader: ゲームディレクトリを開く: %s\n", games_path);
    FRESULT fr = f_opendir(&dir, games_path);
    if (fr != FR_OK) {
        printf("GameLoader: ゲームディレクトリを開けません (%d)\n", fr);
        return 0;
    }
    
    while (count < max_games) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;
        if (strcmp(fno.fname, ".") == 0 || strcmp(fno.fname, "..") == 0) continue;
        
        if (fno.fattrib & AM_DIR) {
            // ディレクトリの場合、ゲームとして扱う
            GameInfo& game = games[count];
            strncpy(game.name, fno.fname, sizeof(game.name) - 1);
            game.name[sizeof(game.name) - 1] = '\0';
            
            // プログラムファイルパス
            buildPath(game.program_path, GameConfig::GAMES_DIR, fno.fname);
            strcat(game.program_path, "/game.bin");

            // もし game.lua が存在すればそちらを優先する
            char lua_path[128];
            buildPath(lua_path, GameConfig::GAMES_DIR, fno.fname);
            strcat(lua_path, "/game.lua");
            if (fileExists(lua_path)) {
                strncpy(game.program_path, lua_path, sizeof(game.program_path)-1);
                game.program_path[sizeof(game.program_path)-1] = '\0';
            }
            
            // 画像ファイルパス
            buildPath(game.image_path, GameConfig::GAMES_DIR, fno.fname);
            strcat(game.image_path, "/icon.bmp");
            
            // 音声ファイルパス
            buildPath(game.audio_path, GameConfig::GAMES_DIR, fno.fname);
            strcat(game.audio_path, "/bgm.wav");
            
            game.valid = true;
            count++;
        }
    }
    
    f_closedir(&dir);
    printf("GameLoader: %d games found\n", count);
    return count;
}

/** 240x320 RGB565 生データとして読み込む */
ImageData GameLoader::loadImage(const char* path) {
    ImageData result = {nullptr, 0, 0, false};
    
    if (!mounted) return result;
    
    FIL file;
    FRESULT fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("GameLoader: 画像ファイルを開けません: %s (%d)\n", path, fr);
        return result;
    }
    
    // 簡易実装：RGB565形式の生データを想定
    // 実際の実装では、BMP/PNG等のフォーマットを解析する必要があります
    UINT bytes_read;
    uint32_t file_size = f_size(&file);
    
    // 画像サイズを仮定（240x320の場合）
    uint16_t width = 240;
    uint16_t height = 320;
    uint32_t expected_size = width * height * 2;  // RGB565 = 2バイト/ピクセル
    
    if (file_size < expected_size) {
        printf("GameLoader: 画像ファイルサイズが不正です\n");
        f_close(&file);
        return result;
    }
    
    result.pixels = new uint16_t[width * height];
    if (!result.pixels) {
        printf("GameLoader: メモリ確保失敗\n");
        f_close(&file);
        return result;
    }
    
    fr = f_read(&file, result.pixels, expected_size, &bytes_read);
    f_close(&file);
    
    if (fr != FR_OK || bytes_read != expected_size) {
        printf("GameLoader: 画像読み込み失敗\n");
        delete[] result.pixels;
        result.pixels = nullptr;
        return result;
    }
    
    result.width = width;
    result.height = height;
    result.valid = true;
    
    printf("GameLoader: 画像読み込み成功 (%dx%d)\n", width, height);
    return result;
}

/** 簡易 WAV ヘッダ解析後 PCM データを malloc */
bool GameLoader::loadAudioWAV(const char* path, int16_t** samples, uint32_t* sample_count, uint32_t* sample_rate) {
    if (!mounted) return false;
    
    FIL file;
    FRESULT fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("GameLoader: 音声ファイルを開けません: %s\n", path);
        return false;
    }
    
    // WAVヘッダー読み込み（簡易実装）
    uint8_t header[44];
    UINT bytes_read;
    fr = f_read(&file, header, 44, &bytes_read);
    if (fr != FR_OK || bytes_read != 44) {
        printf("GameLoader: WAVヘッダー読み込み失敗\n");
        f_close(&file);
        return false;
    }
    
    // "RIFF"チェック
    if (header[0] != 'R' || header[1] != 'I' || header[2] != 'F' || header[3] != 'F') {
        printf("GameLoader: WAV形式ではありません\n");
        f_close(&file);
        return false;
    }
    
    // サンプルレート取得（22バイト目から）
    *sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    
    // データサイズ取得（40バイト目から）
    uint32_t data_size = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);
    *sample_count = data_size / 2;  // 16bitステレオ = 2バイト/サンプル
    
    // サンプルデータ読み込み
    *samples = (int16_t*)malloc(data_size);
    if (!*samples) {
        printf("GameLoader: メモリ確保失敗\n");
        f_close(&file);
        return false;
    }
    
    fr = f_read(&file, *samples, data_size, &bytes_read);
    f_close(&file);
    
    if (fr != FR_OK || bytes_read != data_size) {
        printf("GameLoader: 音声データ読み込み失敗\n");
        free(*samples);
        *samples = nullptr;
        return false;
    }
    
    printf("GameLoader: 音声読み込み成功 (%lu サンプル, %lu Hz)\n", *sample_count, *sample_rate);
    return true;
}

/** ファイル全バイトを malloc して読み込む */
bool GameLoader::loadProgram(const char* path, uint8_t** data, size_t* size) {
    if (!mounted) return false;
    
    printf("GameLoader: loadProgram path=%s\n", path);
    FIL file;
    FRESULT fr = f_open(&file, path, FA_READ);
    if (fr != FR_OK) {
        printf("GameLoader: プログラムファイルを開けません: %s (%d)\n", path, fr);
        return false;
    }
    
    *size = f_size(&file);
    *data = (uint8_t*)malloc(*size);
    if (!*data) {
        printf("GameLoader: メモリ確保失敗\n");
        f_close(&file);
        return false;
    }
    
    UINT bytes_read;
    fr = f_read(&file, *data, *size, &bytes_read);
    f_close(&file);
    
    if (fr != FR_OK || bytes_read != *size) {
        printf("GameLoader: プログラム読み込み失敗\n");
        free(*data);
        *data = nullptr;
        return false;
    }
    
    printf("GameLoader: プログラム読み込み成功 (%zu バイト)\n", *size);
    return true;
}

/** f_stat でファイル存在確認 */
bool GameLoader::fileExists(const char* path) {
    if (!mounted) return false;
    
    FILINFO fno;
    FRESULT fr = f_stat(path, &fno);
    return (fr == FR_OK);
}

/** f_opendir / f_readdir でエントリ名を printf */
void GameLoader::listDirectory(const char* path) {
    if (!mounted) return;
    
    DIR dir;
    FILINFO fno;
    
    FRESULT fr = f_opendir(&dir, path);
    if (fr != FR_OK) {
        printf("GameLoader: ディレクトリを開けません: %s\n", path);
        return;
    }
    
    printf("--- ディレクトリ一覧: %s ---\n", path);
    while (true) {
        fr = f_readdir(&dir, &fno);
        if (fr != FR_OK || fno.fname[0] == 0) break;
        
        printf("  %s [%s]\n", fno.fname, (fno.fattrib & AM_DIR) ? "DIR" : "FILE");
    }
    
    f_closedir(&dir);
}
