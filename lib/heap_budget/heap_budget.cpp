// ============================================
// ファイル: heap_budget.cpp
// ============================================

#include "heap_budget.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.hpp"
#include "hardware/sync.h"

namespace HeapBudget {

static size_t s_used = 0;

/** 予算内に bytes が収まるか判定する（内部用） */
static bool canAlloc(size_t bytes) {
    if (bytes == 0) {
        return false;
    }
    const size_t budget = HeapConfig::BUDGET_BYTES;
    const size_t reserve = HeapConfig::RESERVE_BYTES;
    if (budget <= reserve) {
        return false;
    }
    const size_t limit = budget - reserve;
    return s_used + bytes <= limit;
}

/** 使用量カウンタに bytes を加算する（割り込み無効化は呼び出し側） */
static void addUsed(size_t bytes) { s_used += bytes; }

/** 使用量カウンタから bytes を減算する（アンダーフロー時は 0 にリセット） */
static void subUsed(size_t bytes) {
    if (bytes > s_used) {
        printf("HeapBudget: underflow (used=%u, free=%u)\n", (unsigned)s_used, (unsigned)bytes);
        s_used = 0;
        return;
    }
    s_used -= bytes;
}

/** 予算内なら malloc し使用量を加算する。Lua/画像確保の入口 */
bool tryAlloc(size_t bytes, void** out) {
    if (!out) {
        return false;
    }
    *out = nullptr;
    if (!canAlloc(bytes)) {
        printf("HeapBudget: deny alloc %u (used=%u budget=%u)\n", (unsigned)bytes, (unsigned)s_used,
               (unsigned)HeapConfig::BUDGET_BYTES);
        return false;
    }

    void* p = malloc(bytes);
    if (!p) {
        printf("HeapBudget: malloc failed %u\n", (unsigned)bytes);
        return false;
    }

    uint32_t irq = save_and_disable_interrupts();
    addUsed(bytes);
    restore_interrupts(irq);

    *out = p;
    return true;
}

/** free し使用量を減算する（ptr が nullptr のときは何もしない） */
void release(void* ptr, size_t bytes) {
    if (!ptr) {
        return;
    }
    free(ptr);

    uint32_t irq = save_and_disable_interrupts();
    const size_t prev_used = s_used;
    const bool underflow = bytes > s_used;
    if (!underflow) {
        s_used -= bytes;
    } else {
        s_used = 0;
    }
    restore_interrupts(irq);

    if (underflow) {
        printf("HeapBudget: underflow (used=%u, free=%u)\n", (unsigned)prev_used, (unsigned)bytes);
    }
}

/** Lua lua_Alloc 用 realloc。失敗時は旧ブロックを維持して nullptr を返す */
void* reallocBlock(void* ptr, size_t osize, size_t nsize) {
    if (nsize == 0) {
        release(ptr, osize);
        return nullptr;
    }

    if (!ptr) {
        void* p = nullptr;
        if (!tryAlloc(nsize, &p)) {
            return nullptr;
        }
        return p;
    }

    if (nsize > osize) {
        const size_t delta = nsize - osize;
        if (!canAlloc(delta)) {
            printf("HeapBudget: deny realloc +%u (used=%u)\n", (unsigned)delta, (unsigned)s_used);
            return nullptr;
        }
    }

    void* np = realloc(ptr, nsize);
    if (!np) {
        printf("HeapBudget: realloc failed %u -> %u\n", (unsigned)osize, (unsigned)nsize);
        return nullptr;
    }

    uint32_t irq = save_and_disable_interrupts();
    if (nsize >= osize) {
        addUsed(nsize - osize);
    } else {
        const size_t delta = osize - nsize;
        if (delta > s_used) {
            restore_interrupts(irq);
            printf("HeapBudget: underflow (used=%u, free=%u)\n", (unsigned)s_used, (unsigned)delta);
            s_used = 0;
            return np;
        }
        s_used -= delta;
    }
    restore_interrupts(irq);

    return np;
}

/** 現在の使用量（バイト）を返す。 */
size_t used() { return s_used; }

/** 予約分を除いた残り確保可能バイト数を返す。 */
size_t available() {
    const size_t budget = HeapConfig::BUDGET_BYTES;
    const size_t reserve = HeapConfig::RESERVE_BYTES;
    if (budget <= reserve || s_used >= budget - reserve) {
        return 0;
    }
    return (budget - reserve) - s_used;
}

/** ヒープ全体予算（バイト）を返す。 */
size_t budget() { return HeapConfig::BUDGET_BYTES; }

/** 常に確保しておく予約バイト数を返す。 */
size_t reserve() { return HeapConfig::RESERVE_BYTES; }

/** デバッグ用: 使用量カウンタを 0 にリセット（実際の malloc は解放しない） */
void resetAccounting() {
    uint32_t irq = save_and_disable_interrupts();
    s_used = 0;
    restore_interrupts(irq);
}

}  // namespace HeapBudget
