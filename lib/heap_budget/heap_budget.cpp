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

static void addUsed(size_t bytes) { s_used += bytes; }

static void subUsed(size_t bytes) {
    if (bytes > s_used) {
        printf("HeapBudget: underflow (used=%u, free=%u)\n", (unsigned)s_used, (unsigned)bytes);
        s_used = 0;
        return;
    }
    s_used -= bytes;
}

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

size_t used() { return s_used; }

size_t available() {
    const size_t budget = HeapConfig::BUDGET_BYTES;
    const size_t reserve = HeapConfig::RESERVE_BYTES;
    if (budget <= reserve || s_used >= budget - reserve) {
        return 0;
    }
    return (budget - reserve) - s_used;
}

size_t budget() { return HeapConfig::BUDGET_BYTES; }

size_t reserve() { return HeapConfig::RESERVE_BYTES; }

void resetAccounting() {
    uint32_t irq = save_and_disable_interrupts();
    s_used = 0;
    restore_interrupts(irq);
}

}  // namespace HeapBudget
