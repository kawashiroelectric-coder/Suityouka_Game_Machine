// ============================================
// ファイル: heap_budget.cpp
// ============================================

#include "heap_budget.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config.hpp"
#include "pico/mutex.h"

namespace HeapBudget {

static size_t s_used = 0;
static mutex_t s_heap_mutex;
static bool s_mutex_ready = false;

/** Core0/Core1 からの malloc/free 競合を防ぐ */
static void ensureHeapMutex() {
    if (!s_mutex_ready) {
        mutex_init(&s_heap_mutex);
        s_mutex_ready = true;
    }
}

/** 予算内に bytes が収まるか判定する（内部用・mutex 保持中に呼ぶ） */
static bool canAllocUnlocked(size_t bytes) {
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

/** 予算内なら malloc し使用量を加算する。Lua/画像確保の入口 */
bool tryAlloc(size_t bytes, void** out) {
    if (!out) {
        return false;
    }
    *out = nullptr;
    ensureHeapMutex();
    mutex_enter_blocking(&s_heap_mutex);

    if (!canAllocUnlocked(bytes)) {
        printf("HeapBudget: deny alloc %u (used=%u budget=%u)\n", (unsigned)bytes, (unsigned)s_used,
               (unsigned)HeapConfig::BUDGET_BYTES);
        mutex_exit(&s_heap_mutex);
        return false;
    }

    void* p = malloc(bytes);
    if (!p) {
        printf("HeapBudget: malloc failed %u\n", (unsigned)bytes);
        mutex_exit(&s_heap_mutex);
        return false;
    }

    s_used += bytes;
    *out = p;
    mutex_exit(&s_heap_mutex);
    return true;
}

/** free し使用量を減算する（ptr が nullptr のときは何もしない） */
void release(void* ptr, size_t bytes) {
    if (!ptr) {
        return;
    }
    ensureHeapMutex();
    mutex_enter_blocking(&s_heap_mutex);

    free(ptr);

    const size_t prev_used = s_used;
    const bool underflow = bytes > s_used;
    if (!underflow) {
        s_used -= bytes;
    } else {
        s_used = 0;
    }
    mutex_exit(&s_heap_mutex);

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

    ensureHeapMutex();
    mutex_enter_blocking(&s_heap_mutex);

    if (nsize > osize) {
        const size_t delta = nsize - osize;
        if (!canAllocUnlocked(delta)) {
            printf("HeapBudget: deny realloc +%u (used=%u)\n", (unsigned)delta, (unsigned)s_used);
            mutex_exit(&s_heap_mutex);
            return nullptr;
        }
    }

    void* np = realloc(ptr, nsize);
    if (!np) {
        printf("HeapBudget: realloc failed %u -> %u\n", (unsigned)osize, (unsigned)nsize);
        mutex_exit(&s_heap_mutex);
        return nullptr;
    }

    if (nsize >= osize) {
        s_used += nsize - osize;
    } else {
        const size_t delta = osize - nsize;
        if (delta > s_used) {
            printf("HeapBudget: underflow (used=%u, free=%u)\n", (unsigned)s_used, (unsigned)delta);
            s_used = 0;
        } else {
            s_used -= delta;
        }
    }
    mutex_exit(&s_heap_mutex);
    return np;
}

/** 現在の使用量（バイト）を返す。 */
size_t used() {
    ensureHeapMutex();
    mutex_enter_blocking(&s_heap_mutex);
    const size_t u = s_used;
    mutex_exit(&s_heap_mutex);
    return u;
}

/** 予約分を除いた残り確保可能バイト数を返す。 */
size_t available() {
    ensureHeapMutex();
    mutex_enter_blocking(&s_heap_mutex);
    const size_t budget = HeapConfig::BUDGET_BYTES;
    const size_t reserve = HeapConfig::RESERVE_BYTES;
    size_t avail = 0;
    if (budget > reserve && s_used < budget - reserve) {
        avail = (budget - reserve) - s_used;
    }
    mutex_exit(&s_heap_mutex);
    return avail;
}

/** ヒープ全体予算（バイト）を返す。 */
size_t budget() { return HeapConfig::BUDGET_BYTES; }

/** 常に確保しておく予約バイト数を返す。 */
size_t reserve() { return HeapConfig::RESERVE_BYTES; }

/** デバッグ用: 使用量カウンタを 0 にリセット（実際の malloc は解放しない） */
void resetAccounting() {
    ensureHeapMutex();
    mutex_enter_blocking(&s_heap_mutex);
    s_used = 0;
    mutex_exit(&s_heap_mutex);
}

}  // namespace HeapBudget
