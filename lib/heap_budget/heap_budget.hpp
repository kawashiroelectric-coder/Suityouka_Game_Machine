// ============================================
// ファイル: heap_budget.hpp
// 動的ヒープの全体予算管理（RP2040 SRAM 向け）
// ============================================

#ifndef HEAP_BUDGET_HPP
#define HEAP_BUDGET_HPP

#include <cstddef>
#include <cstdint>

/** malloc 系の使用量を追跡し、予算超過時は確保を拒否する。
 *  Lua の lua_Alloc と load_image / フォント / SE RAM 載せが共用。 */
namespace HeapBudget {

/** 予算内なら malloc し used_ を加算。Lua/画像確保の入口 */
bool tryAlloc(size_t bytes, void** out);

/** free し used_ を減算（ptr が nullptr のときは何もしない） */
void release(void* ptr, size_t bytes);

/** Lua lua_Alloc 用 realloc。失敗時は旧ブロックを維持して nullptr */
void* reallocBlock(void* ptr, size_t osize, size_t nsize);

size_t used();
/** 予約分を除いた残り確保可能バイト数を返す。 */
size_t available();
/** ヒープ全体予算（バイト）を返す。 */
size_t budget();
/** 常に確保しておく予約バイト数を返す。 */
size_t reserve();

/** デバッグ: 使用量を 0 にリセット（実際の malloc は解放しない） */
void resetAccounting();

}  // namespace HeapBudget

#endif  // HEAP_BUDGET_HPP
