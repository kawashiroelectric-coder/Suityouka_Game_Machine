// ============================================
// ファイル: sd_service.hpp
// SD カードの存在確認・FatFS マウント・ルート一覧
// ============================================

#ifndef SD_SERVICE_HPP
#define SD_SERVICE_HPP

/** FatFS マウント状態の管理（1 枚の SD カード想定） */
class SdService {
public:
    static bool isCardPresent();
    static bool isMounted();
    static bool mount();
    static void unmount();
    static void listRoot();
};

#endif  // SD_SERVICE_HPP
