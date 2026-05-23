#ifndef SD_DEBUG_H
#define SD_DEBUG_H

#ifdef __cplusplus
extern "C" {
#endif

/** SD初期化の段階別診断（シリアルに詳細を出力） */
void sd_debug_run_diagnostics(void);

#ifdef __cplusplus
}
#endif

#endif
