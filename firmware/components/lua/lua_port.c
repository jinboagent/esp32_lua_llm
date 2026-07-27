#include "lua_if.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* ---- Allocator Context ---- */

typedef struct {
    size_t used;
    size_t limit;
} lua_alloc_ctx_t;

static void *s_lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    lua_alloc_ctx_t *ctx = (lua_alloc_ctx_t *)ud;

    if (nsize == 0) {
        /* Free */
        ctx->used -= osize;
        free(ptr);
        return NULL;
    }

    if (ptr == NULL) {
        /* New allocation */
        if (ctx->used + nsize > ctx->limit) {
            return NULL; /* OOM */
        }
        void *p = malloc(nsize);
        if (p) {
            ctx->used += nsize;
        }
        return p;
    }

    /* Resize */
    size_t new_used = ctx->used - osize + nsize;
    if (new_used > ctx->limit) {
        return NULL; /* OOM */
    }
    void *p = realloc(ptr, nsize);
    if (p) {
        ctx->used = new_used;
    }
    return p;
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

/* ---- Sandbox Setup ---- */

static void s_setup_sandbox(lua_State *L)
{
    /* Remove dangerous globals */
    const char *blocked[] = {
        "os", "io", "debug", "package", "coroutine", NULL
    };
    for (int i = 0; blocked[i] != NULL; i++) {
        lua_pushnil(L);
        lua_setglobal(L, blocked[i]);
    }

    /* Remove dangerous functions */
    const char *blocked_funcs[] = {
        "loadfile", "dofile", NULL
    };
    for (int i = 0; blocked_funcs[i] != NULL; i++) {
        lua_pushnil(L);
        lua_setglobal(L, blocked_funcs[i]);
    }
}

/* ---- Engine State ---- */

static lua_State *s_lua_state = NULL;
static lua_alloc_ctx_t s_alloc_ctx = {0};
static bool s_initialized = false;

/* ---- Public API ---- */

int lua_engine_init(void)
{
    if (s_initialized) {
        return -607;
    }

    s_alloc_ctx.used = 0;
    s_alloc_ctx.limit = LUA_MEMORY_LIMIT;

    s_lua_state = lua_newstate(s_lua_alloc, &s_alloc_ctx);
    if (s_lua_state == NULL) {
        return -600;
    }

    /* Open standard libraries */
    luaL_openlibs(s_lua_state);

    /* Apply sandbox restrictions */
    s_setup_sandbox(s_lua_state);

    s_initialized = true;
    printf("Lua: engine initialized (memory limit: %u KB)\n",
           (unsigned)(LUA_MEMORY_LIMIT / 1024));
    return 0;
}

int lua_engine_deinit(void)
{
    if (!s_initialized) {
        return -606;
    }

    if (s_lua_state != NULL) {
        lua_close(s_lua_state);
        s_lua_state = NULL;
    }

    printf("Lua: engine destroyed (peak memory: %u bytes)\n",
           (unsigned)s_alloc_ctx.used);
    s_alloc_ctx.used = 0;
    s_initialized = false;
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

    /* Install timeout hook */
    s_hook_ctx.count = 0;
    s_hook_ctx.limit = LUA_EXEC_TIMEOUT_INSTR;
    lua_sethook(s_lua_state, s_timeout_hook, LUA_MASKCOUNT, 100);

    /* Compile the script */
    int status = luaL_loadstring(s_lua_state, script);
    if (status != LUA_OK) {
        /* Compile error — get error message */
        const char *err = lua_tostring(s_lua_state, -1);
        if (result != NULL && result_len > 0) {
            snprintf(result, result_len, "%s", err ? err : "compile error");
        }
        lua_pop(s_lua_state, 1);
        lua_sethook(s_lua_state, NULL, 0, 0);
        return -612;
    }

    /* Execute */
    status = lua_pcall(s_lua_state, 0, 1, 0);

    /* Remove hook */
    lua_sethook(s_lua_state, NULL, 0, 0);

    if (status != LUA_OK) {
        const char *err = lua_tostring(s_lua_state, -1);

        /* Check if it was a timeout */
        if (err != NULL && strstr(err, "execution timeout") != NULL) {
            lua_pop(s_lua_state, 1);
            return -614;
        }

        /* Check if it was an OOM */
        if (status == LUA_ERRMEM) {
            lua_pop(s_lua_state, 1);
            return -610;
        }

        /* Runtime error */
        if (result != NULL && result_len > 0) {
            snprintf(result, result_len, "%s", err ? err : "runtime error");
        }
        lua_pop(s_lua_state, 1);
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

    /* Clear the stack */
    lua_settop(s_lua_state, 0);
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

    lua_getglobal(s_lua_state, name);
    int exists = lua_isfunction(s_lua_state, -1) ? 1 : 0;
    lua_pop(s_lua_state, 1);
    return exists;
}

int lua_engine_call_on_adv(const char *func_name, const char *addr,
                           int8_t rssi, const char *name)
{
    if (!s_initialized || s_lua_state == NULL) {
        return -606;
    }

    /* Look up the function */
    lua_getglobal(s_lua_state, func_name);
    if (!lua_isfunction(s_lua_state, -1)) {
        lua_pop(s_lua_state, 1);
        return -1; /* function not defined */
    }

    /* Push arguments: addr, rssi, name */
    lua_pushstring(s_lua_state, addr ? addr : "");
    lua_pushinteger(s_lua_state, rssi);
    if (name != NULL) {
        lua_pushstring(s_lua_state, name);
    } else {
        lua_pushnil(s_lua_state);
    }

    /* Call with timeout hook */
    s_hook_ctx.count = 0;
    s_hook_ctx.limit = LUA_EXEC_TIMEOUT_INSTR;
    lua_sethook(s_lua_state, s_timeout_hook, LUA_MASKCOUNT, 100);

    int status = lua_pcall(s_lua_state, 3, 1, 0);

    lua_sethook(s_lua_state, NULL, 0, 0);

    if (status != LUA_OK) {
        lua_pop(s_lua_state, 1);
        lua_settop(s_lua_state, 0);
        return -613;
    }

    /* Get return value (true = pass, false = suppress) */
    int result = lua_toboolean(s_lua_state, -1) ? 1 : 0;
    lua_pop(s_lua_state, 1);
    lua_settop(s_lua_state, 0);
    return result;
}

int lua_engine_call_transform(const char *func_name, const char *addr,
                              const char *json_in, char *json_out, uint16_t out_len)
{
    if (!s_initialized || s_lua_state == NULL) {
        return -606;
    }

    lua_getglobal(s_lua_state, func_name);
    if (!lua_isfunction(s_lua_state, -1)) {
        lua_pop(s_lua_state, 1);
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
        return -613;
    }

    const char *ret = lua_tostring(s_lua_state, -1);
    if (ret != NULL && json_out != NULL && out_len > 0) {
        snprintf(json_out, out_len, "%s", ret);
    }

    lua_pop(s_lua_state, 1);
    lua_settop(s_lua_state, 0);
    return 0;
}
