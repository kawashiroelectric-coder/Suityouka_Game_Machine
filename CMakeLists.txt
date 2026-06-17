# ============================================
# Raspberry Pi Pico 2W ゲーム機プロジェクト
# ============================================

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(Suityouka_Game_Machine C CXX ASM)

# Lua ゲーム中の FPS / RAM オーバーレイ（cmake -DGAME_MACHINE_DEBUG=ON）
option(GAME_MACHINE_DEBUG "Enable FPS/RAM debug overlay during Lua games" ON)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# ============================================
# ソースファイル
# ============================================
add_executable(Suityouka_Game_Machine 
    # メインループ
    game_machine_main.cpp
    
    # SDカード (carlk3/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
    lib/sd_card_hw/hw_config.c
    lib/sd_card_hw/sd_debug.c
    lib/sd_service/sd_service.cpp
    
    # ボタン入力管理
    lib/button_input/button_input.cpp
    lib/encoder_input/encoder_input.cpp
    lib/input_test_mode/input_test_mode.cpp
    lib/game_catalog/game_catalog.cpp
    lib/game_select_menu/game_select_menu.cpp
    lib/system_settings_menu/system_settings_menu.cpp
    lib/encoder_volume/encoder_volume.cpp
    lib/device_settings/device_settings.cpp
    
    # 音声出力管理
    lib/audio_output/audio_output.cpp
    lib/battery_monitor/battery_monitor.cpp

    # SD上 Lua スクリプト実行
    lib/heap_budget/heap_budget.cpp
    lib/lua_interpreter/lua_interpreter.cpp
    lib/lua_interpreter/bg_stream_util.cpp
    lib/lua_interpreter/vn_stream_compose.cpp
    lib/lua_interpreter/lua_api_internal.cpp
    lib/lua_interpreter/lua_api_draw.cpp
    lib/lua_interpreter/lua_api_audio.cpp
    lib/lua_interpreter/lua_api_machine.cpp
    lib/lua_interpreter/lua_save_data.cpp
    lib/lua_interpreter/debug_overlay.cpp
    lib/lua_interpreter/lua_audio.cpp

    # ゲーム描画 (RGB565 framebuffer)
    lib/game_display/game_display.cpp
    lib/font_renderer/font_renderer.cpp
    lib/tile_layers/tile_layers.cpp
    
    # ゲームローダー（SDカード読み込み）
    #lib/game_loader/game_loader.cpp
    
    # LCD表示制御
    lib/ST7789/st7789_lcd.cpp

)

# プロジェクト情報
pico_set_program_name(Suityouka_Game_Machine "Suityouka_Game_Machine")
pico_set_program_version(Suityouka_Game_Machine "0.1")

# PIOヘッダー生成
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/blink.pio)
    pico_generate_pio_header(Suityouka_Game_Machine ${CMAKE_CURRENT_LIST_DIR}/blink.pio)
endif()
if(EXISTS ${CMAKE_CURRENT_LIST_DIR}/lib/pcm5102_i2s/pcm5102_i2s.pio)
    pico_generate_pio_header(Suityouka_Game_Machine
        ${CMAKE_CURRENT_LIST_DIR}/lib/pcm5102_i2s/pcm5102_i2s.pio)
endif()

# ============================================
# 標準入出力設定
# ============================================
# UART: 無効
# USB: 有効（シリアル通信用）
pico_enable_stdio_uart(Suityouka_Game_Machine 0)
pico_enable_stdio_usb(Suityouka_Game_Machine 1)

# ============================================
# インクルードディレクトリ
# ============================================
target_include_directories(Suityouka_Game_Machine PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}                    # プロジェクトルート（config.hpp等）
    ${CMAKE_CURRENT_LIST_DIR}/lib/button_input   # ボタン入力ライブラリのヘッダー
    ${CMAKE_CURRENT_LIST_DIR}/lib/encoder_input  # ロータリーエンコーダ
    ${CMAKE_CURRENT_LIST_DIR}/lib/input_test_mode # SD 未検知時入力テスト
    ${CMAKE_CURRENT_LIST_DIR}/lib/game_catalog    # /games ゲーム検出
    ${CMAKE_CURRENT_LIST_DIR}/lib/game_select_menu # ゲーム選択メニュー
    ${CMAKE_CURRENT_LIST_DIR}/lib/system_settings_menu # システム設定メニュー
    ${CMAKE_CURRENT_LIST_DIR}/lib/encoder_volume  # エンコーダ音量
    ${CMAKE_CURRENT_LIST_DIR}/lib/device_settings # フラッシュ永続設定
    ${CMAKE_CURRENT_LIST_DIR}/lib/audio_output   # 音声出力ライブラリのヘッダー
    ${CMAKE_CURRENT_LIST_DIR}/lib/battery_monitor # バッテリー ADC (Core1)
    ${CMAKE_CURRENT_LIST_DIR}/lib/heap_budget      # 動的ヒープ予算
    ${CMAKE_CURRENT_LIST_DIR}/lib/lua_interpreter  # Lua インタプリタ
    ${CMAKE_CURRENT_LIST_DIR}/lib/game_display    # ゲーム描画
    ${CMAKE_CURRENT_LIST_DIR}/lib/font_renderer   # UTF-8 サブセットフォント
    ${CMAKE_CURRENT_LIST_DIR}/lib/sd_path_util    # FatFS パス正規化
    ${CMAKE_CURRENT_LIST_DIR}/lib/tile_layers     # タイル背景レイヤー
    ${CMAKE_CURRENT_LIST_DIR}/lib/ST7789         # ST7789 LCD ドライバヘッダー
    ${CMAKE_CURRENT_LIST_DIR}/lib/pcm5102_i2s    # PCM5102 32bit I2S PIO
    ${CMAKE_CURRENT_LIST_DIR}/lib/sd_service       # SD マウント管理
    ${CMAKE_CURRENT_LIST_DIR}/lib/sd_card_hw     # SD ピン定義 (sd_pins.h)
    # Lua headers will be provided by FetchContent-built library
)

# ============================================
# Lua runtime via FetchContent
# ============================================
include(FetchContent)
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.6
)
FetchContent_MakeAvailable(lua)

# If luaconf.h exists in the fetched lua source, enable 32-bit mode for embedded build
if (EXISTS ${lua_SOURCE_DIR}/luaconf.h)
    file(READ ${lua_SOURCE_DIR}/luaconf.h LUACONF_CONTENT)
    string(REGEX REPLACE "#define[ \t]+LUA_32BITS[ \t]+0" "#define LUA_32BITS 1" LUACONF_CONTENT "${LUACONF_CONTENT}")
    file(WRITE ${lua_SOURCE_DIR}/luaconf.h "${LUACONF_CONTENT}")
endif()

# Build a static Lua library from upstream sources
file(GLOB LUA_SRC "${lua_SOURCE_DIR}/*.c")
list(REMOVE_ITEM LUA_SRC
    "${lua_SOURCE_DIR}/lua.c"
    "${lua_SOURCE_DIR}/onelua.c"
    "${lua_SOURCE_DIR}/ltests.c"
)
if (LUA_SRC)
    add_library(lua_static STATIC ${LUA_SRC})
    target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
    target_compile_definitions(lua_static PUBLIC LUA_32BITS)
else()
    message(FATAL_ERROR "Lua sources not found in ${lua_SOURCE_DIR}; FetchContent may have failed.")
endif()
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR}/src)


# ============================================
# SDカード / FatFS (no-OS-FatFS-SD-SDIO-SPI-RPi-Pico)
# ============================================
add_subdirectory(
    ${CMAKE_CURRENT_LIST_DIR}/lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico/src
    build_fatfs_sd
)

# ============================================
# リンクライブラリ
# ============================================
target_compile_definitions(Suityouka_Game_Machine PRIVATE
    USE_PRINTF
    USE_DBG_PRINTF
)
if(GAME_MACHINE_DEBUG)
    target_compile_definitions(Suityouka_Game_Machine PRIVATE GAME_MACHINE_DEBUG)
endif()

target_link_libraries(Suityouka_Game_Machine 
    # Pico SDK基本ライブラリ
    pico_stdlib 
    pico_multicore   # マルチコア処理（必要に応じて）
    
    # ハードウェア抽象化レイヤー
    hardware_spi      # SPI通信（LCD、SDカード用）
    hardware_i2c      # I2C通信（ボタン入力用）
    hardware_dma      # DMA転送（LCD、音声出力用）
    hardware_adc      # バッテリー電圧 (Core1)
    hardware_pio      # PCM5102 I2S（音声 PIO）
    hardware_gpio     # GPIO制御
    hardware_pwm      # LCD バックライト (BLK)
    hardware_flash    # 音量・輝度の永続化

    # SDカード/FatFSライブラリ
    no-OS-FatFS-SD-SDIO-SPI-RPi-Pico
    lua_static
)

target_link_options(Suityouka_Game_Machine PRIVATE "LINKER:--print-memory-usage")
# ============================================
# 出力ファイル生成（.uf2, .hex, .bin等）
# ============================================
pico_add_extra_outputs(Suityouka_Game_Machine)
