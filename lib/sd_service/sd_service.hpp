// ============================================
// ファイル: sd_service.hpp
// SD カードの存在確認・FatFS マウント・ルート一覧
//
// game_machine_main が mount/unmount を呼び、Lua 側は setSdMounted で状態同期。
// SPI 低レイヤは lib/sd_card_hw + no-OS-FatFS に委譲。
// ============================================

#ifndef SD_SERVICE_HPP
#define SD_SERVICE_HPP

/** FatFS マウント状態の管理（1 枚の SD カード想定） */
class SdService {
public:
    /** カード挿入 GPIO の状態（マウント前でも可） */
    static bool isCardPresent();
    /** FatFS がマウント済みかどうか */
    static bool isMounted();
    /** hw_config + f_mount。成功時のみ true */
    static bool mount();
    /** f_unmount（ファイルエクスプローラ終了時等） */
    static void unmount();
    /** f_unmount のあと f_mount し直す（ゲーム終了後の FatFS 復旧用） */
    static bool remount();
    /** デバッグ: ルートディレクトリを printf */
    static void listRoot();
};

#endif  // SD_SERVICE_HPP
