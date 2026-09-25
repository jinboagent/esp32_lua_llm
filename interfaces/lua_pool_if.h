/*
 * lua_pool_if.h — static pool allocator for the Lua engine.
 *
 * Extracted from lua_port.c in the 2026-08-16 evaluation response:
 *   - 32-bit offsets and sizes (the old uint16 fields silently truncated
 *     past 64 KB of a 128 KB pool — latent correctness bug)
 *   - address-ordered free list with real on-free coalescing (the old
 *     head-insert list never coalesced despite the comment)
 *   - single accounting ledger (used/peak live here, not mirrored)
 *   - top blocks are returned to the bump pointer on free
 *
 * Pure C, no FreeRTOS dependency: host-unit-testable.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/* Memory limit for the Lua pool (was lua_if.h; moved here with the pool) */
#define LUA_MEMORY_LIMIT       (128 * 1024)

/* Reset the pool: bump pointer, free list, counters. */
void lua_pool_init(void);

/* Allocate `size` usable bytes (aligned to 4). NULL on exhaustion. */
void *lua_pool_alloc(size_t size);

/* Free a pointer returned by alloc/realloc (NULL and foreign pointers
 * are ignored). Coalesces with address-adjacent free blocks. */
void lua_pool_free(void *ptr);

/* Resize; may move. NULL on exhaustion (old block untouched). */
void *lua_pool_realloc(void *ptr, size_t new_size);

/* Bytes currently charged (block sizes + headers). */
size_t lua_pool_used(void);

/* High-water mark of lua_pool_used(). */
size_t lua_pool_peak(void);

/* Total bytes charged for a live allocation (header + aligned size). */
size_t lua_pool_charge(const void *ptr);

/* Per-allocation header overhead in bytes. */
size_t lua_pool_header_size(void);
