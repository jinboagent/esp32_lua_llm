#include "lua_if.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* ---- Static Pool Allocator (B-S3-1 fix) ---- */

/* Block header: 4 bytes overhead per allocation */
typedef struct {
    uint16_t size;    /* usable size (excluding header) */
    uint16_t next;    /* offset to next free block, 0 = allocated or end */
} pool_block_t;

#define POOL_HDR  sizeof(pool_block_t)
#define POOL_ALIGN 4

static uint8_t s_lua_heap[LUA_MEMORY_LIMIT] __attribute__((aligned(4)));
static size_t s_heap_top = 0;     /* bump pointer for new allocations */
static uint16_t s_free_head = 0;  /* head of free list (0 = empty) */
static size_t s_pool_used = 0;

static inline uint16_t offset_of(void *ptr) {
    return (uint16_t)((uint8_t *)ptr - s_lua_heap);
}

static inline pool_block_t *block_at(uint16_t off) {
    return (pool_block_t *)(s_lua_heap + off);
}

static void *s_pool_alloc(size_t size)
{
    /* Align size */
    size = (size + POOL_ALIGN - 1) & ~(POOL_ALIGN - 1);
    if (size == 0) size = POOL_ALIGN;

    /* Try free list first (first-fit) */
    uint16_t prev = 0;
    uint16_t cur = s_free_head;
    while (cur != 0) {
        pool_block_t *blk = block_at(cur);
        if (blk->size >= size) {
            /* Use this block */
            uint16_t next = blk->next;
            if (prev == 0) {
                s_free_head = next;
            } else {
                block_at(prev)->next = next;
            }
            s_pool_used += blk->size + POOL_HDR;
            return (void *)(s_lua_heap + cur + POOL_HDR);
        }
        prev = cur;
        cur = blk->next;
    }

    /* Bump allocate from top */
    size_t needed = size + POOL_HDR;
    if (s_heap_top + needed > LUA_MEMORY_LIMIT) {
        return NULL; /* OOM */
    }

    uint16_t off = (uint16_t)s_heap_top;
    pool_block_t *blk = block_at(off);
    blk->size = (uint16_t)size;
    blk->next = 0;
    s_heap_top += needed;
    s_pool_used += needed;
    return (void *)(s_lua_heap + off + POOL_HDR);
}

static void s_pool_free(void *ptr)
{
    if (ptr == NULL) return;
    if ((uint8_t *)ptr < s_lua_heap || (uint8_t *)ptr >= s_lua_heap + s_heap_top) return;

    uint16_t off = offset_of((uint8_t *)ptr - POOL_HDR);
    pool_block_t *blk = block_at(off);
    s_pool_used -= (blk->size + POOL_HDR);

    /* Add to free list (sorted by offset for future coalescing) */
    blk->next = s_free_head;
    s_free_head = off;
}

static void *s_pool_realloc(void *ptr, size_t new_size)
{
    if (ptr == NULL) return s_pool_alloc(new_size);
    if (new_size == 0) { s_pool_free(ptr); return NULL; }

    uint16_t off = offset_of((uint8_t *)ptr - POOL_HDR);
    pool_block_t *blk = block_at(off);
    size_t old_size = blk->size;

    if (new_size <= old_size) {
        return ptr; /* shrink in place (no split for simplicity) */
    }

    void *new_ptr = s_pool_alloc(new_size);
    if (new_ptr == NULL) return NULL;
    memcpy(new_ptr, ptr, old_size);
    s_pool_free(ptr);
    return new_ptr;
}

/* ---- Lua Allocator Callback ---- */

typedef struct {
    size_t used;
    size_t limit;
} lua_alloc_ctx_t;

static void *s_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    lua_alloc_ctx_t *ctx = (lua_alloc_ctx_t *)ud;
    (void)osize;

    if (nsize == 0) {
        /* Estimate freed size from pool header */
        if (ptr != NULL) {
            uint16_t off = offset_of((uint8_t *)ptr - POOL_HDR);
            pool_block_t *blk = block_at(off);
            ctx->used -= (blk->size + POOL_HDR);
        }
        s_pool_free(ptr);
        return NULL;
    }

    if (ptr == NULL) {
        if (ctx->used + nsize + POOL_HDR > ctx->limit) {
            return NULL;
        }
        void *p = s_pool_alloc(nsize);
        if (p) {
            pool_block_t *blk = block_at(offset_of((uint8_t *)p - POOL_HDR));
            ctx->used += blk->size + POOL_HDR;
        }
        return p;
    }

    /* Resize */
    if (ctx->used + nsize > ctx->limit + osize) {
        return NULL;
    }
    void *p = s_pool_realloc(ptr, nsize);
    if (p) {
        /* Recalculate used from pool */
        ctx->used = s_pool_used;
    }
    return p;
}

/* ---- Mutex (B-S3-4 fix) ---- */

static SemaphoreHandle_t s_lua_mutex = NULL;

void lua_engine_lock(void)
{
    if (s_lua_mutex != NULL) {
        xSemaphoreTake(s_lua_mutex, portMAX_DELAY);
    }
}

void lua_engine_unlock(void)
{
    if (s_lua_mutex != NULL) {
        xSemaphoreGive(s_lua_mutex);
    }
}

/* ---- Timeout Hook ---- */

typedef struct {
    uint32_t count;
    uint32_t limit;
} lua_hook_ctx_t;

static lua_hook_ctx_t s_hook_ctx;

static void s_timeout_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    s_hook_ctx.count++;
    if (s_hook_ctx.count >= s_hook_ctx.limit) {
        luaL_error(L, "execution timeout");
    }
}

/* ---- Whitelist Sandbox (B-S3-2 fix) ---- */

static void s_setup_sandbox(lua_State *L)
{
    /* Only open safe libraries — no luaL_openlibs() */
    luaopen_base(L);
    lua_pop(L, 1);

    luaL_requiref(L, "string", luaopen_string, 1);
    lua_pop(L, 1);
    luaL_requiref(L, "table", luaopen_table, 1);
    lua_pop(L, 1);
    luaL_requiref(L, "math", luaopen_math, 1);
    lua_pop(L, 1);
    luaL_requiref(L, "utf8", luaopen_utf8, 1);
    lua_pop(L, 1);

    /* Remove dangerous base functions */
    const char *blocked[] = {
        "loadfile", "dofile", "load", "collectgarbage", NULL
    };
    for (int i = 0; blocked[i] != NULL; i++) {
        lua_pushnil(L);
        lua_setglobal(L, blocked[i]);
    }
}

/* ---- Engine State ---- */

static lua_State *s_lua_state = NULL;
static lua_alloc_ctx_t s_alloc_ctx = {0};
static bool s_initialized = false;
static size_t s_peak_used = 0;

/* ---- Public API ---- */

int lua_engine_init(void)
{
    if (s_initialized) {
        return -607;
    }

    /* Create mutex */
    s_lua_mutex = xSemaphoreCreateMutex();
    if (s_lua_mutex == NULL) {
        return -600;
    }

    /* Reset pool */
    s_heap_top = 0;
    s_free_head = 0;
    s_pool_used = 0;
    s_peak_used = 0;

    s_alloc_ctx.used = 0;
    s_alloc_ctx.limit = LUA_MEMORY_LIMIT;

    s_lua_state = lua_newstate(s_lua_alloc, &s_alloc_ctx);
    if (s_lua_state == NULL) {
        vSemaphoreDelete(s_lua_mutex);
        s_lua_mutex = NULL;
        return -600;
    }

    /* Whitelist sandbox — only safe libraries */
    s_setup_sandbox(s_lua_state);

    s_initialized = true;
    printf("Lua: engine initialized (pool: %u KB, whitelist sandbox)\n",
           (unsigned)(LUA_MEMORY_LIMIT / 1024));
    return 0;
}

int lua_engine_deinit(void)
{
    if (!s_initialized) {
        return -606;
    }

    lua_engine_lock();

    if (s_lua_state != NULL) {
        lua_close(s_lua_state);
        s_lua_state = NULL;
    }

    printf("Lua: engine destroyed (peak: %u bytes)\n", (unsigned)s_peak_used);

    s_alloc_ctx.used = 0;
    s_initialized = false;

    lua_engine_unlock();

    if (s_lua_mutex != NULL) {
        vSemaphoreDelete(s_lua_mutex);
        s_lua_mutex = NULL;
    }
    return 0;
}

/* Trial compile only — no execution (B-S3-3 fix) */
int lua_engine_compile_check(const char *script, char *err, uint16_t err_len)
{
    if (script == NULL) {
        return -602;
    }
    if (!s_initialized || s_lua_state == NULL) {
        return -606;
    }

    lua_engine_lock();

    int status = luaL_loadstring(s_lua_state, script);
    if (status != LUA_OK) {
        const char *msg = lua_tostring(s_lua_state, -1);
        if (err != NULL && err_len > 0) {
            snprintf(err, err_len, "%s", msg ? msg : "compile error");
        }
        lua_pop(s_lua_state, 1);
        lua_engine_unlock();
        return -612;
    }

    /* Discard the compiled chunk — we only wanted to verify syntax */
    lua_pop(s_lua_state, 1);
    lua_engine_unlock();
    return 0;
}

int lua_engine_exec(const char *script, char *result, uint16_t result_len)
{
    if (script == NULL) {
        return -602;
    }
    if (!s_initialized || s_lua_state == NULL) {
        return -606;
    }

    lua_engine_lock();

    /* Install timeout hook */
    s_hook_ctx.count = 0;
    s_hook_ctx.limit = LUA_EXEC_TIMEOUT_INSTR;
    lua_sethook(s_lua_state, s_timeout_hook, LUA_MASKCOUNT, 100);

    /* Compile */
    int status = luaL_loadstring(s_lua_state, script);
    if (status != LUA_OK) {
        const char *err = lua_tostring(s_lua_state, -1);
        if (result != NULL && result_len > 0) {
            snprintf(result, result_len, "%s", err ? err : "compile error");
        }
        lua_pop(s_lua_state, 1);
        lua_sethook(s_lua_state, NULL, 0, 0);
        lua_engine_unlock();
        return -612;
    }

    /* Execute */
    status = lua_pcall(s_lua_state, 0, 1, 0);
    lua_sethook(s_lua_state, NULL, 0, 0);

    /* Track peak usage */
    if (s_pool_used > s_peak_used) {
        s_peak_used = s_pool_used;
    }

    if (status != LUA_OK) {
        const char *err = lua_tostring(s_lua_state, -1);

        if (err != NULL && strstr(err, "execution timeout") != NULL) {
            lua_pop(s_lua_state, 1);
            lua_engine_unlock();
            return -614;
        }

        if (status == LUA_ERRMEM) {
            lua_pop(s_lua_state, 1);
            lua_engine_unlock();
            return -610;
        }

        if (result != NULL && result_len > 0) {
            snprintf(result, result_len, "%s", err ? err : "runtime error");
        }
        lua_pop(s_lua_state, 1);
        lua_engine_unlock();
        return -613;
    }

    /* Capture return value */
    if (result != NULL && result_len > 0) {
        if (lua_gettop(s_lua_state) > 0 && !lua_isnil(s_lua_state, -1)) {
            const char *ret = lua_tostring(s_lua_state, -1);
            if (ret != NULL) {
                snprintf(result, result_len, "%s", ret);
            } else if (lua_isnumber(s_lua_state, -1)) {
                snprintf(result, result_len, "%g", lua_tonumber(s_lua_state, -1));
            } else if (lua_isboolean(s_lua_state, -1)) {
                snprintf(result, result_len, "%s",
                         lua_toboolean(s_lua_state, -1) ? "true" : "false");
            } else {
                result[0] = '\0';
            }
        } else {
            result[0] = '\0';
        }
    }

    lua_settop(s_lua_state, 0);
    lua_engine_unlock();
    return 0;
}

bool lua_engine_is_ready(void)
{
    return s_initialized;
}

int lua_engine_has_func(const char *name)
{
    if (name == NULL) {
        return -602;
    }
    if (!s_initialized || s_lua_state == NULL) {
        return -606;
    }

    lua_engine_lock();
    lua_getglobal(s_lua_state, name);
    int exists = lua_isfunction(s_lua_state, -1) ? 1 : 0;
    lua_pop(s_lua_state, 1);
    lua_engine_unlock();
    return exists;
}

int lua_engine_call_on_adv(const char *func_name, const char *addr,
                           int8_t rssi, const char *name)
{
    if (func_name == NULL) {  /* B-S3-5 fix */
        return -602;
    }
    if (!s_initialized || s_lua_state == NULL) {
        return -606;
    }

    lua_engine_lock();

    lua_getglobal(s_lua_state, func_name);
    if (!lua_isfunction(s_lua_state, -1)) {
        lua_pop(s_lua_state, 1);
        lua_engine_unlock();
        return -1;
    }

    lua_pushstring(s_lua_state, addr ? addr : "");
    lua_pushinteger(s_lua_state, rssi);
    if (name != NULL) {
        lua_pushstring(s_lua_state, name);
    } else {
        lua_pushnil(s_lua_state);
    }

    s_hook_ctx.count = 0;
    s_hook_ctx.limit = LUA_EXEC_TIMEOUT_INSTR;
    lua_sethook(s_lua_state, s_timeout_hook, LUA_MASKCOUNT, 100);

    int status = lua_pcall(s_lua_state, 3, 1, 0);
    lua_sethook(s_lua_state, NULL, 0, 0);

    if (status != LUA_OK) {
        lua_pop(s_lua_state, 1);
        lua_settop(s_lua_state, 0);
        lua_engine_unlock();
        return -613;
    }

    int result = lua_toboolean(s_lua_state, -1) ? 1 : 0;
    lua_pop(s_lua_state, 1);
    lua_settop(s_lua_state, 0);
    lua_engine_unlock();
    return result;
}

int lua_engine_call_transform(const char *func_name, const char *addr,
                              const char *json_in, char *json_out, uint16_t out_len)
{
    if (func_name == NULL) {  /* B-S3-5 fix */
        return -602;
    }
    if (!s_initialized || s_lua_state == NULL) {
        return -606;
    }

    lua_engine_lock();

    lua_getglobal(s_lua_state, func_name);
    if (!lua_isfunction(s_lua_state, -1)) {
        lua_pop(s_lua_state, 1);
        lua_engine_unlock();
        return -1;
    }

    lua_pushstring(s_lua_state, addr ? addr : "");
    lua_pushstring(s_lua_state, json_in ? json_in : "");

    s_hook_ctx.count = 0;
    s_hook_ctx.limit = LUA_EXEC_TIMEOUT_INSTR;
    lua_sethook(s_lua_state, s_timeout_hook, LUA_MASKCOUNT, 100);

    int status = lua_pcall(s_lua_state, 2, 1, 0);
    lua_sethook(s_lua_state, NULL, 0, 0);

    if (status != LUA_OK) {
        lua_pop(s_lua_state, 1);
        lua_settop(s_lua_state, 0);
        lua_engine_unlock();
        return -613;
    }

    const char *ret = lua_tostring(s_lua_state, -1);
    if (ret != NULL && json_out != NULL && out_len > 0) {
        snprintf(json_out, out_len, "%s", ret);
    }

    lua_pop(s_lua_state, 1);
    lua_settop(s_lua_state, 0);
    lua_engine_unlock();
    return 0;
}
