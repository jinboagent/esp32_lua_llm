#include "lua_pool_if.h"
#include <string.h>
#include <stdbool.h>

/* ---- Static pool allocator (extracted 2026-08-16, eval response) ---- */

/* Block header: 8 bytes. 32-bit fields keep offsets valid across the full
 * 128 KB pool (the pre-extraction uint16 version truncated past 64 KB). */
typedef struct {
    uint32_t size;    /* usable size (excluding header) */
    uint32_t next;    /* offset of next free block, 0 = none */
} pool_block_t;

#define POOL_HDR   ((uint32_t)sizeof(pool_block_t))
#define POOL_ALIGN 4

static uint8_t s_heap[LUA_MEMORY_LIMIT] __attribute__((aligned(4)));
static uint32_t s_top = 0;        /* bump pointer (offset past last block) */
static uint32_t s_free_head = 0;  /* address-ordered free list, 0 = empty */
static size_t s_used = 0;
static size_t s_peak = 0;

static inline pool_block_t *block_at(uint32_t off)
{
    return (pool_block_t *)(s_heap + off);
}

static inline uint32_t offset_of(const void *ptr)
{
    return (uint32_t)((const uint8_t *)ptr - s_heap);
}

static inline size_t align_up(size_t n)
{
    return (n + POOL_ALIGN - 1) & ~(size_t)(POOL_ALIGN - 1);
}

static inline void track_peak(void)
{
    if (s_used > s_peak) {
        s_peak = s_used;
    }
}

/* Give any free block that ends at s_top back to the bump pointer. */
static void s_shrink_top(void)
{
    for (;;) {
        uint32_t prev = 0;
        uint32_t cur = s_free_head;
        bool shrunk = false;
        while (cur != 0) {
            pool_block_t *blk = block_at(cur);
            if (cur + POOL_HDR + blk->size == s_top) {
                s_top = cur;
                if (prev == 0) {
                    s_free_head = blk->next;
                } else {
                    block_at(prev)->next = blk->next;
                }
                shrunk = true;
                break;
            }
            prev = cur;
            cur = blk->next;
        }
        if (!shrunk) {
            return;
        }
    }
}

void lua_pool_init(void)
{
    /* Offset 0 is the free-list "none" sentinel, so no block may live
     * there — start the bump pointer past it. */
    s_top = POOL_HDR;
    s_free_head = 0;
    s_used = 0;
    s_peak = 0;
}

void *lua_pool_alloc(size_t size)
{
    size_t asize = align_up(size);
    if (asize == 0) {
        asize = POOL_ALIGN;
    }

    /* First-fit over the address-ordered free list */
    uint32_t prev = 0;
    uint32_t cur = s_free_head;
    while (cur != 0) {
        pool_block_t *blk = block_at(cur);
        if (blk->size >= asize) {
            if (prev == 0) {
                s_free_head = blk->next;
            } else {
                block_at(prev)->next = blk->next;
            }
            s_used += blk->size + POOL_HDR;
            track_peak();
            return s_heap + cur + POOL_HDR;
        }
        prev = cur;
        cur = blk->next;
    }

    /* Bump-allocate from the top */
    size_t needed = asize + POOL_HDR;
    if ((size_t)s_top + needed > LUA_MEMORY_LIMIT) {
        return NULL; /* OOM */
    }
    uint32_t off = s_top;
    pool_block_t *blk = block_at(off);
    blk->size = (uint32_t)asize;
    blk->next = 0;
    s_top += (uint32_t)needed;
    s_used += needed;
    track_peak();
    return s_heap + off + POOL_HDR;
}

void lua_pool_free(void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    const uint8_t *p = (const uint8_t *)ptr;
    if (p < s_heap + POOL_HDR || p > s_heap + s_top) {
        return; /* not a live pool pointer */
    }

    uint32_t off = offset_of(ptr) - POOL_HDR;
    pool_block_t *blk = block_at(off);
    s_used -= blk->size + POOL_HDR;

    /* Insert address-ordered, coalescing with both neighbours */
    uint32_t prev = 0;
    uint32_t cur = s_free_head;
    while (cur != 0 && cur < off) {
        prev = cur;
        cur = block_at(cur)->next;
    }

    uint32_t newoff = off;
    pool_block_t *newblk = blk;
    if (prev != 0) {
        pool_block_t *pblk = block_at(prev);
        if (prev + POOL_HDR + pblk->size == off) {
            /* merge into the previous free block */
            pblk->size += POOL_HDR + blk->size;
            newoff = prev;
            newblk = pblk;
        }
    }
    if (newoff == off) {
        blk->next = cur;
        if (prev == 0) {
            s_free_head = off;
        } else {
            block_at(prev)->next = off;
        }
    }
    /* coalesce with the following free block */
    if (cur != 0 && newoff + POOL_HDR + newblk->size == cur) {
        newblk->size += POOL_HDR + block_at(cur)->size;
        newblk->next = block_at(cur)->next;
    }

    s_shrink_top();
}

void *lua_pool_realloc(void *ptr, size_t new_size)
{
    if (ptr == NULL) {
        return lua_pool_alloc(new_size);
    }
    if (new_size == 0) {
        lua_pool_free(ptr);
        return NULL;
    }

    size_t asize = align_up(new_size);
    if (asize == 0) {
        asize = POOL_ALIGN;
    }
    uint32_t off = offset_of(ptr) - POOL_HDR;
    pool_block_t *blk = block_at(off);
    if (asize <= blk->size) {
        return ptr; /* shrink in place (no split, keeps it simple) */
    }

    void *np = lua_pool_alloc(new_size);
    if (np == NULL) {
        return NULL;
    }
    memcpy(np, ptr, blk->size);
    lua_pool_free(ptr);
    return np;
}

size_t lua_pool_used(void)
{
    return s_used;
}

size_t lua_pool_peak(void)
{
    return s_peak;
}

size_t lua_pool_charge(const void *ptr)
{
    if (ptr == NULL) {
        return 0;
    }
    const pool_block_t *blk =
        block_at(offset_of(ptr) - POOL_HDR);
    return blk->size + POOL_HDR;
}

size_t lua_pool_header_size(void)
{
    return POOL_HDR;
}
